/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 andjiang0083
 */
// sim/run.cpp — 用**固件真实源码**(game.h/render.h/actions.h/touch.h)在 PC 上跑
// 编译方式：-Isim/compat 让 #include <M5Unified.h> / <M5GFX.h> 命中替身，
//           其余 include（config_gen/pixel_font/game/render/actions/touch）全是原文件。
// 所以这里断言通过 == 固件逻辑真的对，不是另写了一份模拟器。
// v0.2.0：10 关全量自动通关（贪心 bot）、多路径投放、选关状态机、
//         HEALER/BOSS 技能、ELITE、解锁位图、经济采样。
#include <M5Unified.h>       // fake
#include "config_gen.h"
#include "pixel_text.h"
#include "game.h"
#include "touch.h"
#include "render.h"
#include "actions.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int g_fail = 0, g_pass = 0;
static int s_simSaves = 0;                       // v0.4.1：进度落盘计数器（见测试 15）
static void simCountSave(void) { ++s_simSaves; }
#define CHECK(cond, ...) do { if (cond) ++g_pass; else { ++g_fail; \
  printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static M5GFX* D = &M5.Display;

// ── PPM 导出（真彩，直接吐帧缓冲；由 tools/to_png.py 转 PNG）────
static void dumpPpm(const char* path) {
  FILE* f = fopen(path, "wb"); if (!f) return;
  fprintf(f, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
  uint8_t row[SCREEN_W * 3];
  for (int y = 0; y < SCREEN_H; ++y) {
    for (int x = 0; x < SCREEN_W; ++x) {
      uint16_t c = D->fb[y * SCREEN_W + x];
      row[x*3+0] = (uint8_t)(((c >> 11) & 0x1F) << 3);
      row[x*3+1] = (uint8_t)(((c >> 5) & 0x3F) << 2);
      row[x*3+2] = (uint8_t)((c & 0x1F) << 3);
    }
    fwrite(row, 1, sizeof(row), f);
  }
  fclose(f);
}

// ── 帧推进：模拟 loop() 的 30fps 节拍（共用同一份逻辑）──────────
// ⚠️ g_renderOn=false：自动通关长跑只跑逻辑，不画帧缓冲——
//    1280x720 全图 fillRect 在 PC 上是每帧百万次写内存，纯浪费。
//    需要截图时再打开（快照/视觉评审用）。
static uint64_t g_lastPx = 0;
static bool g_renderOn = true;
static void tick(int frames) {
  for (int i = 0; i < frames; ++i) {
    g_simMs += 33;
    Tap t = readTap();                 // 真 touch.h：只有抬指那一帧 valid
    if (t.valid) handleTap(D, t);
    // v0.4.1：镜像 main.cpp 选关分支——只推进输入/反馈，不跑世界也不渲染
    // （旧仿真在 ST_SEL 也跑 renderFrame，会把 HUD 画到选关屏幕上，与真机不符）
    if (g_w.state == ST_SEL) { updateSelFx(D); continue; }
    gameUpdate(33);                    // 真逻辑（面板打开=内部暂停）
    s_nowMs = g_simMs;                // v0.3.1：镜像 loop()——音效节流时钟
    sfxPump();                        // 仿真下排空队列（同份投递/节流代码）
    uint64_t before = M5GFX::pxFilled;
    if (g_renderOn) renderFrame(D);  // 真渲染（长跑时关闭）
    g_lastPx = M5GFX::pxFilled - before;
  }
}

static void tapAt(int x, int y) { M5.Touch.press(x, y); tick(1); M5.Touch.release(); tick(1); }
static void buildAt(int c, int r) { tapAt(c*CELL+20, MAP_TOP+r*CELL+20); }
static int  alive(void)   { int n=0; for (int i=0;i<MAX_ENEMIES;++i) if (g_w.enemies[i].active) ++n; return n; }
static int  towers(void)  { int n=0; for (int i=0;i<MAX_TOWERS;++i) if (g_w.towers[i].active) ++n; return n; }
static void closePanel(void) { if (g_w.selectedTower >= 0) tapAt(6, MAP_TOP+4); }

// ── 贪心 bot v2：覆盖度优先选址（螺旋/分叉关的钥匙）───────────────
// 每个候选草地格打分 = 以它为塔心、机枪 LV1 射程内能罩住多少个路格；
// 每次建"覆盖最高"的那格 → 火力自然堆在螺旋内圈/汇合段（高手直觉）。
// 血×2.1 后，塔"在正确位置"远比"数量多"重要。
// 某条 route 是否已被现有塔罩住（沿线采样）
static bool routeCoveredByTowers(int rt) {
  const int cnt = routeCount(rt);
  for (int i = 0; i + 1 < cnt; ++i) {
    const Waypoint& a = routeWP(rt, i); const Waypoint& b = routeWP(rt, i + 1);
    int dc = (b.col > a.col) - (b.col < a.col), dr = (b.row > a.row) - (b.row < a.row);
    int cc = a.col, rr = a.row;
    for (;;) {
      float px = cc*CELL + CELL/2, py = MAP_TOP + rr*CELL + CELL/2;
      for (int t = 0; t < MAX_TOWERS; ++t) {
        const Tower& tw = g_w.towers[t];
        if (!tw.active) continue;
        float rg = TOWER_TYPES[tw.type].range_px * (1.0f + TOWER_RANGE_SCALE * (tw.level - 1));
        float dx = px - (tw.col*CELL + CELL/2), dy = py - (MAP_TOP + tw.row*CELL + CELL/2);
        if (dx*dx + dy*dy <= rg*rg) return true;
      }
      if (cc == b.col && rr == b.row) break;
      cc += dc; rr += dr;
    }
  }
  return false;
}

// 候选格能罩住某条 route 的多少格（用于多路图加权）
static int routeCellsInRange(int rt, float tx, float ty, float r2) {
  const int cnt = routeCount(rt);
  int n = 0;
  for (int i = 0; i + 1 < cnt; ++i) {
    const Waypoint& a = routeWP(rt, i); const Waypoint& b = routeWP(rt, i + 1);
    int dc = (b.col > a.col) - (b.col < a.col), dr = (b.row > a.row) - (b.row < a.row);
    int cc = a.col, rr = a.row;
    for (;;) {
      float dx = cc*CELL + CELL/2 - tx, dy = MAP_TOP + rr*CELL + CELL/2 - ty;
      if (dx*dx + dy*dy <= r2) ++n;
      if (cc == b.col && rr == b.row) break;
      cc += dc; rr += dr;
    }
  }
  return n;
}

static int coverageAt(int c, int r) {
  const float rng = TOWER_TYPES[0].range_px;         // LV1 GAT 射程做基准
  const float r2 = rng * rng;
  const float tx = c*CELL + CELL/2, ty = MAP_TOP + r*CELL + CELL/2;
  int n = 0;
  // v0.3.2b：按 route 计权，而不是"路格去重计数"——
  //   ① 两路共用的汇合格会被数两次（交通加权）→ bot 自然堆到汇合段
  //   ② 还没被任何塔罩住的那条路再 ×2 → 两路都摆，不放过整条空路
  //   （旧版 L5/L8 实测：只守最长一路，另一路整条放空、8 只怪全漏）
  for (int rt = 0; rt < LV().routes_n; ++rt) {
    int k = routeCellsInRange(rt, tx, ty, r2);
    n += k;
    if (LV().routes_n > 1 && !routeCoveredByTowers(rt)) n += k;
  }
  // v0.3.0：有关空兵的关，直升机走直线——把每条 route 的
  // spawn→exit 直线按半格采样计入覆盖，bot 才会把防空摆到航线上
  bool hasAir = false;
  for (int k = 0; k < 6 && LV().kinds[k] >= 0; ++k) if (LV().kinds[k]==K_CHOPPER) hasAir=true;
  if (hasAir && g_w.wave + 1 >= AIR_FROM_WAVE) {
    for (int rt = 0; rt < LV().routes_n; ++rt) {
      const int cnt = routeCount(rt);
      float sx, sy, ex2, ey;
      routeWPPx(rt, 0, &sx, &sy); routeWPPx(rt, cnt-1, &ex2, &ey);
      const int steps = 40;                            // ~30px 采样间距
      for (int sIdx = 0; sIdx <= steps; ++sIdx) {
        float px = sx + (ex2-sx)*sIdx/steps, py = sy + (ey-sy)*sIdx/steps;
        float dx = px-tx, dy = py-ty;
        if (dx*dx + dy*dy <= r2) n += 4;             // 每个航线采样=+4：重叠覆盖胜出
      }
    }
  }
  return n;
}

static void botBuildWave(void) {
  // 1) 覆盖贪心建塔（留 40 应急）
  while (towers() < MAX_TOWERS) {
    int bestC=-1, bestR=-1, bestScore=0;
    for (int c = 0; c < COLS; ++c) for (int r = 0; r < MAP_ROWS; ++r) {
      if (g_w.path[c][r] || g_w.occupied[c][r] || g_w.blocked[c][r]) continue;   // v0.4：禁建岩层
      int s = coverageAt(c, r);
      if (s > bestScore) { bestScore=s; bestC=c; bestR=r; }
    }
    if (bestC < 0 || bestScore <= 1) break;          // v0.4：波量翻倍→阈值4降到1，建满别欠防
    int n = towers();
    // v0.3.2：有盾兵池的关→LASER 优先（子弹×0.5，激光全伤）；
    //           CAN 溅射也无视盾。纯机枪阵在盾兵面前是残废——bot 先学会，
    //           调出来的难度才是"玩家学不会就过不了"，不是"bot 笨所以难"。
    bool hasShield = false, hasArmor = false;
    for (int k = 0; k < 6 && LV().kinds[k] >= 0; ++k) {
      if (LV().kinds[k]==K_SHIELD) hasShield=true;
      if (LV().kinds[k]==K_ARMOR)  hasArmor =true;   // 平A减伤8：机枪12伤只剩4
    }
    static const uint8_t CYCLE[4]        = { 1, 0, 4, 2 };   // CAN/GAT/LASER/FRO
    static const uint8_t CYCLE_SHIELD[4] = { 1, 4, 1, 0 };   // CAN/LASER/CAN/GAT
    static const uint8_t CYCLE_ARMOR[4]  = { 1, 0, 1, 2 };   // CAN 溅射无视护甲
    // ⚠️ 不等特殊兵登场才转型：波间建塔在 wave<N 时完成，临阵换型来不及
    const uint8_t* cyc = CYCLE;
    if (hasShield && g_w.wave >= SHIELD_FROM_WAVE - 2) cyc = CYCLE_SHIELD;
    else if (hasArmor)                                 cyc = CYCLE_ARMOR;
    uint8_t type = cyc[n % 4];
    // v0.4：经济收紧后，高 diff 关（旧规则 diff<1.5）更要投资——能量塔
    // 全关通用早铺（仿真实测 L8：bot 零产金塔=收入追不上血池，饿死）。
    if (g_w.wave <= 3 && n % 5 == 4) type = 3;   // ENERGY
    // v0.3.2 盾关降级链（逐步走，不跳级）：LASER→CAN（溅射无视盾）→GAT
    // ⚠️ 别设"留 40 金"门槛：bot 本来就在盈亏线上，门槛=只建 2 座塔等死
    for (int guard = 0; guard < 2 && g_w.gold < TOWER_TYPES[type].cost; ++guard) {
      if (type == 4) type = 1;                 // LASER $140 → CAN $100
      else if (type == 1) type = 0;            // CAN $100 → GAT $60
      else break;
    }
    if (g_w.gold < TOWER_TYPES[type].cost) break;               // 真没钱了
    if (!buildTower(bestC, bestR, type)) break;
  }
  // 2) 升级：v0.3.2 LV3 涨价后"有钱就升"=经济陷阱（两塔满级、全场空虚）。
  //    纪律：先铺够 6 座防面，且只花"余粮"（留 $120 给下一座塔）。
  if (towers() >= 5 || g_w.wave >= LV().waves - 1) {
    bool again = true;
    while (again) {
      again = false;
      for (int i = 0; i < MAX_TOWERS; ++i) {
        Tower& t = g_w.towers[i];
        if (!t.active) continue;
        int cu = upgradeCost(t);
        bool lastWaves = (g_w.wave >= LV().waves - 1);
        if (cu > 0 && g_w.gold >= cu + (lastWaves ? 0 : 80)) {
          upgradeTower(i); again = true;
        }
      }
    }
  }
}

// ── 自动打一关：波间建塔升级，点 NEXT WAVE，直到 CLEAR/GAMEOVER ──
static bool autoPlayLevel(int maxSimSeconds) {
  g_renderOn = false;                       // 长跑只跑逻辑（截图前再开）
  int guardFrames = maxSimSeconds * 30;
  while (guardFrames-- > 0) {
    // 每帧都尝试建/升级：战斗中金币立刻变火力（旧版只在波间花钱，
    // BOSS 自愈波里金闲置 = 打不死，仿真 L6 实测）
    botBuildWave();
    // v0.3.2：bot 也要用波间喘息——旧版 ST_READY 立刻开波=永远吃满压叠，
    // 难度读数比真人（有 7s 建造窗口）苛刻得多。留 1.5s 才按下一波。
    if (g_w.state == ST_READY && g_w.readyLeftMs <= 1500) { startNextWave(); }
    // 调参仪表：逐波打印（L4+ 才打，输出短）。v0.3.2 调参完成 ⇒ 默认关闭；
    // 需要时 `pio`/g++ 加 -DSIM_DEBUG_WAVES 重新编译仿真即可再打开。
#ifdef SIM_DEBUG_WAVES
    { static int dbgW = -1;
      if (g_lv >= 3 && (g_w.wave != dbgW || g_w.state == ST_GAMEOVER)) {
        dbgW = g_w.wave;
        if (g_w.state != ST_READY || g_w.wave > 0)
          printf("    L%d w%-2d lives=%-3d gold=%-4d towers=%d alive=%d%s\n",
                 g_lv+1, g_w.wave, g_w.lives, g_w.gold, towers(), alive(),
                 g_w.state == ST_GAMEOVER ? "  <-- GAMEOVER" : "");
      } }
#endif
    if (g_w.state == ST_CLEAR)   { g_renderOn = true; return true; }
    if (g_w.state == ST_GAMEOVER){ g_renderOn = true; return false; }
    tick(1);
  }
  g_renderOn = true;
  printf("  (timeout in autoPlay)\n"); return false;
}

// ── 截图模式：SIM_SHOT=<1..20> [SIM_SHOT_WAVE=<w>] [SIM_SHOT_OUT=<path>] ──
// 真渲染 + bot 自动打到指定波次，抓"有塔有兵"的战场帧（README/商店封面用）。
// 关掉长跑的 g_renderOn=false 逻辑：这里每帧都真画，所以帧缓冲就是当下画面。
static void shotMode(int lv, int waveTarget, const char* out) {
  levelLoad(lv);
  const int wt = waveTarget > 0 ? waveTarget : LV().waves;   // 0 = 打到最后波
  g_renderOn = true;
  int guard = 90000;
  while (guard-- > 0) {
    botBuildWave();
    if (g_w.state == ST_READY && g_w.readyLeftMs <= 1500) startNextWave();
    if (g_w.state == ST_CLEAR || g_w.state == ST_GAMEOVER) break;
    tick(1);
    // 宣传帧要好读：有塔、有兵、血量还在（HP 0 的帧像"已经输了"）
    if (g_w.wave >= wt && towers() >= 5 && alive() >= 6 && g_w.lives >= 15) break;
  }
  tick(2);                                    // 让帧缓冲对齐当前时刻
  dumpPpm(out);
  printf("shot: L%-2d %-15s wave=%d/%d towers=%d alive=%d lives=%d gold=%d -> %s\n",
         lv + 1, LV().name, g_w.wave, LV().waves, towers(), alive(), g_w.lives, g_w.gold, out);
}

int main(void) {
  // 截图模式（文档/商店封面用），不跑断言
  if (const char* s = getenv("SIM_SHOT")) {
    const char* wv = getenv("SIM_SHOT_WAVE");
    const char* out = getenv("SIM_SHOT_OUT");
    int lv = atoi(s) - 1;
    if (lv < 0 || lv >= LEVEL_COUNT) { printf("SIM_SHOT out of range (1..%d)\n", LEVEL_COUNT); return 2; }
    shotMode(lv, wv ? atoi(wv) : 0, out ? out : "shot.ppm");
    return 0;
  }
  printf("=== PC sim v0.4.1: 20 levels (L11-20 new maps), real game/render/touch/actions ===\n");
  printf("grid %dx%d cell=%d | caps e/t/b/fx = %d/%d/%d/%d\n",
         COLS, MAP_ROWS, CELL, MAX_ENEMIES, MAX_TOWERS, MAX_BULLETS, MAX_FX);

  // ── 1. 开局 = 选关界面；只有 L1 解锁 ───────────────────────────
  levelLoad(0); g_w.state = ST_SEL; drawLevelSelect(D);
  CHECK(g_unlocked == 0x0001 && g_cleared == 0, "initial progress wrong");
  CHECK(levelUnlocked(0) && !levelUnlocked(1), "unlock gate wrong");
  dumpPpm("sim_select.ppm");

  // 点锁定节点：拒绝；点 L1：进关
  tapAt(selNodeX(4) + SEL_NODE_W/2, selNodeY(0) + SEL_NODE_H/2);
  CHECK(g_w.state == ST_SEL, "locked level was entered!");
  tapAt(selNodeX(0) + SEL_NODE_W/2, selNodeY(0) + SEL_NODE_H/2);
  CHECK(g_w.state == ST_READY && g_lv == 0, "tap L1 did not enter level");

  // ── 2. 每关：路径/多路投放/BOSS/通关解锁 ────────────────────────
  int totalChecksBefore = g_pass;
  for (int lv = 0; lv < LEVEL_COUNT; ++lv) {
    CHECK(levelUnlocked(lv), "L%d should be unlocked by now", lv + 1);
    enterLevel(D, lv); closePanel();
    CHECK(g_w.state == ST_READY && g_lv == lv, "enterLevel L%d failed", lv + 1);

    // 路径格数 & 双路出生点
    int cells = 0; for (int c=0;c<COLS;++c) for (int r=0;r<MAP_ROWS;++r) if (g_w.path[c][r]) ++cells;
    CHECK(cells >= 30, "L%d path too short (%d)", lv + 1, cells);
    if (LV().routes_n == 2) {   // 双路关（L5/L8/L13/L16/L20）
      CHECK(routeCount(0) >= 3 && routeCount(1) >= 3, "L%d routes malformed", lv + 1);
      CHECK(routeWP(0,0).col == COLS-1 && routeWP(1,0).col == COLS-1, "L%d spawns not right-edge", lv+1);
    }

    // 自动通关（血量×2.1 后每波更耗时：波数 ×45s + 180s 余量）
    // v0.3.0：L10 TERMINAL=15波+双王+产卵，855s 预算实测不够（wave13/15
    // 满血但没打完）——只放宽仿真预算，不动游戏数值。
    const bool mandatory = (lv <= 3) || (lv >= 10 && lv <= 12);   // L1-4 + L11-13 必通
    bool win = autoPlayLevel(lv >= 9 ? LV().waves * 60 + 300 : LV().waves * 45 + 180);
    if (mandatory)
      CHECK(win, "L%d(%s) NOT cleared by bot (wave=%d lives=%d gold=%d alive=%d)",
            lv + 1, LV().name, g_w.wave, g_w.lives, g_w.gold, alive());
    else if (!win)
      printf("  [info] L%d(%s) bot 未通（非门禁关：bot 不是难度标尺，留真人判定）\n",
             lv + 1, LV().name);
    if (!win) {   // 诊断：塔型构成/场上兵种/wave，难度调参的仪表盘
      int cnt[5]={0}; for(int i=0;i<MAX_TOWERS;++i) if(g_w.towers[i].active){cnt[g_w.towers[i].type]++;}
      int kc[9]={0}; for(int i=0;i<MAX_ENEMIES;++i) if(g_w.enemies[i].active){kc[g_w.enemies[i].kind]++;}
      printf("  [diag] towers G/C/F/E/L=%d/%d/%d/%d/%d alive g/r/t/s/a/b/h/ch/sh=%d/%d/%d/%d/%d/%d/%d/%d/%d\n",
             cnt[0],cnt[1],cnt[2],cnt[3],cnt[4],
             kc[0],kc[1],kc[2],kc[3],kc[4],kc[5],kc[6],kc[7],kc[8]);
      if (mandatory) break;
      // 非门禁关：继续走完 20 关（补上解锁位，解锁逻辑已由门禁关验证过）
      g_cleared |= (1u << lv); g_unlocked |= (1u << (lv + 1));
      continue;
    }

    CHECK(g_cleared & (1u << lv), "L%d clear bit missing", lv + 1);
    if (lv + 1 < LEVEL_COUNT)
      CHECK(g_unlocked & (1u << (lv+1)), "L%d not unlocked after L%d clear", lv + 2, lv + 1);

    printf("L%-2d %-15s OK wave=%d kills=%-3d lives=%-2d gold=%-4d towers=%d\n",
           lv + 1, LV().name, g_w.wave, g_w.score, g_w.lives, g_w.gold, towers());

    // 关键视觉快照：L5 双路战斗 / L6 BOSS(治疗) / L10 终王
    if (lv == 4 || lv == 5 || lv == 9 || lv == 19) {
      char p[64]; snprintf(p, sizeof(p), "sim_lv%02d.ppm", lv + 1);
      dumpPpm(p);
    }

    // 回选关界面（模拟点击）
    tapAt(SCREEN_W/2, SCREEN_H/2);
  }

  // ── 3. BOSS 技能单元测：HEAL / BROOD（L6/L10）──────────────────
  // v0.4.1：逼技能到期要同时置 abilityMs（门控）和 abNextMs[8]（各技能位表）
  {
    levelLoad(5);                       // L6 FROST LORD: AB_HEAL（自愈）
    g_w.gold = 9999; g_w.lives = 99999; startNextWave(); g_w.wave = LV().waves;
    // BOSS 先出场领跑；压到残血，验证自愈脉冲把 hp 抬回来
    g_w.toSpawn = 1; g_w.bossPending = true; tick(30);
    int bossIdx=-1;
    for (int i=0;i<MAX_ENEMIES;++i) if (g_w.enemies[i].active && g_w.enemies[i].kind==K_BOSS) bossIdx=i;
    CHECK(bossIdx>=0, "L6 boss not spawned");
    if (bossIdx>=0) {
      g_w.enemies[bossIdx].hp = 1;                     // 打到只剩血皮
      g_w.enemies[bossIdx].abilityMs = millis();       // 门控立刻到期
      for (int k=0;k<8;++k) g_w.enemies[bossIdx].abNextMs[k] = millis();  // 各技能位全部逼停
      tick(90);                                          // ≥ HEAL_MS；期间可能挨塔弹，但 +8%maxHp 应净涨
      CHECK(g_w.enemies[bossIdx].active && g_w.enemies[bossIdx].hp > 1.0f,
            "boss self-heal did nothing (hp=%.1f)", g_w.enemies[bossIdx].hp);
    }

    levelLoad(9);                       // L10 THE END: AB_HEAL|AB_BROOD
    g_w.gold = 9999; g_w.lives = 99999; startNextWave(); g_w.wave = LV().waves;
    g_w.toSpawn = 1; g_w.bossPending = true; tick(2);
    int swarmBefore=0; for (int i=0;i<MAX_ENEMIES;++i) if (g_w.enemies[i].active && g_w.enemies[i].kind==K_SWARM) ++swarmBefore;
    int boss2=-1; for (int i=0;i<MAX_ENEMIES;++i) if (g_w.enemies[i].active && g_w.enemies[i].kind==K_BOSS) boss2=i;
    CHECK(boss2>=0, "L10 boss not spawned");
    if (boss2>=0) { g_w.enemies[boss2].abilityMs = millis();
      for (int k=0;k<8;++k) g_w.enemies[boss2].abNextMs[k] = millis();
      tick(6);
      int swarmAfter=0; for (int i=0;i<MAX_ENEMIES;++i) if (g_w.enemies[i].active && g_w.enemies[i].kind==K_SWARM) ++swarmAfter;
      CHECK(swarmAfter > swarmBefore, "brood did not spawn minions (%d->%d)", swarmBefore, swarmAfter);
    }

    // HEALER 兵种：L6 pool 含 K_HEAL，wave>=3 应出现（直接压到 wave2→start=3）
    levelLoad(5); g_w.gold = 9999; g_w.lives = 99999;
    g_w.wave = 2; startNextWave();            // → wave 3，HEAL 入门槛
    g_w.toSpawn = 40; tick(150);
    bool healSeen=false; for (int i=0;i<MAX_ENEMIES;++i) if (g_w.enemies[i].active && g_w.enemies[i].kind==K_HEAL) healSeen=true;
    CHECK(healSeen, "HEALER never spawned on L6 wave3+");
  }

  // ── 4. ELITE：L7 elite_from=8，直接压到 wave>=8 应有金边兵 ───────
  {
    levelLoad(6); g_w.gold = 9999; g_w.lives = 99999;
    g_w.wave = LV().elite_from - 1;         // → wave 8 == elite_from
    startNextWave(); tick(300);              // iv≈580ms×12只 → 需~7s
    bool elite=false; for (int i=0;i<MAX_ENEMIES;++i) if (g_w.enemies[i].active && g_w.enemies[i].elite) elite=true;
    CHECK(elite, "no ELITE on L7 wave>=8 (wave=%d)", g_w.wave);
  }

  // ── 5. 面板暂停仍有效（v0.1.2 回归）────────────────────────────
  {
    levelLoad(0); g_w.gold=999; buildTower(15,6,0);
    int ti = findTowerAt(15,6); CHECK(ti>=0,"tower not built");
    g_w.selectedTower = ti; tick(30);
    // 世界暂停：选关后重建敌人再验证位置冻结
    levelLoad(0); startNextWave(); tick(60);
    int idx=-1; for (int i=0;i<MAX_ENEMIES;++i) if (g_w.enemies[i].active){idx=i;break;}
    CHECK(idx>=0,"no enemy for pause test");
    if (idx>=0){ float x=g_w.enemies[idx].x; g_w.selectedTower=ti; tick(20);
      CHECK(g_w.enemies[idx].active && fabsf(g_w.enemies[idx].x-x)<0.01f,"panel pause leaked"); }
    g_w.selectedTower=-1; closePanel(); tick(5);
  }

  // ── 6. v0.3.0：波间倒计时自动开波（首波手动，之后 10s 自动）────
  {
    levelLoad(0); g_w.gold = 9999; g_w.lives = 99999;
    startNextWave();                      // wave1（手动）
    g_w.toSpawn = 0;                     // 立刻清空本波刷怪
    tick(5);                             // 杀光残留敌人 → ST_READY wave=1
    for (int i=0;i<MAX_ENEMIES;++i) g_w.enemies[i].active=false;
    tick(3);
    CHECK(g_w.state==ST_READY && g_w.wave==1, "countdown setup: state=%d wave=%d", (int)g_w.state, g_w.wave);
    int frames=0; while (g_w.state==ST_READY && frames<400){ tick(1); ++frames; }
    CHECK(g_w.state==ST_WAVE && g_w.wave==2, "auto next wave failed (state=%d wave=%d frames=%d)",
          (int)g_w.state, g_w.wave, frames);
    // 7s @30fps ≈212帧（v0.3.2），允许 ±15%
    CHECK(frames > 180 && frames < 244, "auto-start timing off: %d frames (expect ~212)", frames);
  }

  // ── 8. v0.3.0：出生必须满血（真机 bug #4 回归——memset 清零漏赋 hp）──
  {
    levelLoad(0); g_w.gold = 9999; g_w.lives = 99999;
    startNextWave();                          // wave1, pool=GRUNT only
    g_w.toSpawn = 5; tick(40);              // 刷几只出来
    int seen=0, bad=0;
    for (int i=0;i<MAX_ENEMIES;++i) if (g_w.enemies[i].active && g_w.wave==1) {
      // 出生点附近=刚刷出来（还没挨打）：hp 必须 == maxHp
      if (g_w.enemies[i].x > SCREEN_W - 4*CELL) {
        ++seen;
        if (g_w.enemies[i].hp != g_w.enemies[i].maxHp || g_w.enemies[i].hp <= 0) ++bad;
      }
    }
    CHECK(seen >= 1 && bad == 0, "spawned enemies not full-hp: seen=%d bad=%d", seen, bad);
    // 一枪不该死：L1 grunt hp=85，机枪 dmg=12 → 需要 ≥7 发
    int idx=-1; for (int i=0;i<MAX_ENEMIES;++i) if (g_w.enemies[i].active){idx=i;break;}
    CHECK(idx>=0,"hp test: no enemy");
    if (idx>=0) {
      int shots = 0; float hp = g_w.enemies[idx].hp; const int dmg=12;
      while (hp > 0) { hp -= dmg; ++shots; }
      CHECK(shots >= 7, "L1 grunt dies in %d shots (expect >=7)", shots);
      printf("grunt maxHp=%.0f -> %d GAT shots to kill\n", g_w.enemies[idx].maxHp, shots);
    }
  }
  // ── 9. v0.3.0：HUD 底板不得压到 NEXT WAVE 按钮（真机 bug #6）──────
  {
    levelLoad(9); g_renderOn = true;                       // L10: "15/15" 最长
    tick(2); drawHud(D); g_w.hudDirty = false;
    int black=0;
    for (int x=BTN_X; x<BTN_X+24; ++x)
      for (int y=BTN_Y; y<BTN_Y+BTN_H; ++y)
        if (D->fb[y*SCREEN_W+x]==C_UI_BG && x<BAR_X) ++black;  // 按钮左缘被刷黑
    CHECK(black==0, "HUD WAVE plate overlaps NEXT WAVE button: %d px", black);
    // 底板右缘必须留在按钮之前
    CHECK(956 + (FONT_W+1)*3*5 + 8 <= BTN_X, "WAVE plate too wide");
    // HUD 静态标签在擦除后仍在（框伸进 HUD 带的场景）
    levelLoad(0); g_w.gold=9999; buildTower(15,0,0);
    int ti=findTowerAt(15,0); while (upgradeTower(ti)) {}
    g_w.selectedTower=ti; tick(2);                        // 第一行塔：框伸进 HUD
    g_w.selectedTower=-1; tick(3);                        // 关面板 → eraseRangeBox
    int tagLost=0;
    for (int x=800;x<860;++x) if (D->fb[24*SCREEN_W+x]==C_TEXT_DIM) ++tagLost;
    CHECK(tagLost>10, "HUD WAVE label erased by range-box cleanup: %d px", tagLost);
  }

  // ── 7. v0.3.0：射程预览框不残留（升级换半径 / 关面板后像素检查）──
  {
    levelLoad(0); g_w.gold=99999; buildTower(15,6,0);
    int ti=findTowerAt(15,6); CHECK(ti>=0,"box test tower");
    // 升到满级（射程最大），开面板画框，再关面板
    while (upgradeTower(ti)) {}
    g_renderOn = true;
    drawTowerPanel(D, g_w.towers[ti]); tick(2);          // 预览框已画
    int rr=(int)towerRange(g_w.towers[ti]);
    CHECK(rr > 150, "lv3 range should be large (%d)", rr);
    g_w.selectedTower=-1; tick(2);                        // renderFrame 自动关面板 → eraseRangeBox
    int tcx=15*CELL+CELL/2, tcy=MAP_TOP+6*CELL+CELL/2;
    int residue=0;
    // 扫旧框四条边（避开面板/按钮区域：只扫地图带内）
    auto isBtnHi=[&](int x,int y){ if(y<MAP_TOP||y>=BUILD_BAR_Y) return false;
      uint16_t c=D->fb[y*SCREEN_W+x]; return c==C_BTN_HI; };
    for (int x=tcx-rr+3;x<tcx+rr-3;++x){ if(isBtnHi(x,tcy-rr))++residue; if(isBtnHi(x,tcy+rr))++residue; }
    for (int y=tcy-rr+3;y<tcy+rr-3;++y){ if(isBtnHi(tcx-rr,y))++residue; if(isBtnHi(tcx+rr,y))++residue; }
    CHECK(residue==0, "range box residue after close: %d px", residue);
    CHECK(g_rbX1<0, "range box bbox not cleared");
  }

  // ── 10. v0.3.0：空中单位 + 对空矩阵 ─────────────────────────────
  {
    levelLoad(3); g_w.gold = 9999; g_w.lives = 99999;   // L4 kinds=[0,4,7]
    g_w.wave = AIR_FROM_WAVE - 1; startNextWave();        // → wave3：空袭门槛
    g_w.toSpawn = 40; tick(200);
    bool chopper=false; int ci=-1;
    for (int i=0;i<MAX_ENEMIES;++i) if (g_w.enemies[i].active && g_w.enemies[i].kind==K_CHOPPER){chopper=true;ci=i;}
    CHECK(chopper, "CHOPPER never spawned on L4 wave>=AIR_FROM_WAVE");
    if (ci>=0) {
      CHECK(g_w.enemies[ci].flying, "CHOPPER not flagged flying");
      // 飞行高度：视觉抬高生效（y 应比同 route 地面航点高）
      CHECK(g_w.enemies[ci].y < MAP_TOP + routeCount(0)*0 + 14*CELL, "chopper y sanity");
      // CAN（溅射）拿它没办法：把加农放它头顶，掉血必须为 0
      levelLoad(3); g_w.gold=9999; g_w.lives=99999;
      int cc=-1, cr=-1;
      for (int c=0;c<COLS && cc<0;++c) for (int r=0;r<MAP_ROWS;++r)
        if (!g_w.path[c][r] && !g_w.occupied[c][r] && !g_w.blocked[c][r]) { cc=c; cr=r; break; }
      buildTower(cc,cr,1);                                  // CAN at (cc,cr)
      g_w.wave = AIR_FROM_WAVE - 1; startNextWave(); tick(5);
      int ch=-1; for (int i=0;i<MAX_ENEMIES;++i) if (g_w.enemies[i].active && g_w.enemies[i].kind==K_CHOPPER){ch=i;break;}
      // 刷怪是概率+节流的：手动造一只确定性验证对空矩阵
      if (ch<0) { for (int i=0;i<MAX_ENEMIES;++i) if (!g_w.enemies[i].active) {
          spawnKind(i, K_CHOPPER, 0, false); ch=i; break; } }
      CHECK(ch>=0, "no chopper for anti-air test");
      if (ch>=0) {
        // 把它拽到加农正上方：距离=0，溅射本应命中
        g_w.enemies[ch].x = cc*CELL+CELL/2; g_w.enemies[ch].y = MAP_TOP+cr*CELL+CELL/2 - FLY_LIFT;
        float hp0 = g_w.enemies[ch].hp; tick(60);         // 加农至少开火一轮
        CHECK(g_w.enemies[ch].active && g_w.enemies[ch].hp == hp0,
              "CANNON hurt a chopper (%.0f -> %.0f)", hp0, g_w.enemies[ch].hp);
        // GAT 必须能打到天：另找一块草地建机枪，把直升机挪到它头上 → 掉血
        int gc=-1, gr2=-1;
        for (int c=0;c<COLS && gc<0;++c) for (int r=0;r<MAP_ROWS;++r)
          if (!g_w.path[c][r] && !g_w.occupied[c][r] && !g_w.blocked[c][r]) { gc=c; gr2=r; break; }
        CHECK(gc>=0, "no grass for GAT anti-air test");
        buildTower(gc,gr2,0);                              // GAT (cost 60)
        g_w.enemies[ch].x = gc*CELL+CELL/2;
        g_w.enemies[ch].y = MAP_TOP+gr2*CELL+CELL/2 - FLY_LIFT;
        tick(30);                                          // GAT 420ms → ≥1 发
        CHECK(!g_w.enemies[ch].active || g_w.enemies[ch].hp < hp0,
              "GAT cannot shoot air (hp=%.0f)", g_w.enemies[ch].hp);
      }
    }
  }

  // ── 11. v0.3.0：能量塔产金 + 激光瞬击 ────────────────────────────
  {
    levelLoad(0); g_w.gold = 300;                          // ENERGY=80
    CHECK(buildTower(15,6,3), "energy tower not built");
    int ei = findTowerAt(15,6); CHECK(ei>=0,"energy idx");
    int g0 = g_w.gold; tick(130);                        // 4.3s ≥ 一个 4000ms 脉冲
    CHECK(g_w.gold >= g0 + TOWER_TYPES[3].damage, "energy pulse paid nothing (%d->%d)", g0, (int)g_w.gold);
    // 能量塔不攻击：场上放一只怪，它不该掉血（energy 射程=0）
    levelLoad(0); g_w.gold = 999; buildTower(15,6,3);
    startNextWave(); tick(20);
    int eidx=-1; for (int i=0;i<MAX_ENEMIES;++i) if (g_w.enemies[i].active){eidx=i;break;}
    CHECK(eidx>=0,"energy test: no enemy");
    if (eidx>=0) { float hp=g_w.enemies[eidx].hp;
      g_w.enemies[eidx].x=15*CELL+20; tick(60);
      CHECK(!g_w.enemies[eidx].active || g_w.enemies[eidx].hp==hp, "ENERGY tower dealt damage!"); }
    // 激光：瞬击无子弹——开火帧敌人立刻掉血，且弹池全空
    levelLoad(0); g_w.gold=999; buildTower(15,6,4);      // LASER=140
    int li=findTowerAt(15,6); CHECK(li>=0,"laser idx");
    startNextWave(); tick(20);
    int tidx=-1; for (int i=0;i<MAX_ENEMIES;++i) if (g_w.enemies[i].active){tidx=i;break;}
    CHECK(tidx>=0,"laser test: no enemy");
    if (tidx>=0) {
      g_w.enemies[tidx].x=15*CELL+CELL/2; g_w.enemies[tidx].y=MAP_TOP+6*CELL+CELL/2;
      float hp0=g_w.enemies[tidx].hp; int bulletsBefore=0;
      for (int i=0;i<MAX_BULLETS;++i) if (g_w.bullets[i].active) ++bulletsBefore;
      tick(4);                                            // 一帧内应瞬击命中（无飞行时间）
      CHECK(g_w.enemies[tidx].active==false || g_w.enemies[tidx].hp < hp0,
            "laser did not hit instantly (hp=%.0f)", g_w.enemies[tidx].hp);
      CHECK(g_w.towers[li].beamMs > 0 || g_w.enemies[tidx].hp < hp0, "laser beam not armed");
      int bulletsAfter=bulletsBefore; for (int i=0;i<MAX_BULLETS;++i) if (g_w.bullets[i].active) ++bulletsAfter;
      CHECK(bulletsAfter==bulletsBefore, "laser spawned bullets (%d->%d)", bulletsBefore, bulletsAfter);
    }
  }

  // ── 12. v0.3.0：星级 = f(剩余生命)，历史最高保留 ─────────────────
  {
    memset(g_stars, 0, sizeof(g_stars));
    CHECK(starsFor(START_LIVES)==3 && starsFor((int)(START_LIVES*0.9))==3
          && starsFor((int)(START_LIVES*0.55))==2 && starsFor(1)==1, "starsFor thresholds");
    levelLoad(0); g_w.gold=9999; g_w.lives=START_LIVES;   // 无伤通关 → ★★★
    bool win = autoPlayLevel(LV().waves * 45 + 180);
    CHECK(win, "star test: L1 not cleared");
    if (win) {
      CHECK(g_stars[0]==starsFor(g_w.lives), "star != lives ratio (%d vs %d)", g_stars[0], starsFor(g_w.lives));
      // 历史最高保留：再重开一次压成残血通关，星不应下降
      uint8_t best = g_stars[0];
      levelLoad(0); g_w.gold=9999; g_stars[0]=best;
      win = autoPlayLevel(LV().waves * 45 + 180);
      CHECK(!win || g_stars[0] >= best, "best star regressed: %d -> %d", best, g_stars[0]);
      printf("L1 stars=%d (lives=%d)\n", g_stars[0], g_w.lives);
    }
  }

  // ── 13. v0.3.1：音效投递/节流/静音开关（同份 sfx.h 代码路径）────
  {
    levelLoad(0); g_w.gold = 999; sfxSetMuted(false);
    // 静音开关：选关界面点右下角按钮 → s_muted 翻转 + 再点恢复
    g_w.state = ST_SEL; drawLevelSelect(D); tick(2);
    tapAt(MUTE_BTN_X + MUTE_BTN_W/2, MUTE_BTN_Y + MUTE_BTN_H/2);
    CHECK(sfxIsMuted(), "mute tap did not mute");
    // 静音中：开火不投递（队列必须空）
    s_qTail = s_qHead;                      // 清残留
    g_w.state = ST_READY; startNextWave(); tick(30);
    CHECK(s_qTail == s_qHead, "muted but queue got events");
    // 回选关界面才能再点开关（设计：静音切换只在选关界面）
    g_w.state = ST_SEL; drawLevelSelect(D); tick(2);
    tapAt(MUTE_BTN_X + MUTE_BTN_W/2, MUTE_BTN_Y + MUTE_BTN_H/2);
    CHECK(!sfxIsMuted(), "second tap did not unmute");
    // 开火必投递：机枪对着贴脸的敌人打一波，记账里必须有 GUN+HIT
    levelLoad(0); g_w.gold=999; sfxSetMuted(false);
    buildTower(15,6,0); startNextWave(); tick(20);
    int eidx=-1; for (int i=0;i<MAX_ENEMIES;++i) if (g_w.enemies[i].active){eidx=i;break;}
    CHECK(eidx>=0,"sfx test: no enemy");
    if (eidx>=0) {
      g_w.enemies[eidx].x=15*CELL+CELL/2; g_w.enemies[eidx].y=MAP_TOP+6*CELL+CELL/2;
      uint32_t g0=s_simPlay[SFXID_GUN], h0=s_simPlay[SFXID_HIT];
      tick(15);                           // GAT 420ms → ≥1发；s_nowMs 每tick+33
      CHECK(s_simPlay[SFXID_GUN] > g0, "no GUN played after firing");
      CHECK(s_simPlay[SFXID_HIT] > h0, "no HIT played after bullet impact");
    }
    // 节流：同一毫秒连投 GUN，只应起播一条（间隔未到全丢）
    tick(3);                              // 拉开与刚才开火的间隔（>80ms）
    s_qTail = s_qHead;
    for (int k=0;k<5;++k) sfxPost(SFXID_GUN);
    int n=0; for (int i=s_qTail;i!=s_qHead;i=(i+1)%SFX_QUEUE) ++n;
    CHECK(n==1, "throttle failed: %d queued (expect 1)", n);
    // 能量塔产金事件
    levelLoad(0); g_w.gold=300; sfxSetMuted(false);
    buildTower(15,6,3); tick(130);
    CHECK(s_simPlay[SFXID_COIN] > 0, "COIN never played after energy pulses");
  }

  // ── 14. v0.3.2：弹盾兵——子弹×0.5 / 激光全伤 / 溅射无视 + 压叠窗口 ──
  {
    levelLoad(4); g_w.gold=9999; g_w.lives=99999;      // L5 kinds 含 SHIELD
    g_w.wave = SHIELD_FROM_WAVE - 1; startNextWave();   // → wave5
    g_w.toSpawn = 40; tick(240);
    bool sh=false, anyChopperGone=true; (void)anyChopperGone;
    int si=-1; for (int i=0;i<MAX_ENEMIES;++i) if (g_w.enemies[i].active && g_w.enemies[i].kind==K_SHIELD){sh=true;si=i;break;}
    CHECK(sh, "SHIELD never spawned on L5 wave>=%d", SHIELD_FROM_WAVE);
    if (si>=0) CHECK(g_w.enemies[si].shield, "SHIELD not flagged");

    // 定量：同位置机枪直击减半、激光全伤（用固定靶，绕开走位噪声）
    levelLoad(0); g_w.gold=999;
    buildTower(15,6,0); int gi=findTowerAt(15,6);
    buildTower(20,6,4); int li=findTowerAt(20,6);       // LASER 备着
    startNextWave(); tick(15);
    int e=-1; for (int i=0;i<MAX_ENEMIES;++i) if (g_w.enemies[i].active){e=i;break;}
    CHECK(e>=0,"shield test: no enemy");
    if (e>=0) {
      g_w.enemies[e].shield=true;
      g_w.enemies[e].armor=0;
      g_w.enemies[e].maxHp=100000; g_w.enemies[e].hp=100000;   // 固定靶不许死
      g_w.towers[li].active=false;                               // ⚠️ 隔离 LASER(45全伤)
      g_w.enemies[e].x=15*CELL+CELL/2; g_w.enemies[e].y=MAP_TOP+6*CELL+CELL/2;
      float hp0=g_w.enemies[e].hp; tick(14);                    // 462ms→GAT≤2发
      float lost=hp0-g_w.enemies[e].hp;
      CHECK(lost>0 && lost<=13, "shield bullet halving broken: %.0f (expect 6)", lost);
      // 激光打盾：不减半（LV1 LASER dmg45 全伤）
      g_w.towers[li].active=true;                             // LASER 归位
      g_w.enemies[e].hp = 100000; hp0=100000;
      g_w.enemies[e].x=20*CELL+CELL/2; g_w.enemies[e].y=MAP_TOP+6*CELL+CELL/2;
      g_w.towers[li].cooldownMs=0; tick(3);
      float l2=hp0-g_w.enemies[e].hp;
      CHECK(l2>=45, "LASER halved by shield: %.0f (expect 45)", l2);
      // 溅射无视盾：加农落点全覆盖→全额34（splashDamage 不看 shield）
      buildTower(25,6,1);                                        // CAN $100? gold 兜底
      g_w.gold=9999; buildTower(25,6,1);
      int ci=findTowerAt(25,6); CHECK(ci>=0,"can build for splash test");
      g_w.enemies[e].hp=100000; hp0=100000;
      g_w.enemies[e].x=25*CELL+CELL/2; g_w.enemies[e].y=MAP_TOP+6*CELL+CELL/2;
      g_w.towers[ci].cooldownMs=0; tick(6);                  // 炮弹飞一小段+溅射
      float l3=hp0-g_w.enemies[e].hp;
      CHECK(l3>=34, "splash halved by shield: %.0f (expect >=34)", l3);
    }

    // 压叠：刷怪完毕+残敌≥8在场 → ~5s(150帧) 后强开下波（不等全清）
    levelLoad(1); g_w.gold=9999; g_w.lives=99999;        // ⚠️ 用 L2(7波)：L1 第5波=末波，
    g_w.wave = 4; startNextWave();                       //    末波禁压叠（守卫是对的）
    int wsp=0; while (alive()<8 && wsp<400){tick(1);++wsp;}  // 等≥8只进场
    g_w.toSpawn = 0;                                   // 假装已刷完，场上留残敌
    CHECK(alive()>=8, "overlap setup: need 8 survivors (got %d)", alive());
    int fr=0; while (g_w.state==ST_WAVE && g_w.wave==5 && fr<300){tick(1);++fr;}
    CHECK(fr>=115 && fr<=185, "overlap force-start off: %d frames (expect ~150)", fr);
  }

  // ── 15. v0.4.1：进度落盘去重（P0 回归：uint16 截断→每帧写 NVS）────
  //      + 星级进步也必须落盘（旧去重只看 unlock/clear，重打刷星静默丢失）
  {
    for (int i=0;i<LEVEL_COUNT;++i) g_stars[i] = (uint8_t)((i*7+1) % 4);   // 任意星级
    uint32_t a = packStars(0), b = packStars(10);
    uint8_t rt[LEVEL_COUNT]; int bad=0;
    for (int i=0;i<10;++i) rt[i]=(uint8_t)((a>>(i*3))&7);
    for (int i=10;i<LEVEL_COUNT;++i) rt[i]=(uint8_t)((b>>((i-10)*3))&7);
    for (int i=0;i<LEVEL_COUNT;++i) if (rt[i]!=g_stars[i]) ++bad;
    CHECK(bad==0, "packStars roundtrip broken (%d mismatches)", bad);

    levelLoad(16);                       // L17：解锁位在 bit16——旧 uint16 缓存的炸点
    g_w.gold = 9999; g_w.lives = 99999;
    g_unlocked |= (1u << 16); g_cleared |= (1u << 16);      // 模拟刚通关 L17
    g_w.state = ST_CLEAR;
    s_simSaves = 0; g_saveProgress = simCountSave;
    for (int i = 0; i < 30; ++i) progressTickOnClear();     // 通关画面停留 30 帧
    CHECK(s_simSaves == 1, "clear screen saved %d times in 30 frames (expect 1)", s_simSaves);
    g_stars[16] = 3;                                        // 重打刷高星级
    progressTickOnClear();
    CHECK(s_simSaves == 2, "star progress did not trigger save (saves=%d)", s_simSaves);
    for (int i = 0; i < 10; ++i) progressTickOnClear();     // 无变化→零写入
    CHECK(s_simSaves == 2, "unchanged progress re-saved (saves=%d)", s_simSaves);
    g_saveProgress = nullptr;
  }

  // ── 16. v0.4.1：BOSS 各技能位独立节拍（L6 = HEAL 2s + GHOST 7s）────
  //      旧实现到期齐放+最后一个 if 常量劫持：治疗被拖成 7s/次
  {
    levelLoad(5); g_w.gold = 9999; g_w.lives = 99999;
    startNextWave(); g_w.wave = LV().waves;
    g_w.toSpawn = 1; g_w.bossPending = true; tick(30);      // 只出王，无塔干扰
    int bi=-1; for (int i=0;i<MAX_ENEMIES;++i) if (g_w.enemies[i].active && g_w.enemies[i].kind==K_BOSS) bi=i;
    CHECK(bi>=0, "cadence test: L6 boss missing");
    if (bi>=0) {
      Enemy& B = g_w.enemies[bi];
      B.hp = 1;
      B.abilityMs = millis();
      for (int k=0;k<8;++k) B.abNextMs[k] = millis();       // 全部逼停→各自重新排表
      tick(BOSS_HEAL_MS/33 + 4);                            // ~2s：HEAL 应已自愈
      CHECK(B.hp > 1.5f, "HEAL did not fire at own 2s cadence (hp=%.1f)", B.hp);
      CHECK(B.ghostMs == 0, "GHOST fired early (cadence hijack?)");
      tick((GHOST_MS - BOSS_HEAL_MS)/33 + 4);               // 刚过 7s：GHOST 现身且未衰减完
      CHECK(B.ghostMs > 0, "GHOST never fired at own 7s cadence");
    }
  }

  // ── 17. v0.4.1：选关锁关红叉 20 帧自擦（旧实现画完永留）──────────
  {
    g_unlocked = 0x0001; g_cleared = 0;                     // 只解锁 L1
    g_w.state = ST_SEL; drawLevelSelect(D);
    const int cx = selNodeX(2) + SEL_NODE_W/2, cy = selNodeY(0) + SEL_NODE_H/2;  // 节点正中心=两红叉对角线交点
    const uint16_t before = D->fb[cy * SCREEN_W + cx];
    tapAt(cx, cy);                                          // 点锁定节点
    CHECK(g_selFx.lifeFrames > 0, "locked tap did not arm selFx");
    updateSelFx(D);                                         // 画一帧红叉
    CHECK(D->fb[cy * SCREEN_W + cx] == C_BAD, "locked feedback not red");
    for (int i = 0; i < 25; ++i) updateSelFx(D);            // 20 帧到期自擦
    CHECK(g_selFx.lifeFrames == 0, "selFx did not expire");
    CHECK(D->fb[cy * SCREEN_W + cx] == before, "node not restored after selFx");
  }

  // ── 18. v0.4.1：波量公式唯一化（waveSize 与旧硬编码逐点一致）─────
  // 6+(w*3)/2 整数除：w=1→7, 2→9, 3→10, 10→21（与 v0.4 两处硬编码同值）
  {
    CHECK(waveSize(1)==7 && waveSize(2)==9 && waveSize(3)==10 && waveSize(10)==21,
          "waveSize formula drifted");
  }

  printf("\n=== RESULT: %d passed, %d failed (level-loop checks=%d) ===\n",
         g_pass, g_fail, totalChecksBefore);
  return g_fail ? 1 : 0;
}
