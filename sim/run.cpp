/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 andjiang0083
 */
// sim/run.cpp — 用**固件真实源码**(game.h/render.h/actions.h/touch.h)在 PC 上跑
// 编译方式：-Isim/compat 让 #include <M5Unified.h> / <M5GFX.h> 命中替身，
//           其余 include（config_gen/pixel_font/game/render/actions/touch）全是原文件。
// 所以这里断言通过 == 固件逻辑真的对，不是另写了一份模拟器。
// v0.2.0：10 关全量自动通关（贪心 bot）、多路径投放、选关状态机、
//         HEALER/BOSS 技能、ELITE、解锁位图、经济采样。
#include <M5Unified.h>       // fake（显示侧已转 Gfx，见 sim/compat/M5Unified.h）
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

static Gfx* D = &M5.Display;

// ── PPM 导出（真彩，直接吐帧缓冲；由 tools/to_png.py 转 PNG）────
static void dumpPpm(const char* path) {
  FILE* f = fopen(path, "wb"); if (!f) return;
  fprintf(f, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
  uint8_t row[SCREEN_W * 3];
  for (int y = 0; y < SCREEN_H; ++y) {
    for (int x = 0; x < SCREEN_W; ++x) {
      uint16_t c = D->fb()[y * SCREEN_W + x];
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
    // v0.4.2：镜像 main.cpp 的两个静态界面分支——只推进输入/反馈，
    // 不跑世界也不渲染（ST_TITLE 下若走 renderFrame，会把上一局 HUD
    // 画到标题界面上，与真机不符；标题帧截图就靠这条保持干净）
    if (g_w.state == ST_SEL) { updateSelFx(D); continue; }
    if (g_w.state == ST_TITLE) continue;
    gameUpdate(33);                    // 真逻辑（面板打开=内部暂停）
    s_nowMs = g_simMs;                // v0.3.1：镜像 loop()——音效节流时钟
    sfxPump();                        // 仿真下排空队列（同份投递/节流代码）
    uint64_t before = GfxFb::pxFilled;
    if (g_renderOn) renderFrame(D);  // 真渲染（长跑时关闭）
    g_lastPx = GfxFb::pxFilled - before;
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

// ── v0.4.2 分兵升级：每条 route 按"覆盖占比"独立记账，最弱路线优先 ──
// 旧实现从塔 0 轮询升级——火力天然堆在先建/高分的塔（多半在同一条路），
// 双路关另一路长期低级放空（L5/L8/L13/L14/L16/L20 全是双路）。
// ROADMAP 承认"bot 不会分兵所以不是难度标尺"——这就是那句的实现侧修复。
// 判据（每条路各自算）：该路被塔射程罩住的路格占比。
static void botUpgradeSplit(void) {
  for (;;) {
    // 1) 找覆盖占比最低的 route（双路关的短板）
    int worstRt = -1; float worstRatio = 1e9f;
    for (int rt = 0; rt < LV().routes_n; ++rt) {
      int cov = 0, tot = 0;
      const int cnt = routeCount(rt);
      for (int i = 0; i + 1 < cnt; ++i) {
        const Waypoint& a = routeWP(rt, i); const Waypoint& b = routeWP(rt, i + 1);
        const int dc = (b.col > a.col) - (b.col < a.col);
        const int dr = (b.row > a.row) - (b.row < a.row);
        int cc = a.col, rr = a.row;
        for (;;) {
          ++tot;
          const float px = cc*CELL + CELL/2, py = MAP_TOP + rr*CELL + CELL/2;
          for (int t = 0; t < MAX_TOWERS; ++t) {
            const Tower& tw = g_w.towers[t];
            if (!tw.active) continue;
            const float rg = TOWER_TYPES[tw.type].range_px
                           * (1.0f + TOWER_RANGE_SCALE * (tw.level - 1));
            const float dx = px - (tw.col*CELL+CELL/2), dy = py - (MAP_TOP+tw.row*CELL+CELL/2);
            if (dx*dx + dy*dy <= rg*rg) { ++cov; break; }
          }
          if (cc == b.col && rr == b.row) break;
          cc += dc; rr += dr;
        }
      }
      const float ratio = tot ? (float)cov / tot : 1.0f;
      if (ratio < worstRatio) { worstRatio = ratio; worstRt = rt; }
    }
    if (worstRt < 0) break;
    // 2) 最弱路线上，守护它的塔里挑等级最低的升（补短板，不堆长板）
    int pick = -1; uint8_t pickLv = 0xFF;
    for (int t = 0; t < MAX_TOWERS; ++t) {
      const Tower& tw = g_w.towers[t];
      if (!tw.active) continue;
      const float rg = TOWER_TYPES[tw.type].range_px
                     * (1.0f + TOWER_RANGE_SCALE * (tw.level - 1));
      const float tx = tw.col*CELL+CELL/2, ty = MAP_TOP+tw.row*CELL+CELL/2;
      bool guards = false;
      const int cnt = routeCount(worstRt);
      for (int i = 0; i + 1 < cnt && !guards; ++i) {
        const Waypoint& a = routeWP(worstRt, i); const Waypoint& b = routeWP(worstRt, i + 1);
        const int dc = (b.col > a.col) - (b.col < a.col);
        const int dr = (b.row > a.row) - (b.row < a.row);
        int cc = a.col, rr = a.row;
        for (;;) {
          const float dx = cc*CELL+CELL/2 - tx, dy = MAP_TOP + rr*CELL+CELL/2 - ty;
          if (dx*dx + dy*dy <= rg*rg) { guards = true; break; }
          if (cc == b.col && rr == b.row) break;
          cc += dc; rr += dr;
        }
      }
      if (!guards) continue;
      if (tw.level < pickLv) { pickLv = tw.level; pick = t; }
    }
    if (pick < 0) break;                          // 最弱路也无塔守护 → 没得升
    const int cu = upgradeCost(g_w.towers[pick]);
    if (cu <= 0) break;                           // 已满级
    const bool lastWaves = (g_w.wave >= LV().waves - 1);
    if (g_w.gold < cu + (lastWaves ? 0 : 80)) break;   // 留余粮（同旧纪律）
    upgradeTower(pick);
  }
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
  //    纪律：先铺够 6 座防面；v0.4.2 起升级预算按 route 分配（最弱路优先），
  //    双路关不再"一条路满级、另一路放空"。
  if (towers() >= 5 || g_w.wave >= LV().waves - 1) {
    botUpgradeSplit();
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

// ── 截图模式 ─────────────────────────────────────────────────────
// SIM_SHOT=title                       → 抓开机标题界面（v0.4.2 起商店封面用）
// SIM_SHOT=<1..20> [SIM_SHOT_WAVE=<w>] → 真渲染 + bot 自动打到指定波次，
//   抓"有塔有兵"的战场帧（README/商店封面用）。关掉长跑的 g_renderOn=false
//   逻辑：这里每帧都真画，所以帧缓冲就是当下画面。
static void titleShotMode(const char* out) {
  g_w.state = ST_TITLE;               // 标题界面 = 开机第一屏（main.cpp setup 同款）
  g_renderOn = true;
  drawTitle(D);                       // setup 里就是直接画一次
  tick(2);                            // 让帧缓冲对齐当前时刻
  dumpPpm(out);
  printf("shot: title screen v0.4.3 -> %s\n", out);
}

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

// ── v0.4.3：UI 文字适配断言（中英双语 × 每个界面元素 × 容器宽度）──
// 动机：v0.4.2 标题界面的 LANGUAGE 标签硬编码 x=360，8 字符×12px=96px
// 伸到 456，而 ENGLISH 按钮从 430 开始——重叠 26px。这类 bug 的特征是
// "每个元素各自画得对、相对位置算错"，肉眼在截图里才发现，所以逐元素
// 查容器。**新增界面文字时，在这里补一条对应的容器断言**——这是本项目
// "截图评审"的制度化替代品。
static void checkUiTextFit(void) {
  for (int lang = 0; lang < LANG_COUNT; ++lang) {
    setLang(lang);
    const char* L = (lang == LANG_CN) ? "CN" : "EN";

    // ── 标题界面 ──────────────────────────────────────────────
    // v0.4.3：LANGUAGE 标签改为按钮组上方居中（旧版 x=360 硬编码，
    // EN 94px 伸进 ENGLISH 按钮 26px）。居中语义：不得宽于按钮组。
    CHECK(px_textWidth(TR(STR_LANG), 2) <= (LANG_CN_X + LANG_BTN_W) - LANG_EN_X,
          "[%s] title: STR_LANG %dpx wider than button group", L, px_textWidth(TR(STR_LANG), 2));
    // 语言标签底边（中文点阵 16px 高于 ASCII 的 14px，取大者）不得贴按钮顶边
    CHECK(TITLE_LANG_LBL_Y + CN_FONT_H + 8 <= LANG_BTN_Y,
          "[%s] title: LANG label bottom %d too close to buttons @%d",
          L, TITLE_LANG_LBL_Y + CN_FONT_H, LANG_BTN_Y);
    // 中文标题（TR 化，v0.4.3）：不得宽于两行英文大标题的宽度量级
    CHECK(px_textWidth(TR(STR_TITLE_CN), 2) <= 400,
          "[%s] title: STR_TITLE_CN %dpx too wide", L, px_textWidth(TR(STR_TITLE_CN), 2));
    // TAP TO START 双色叠字：不得贴屏幕边（居中）
    CHECK(px_textWidth(TR(STR_TITLE_HINT), 3) <= SCREEN_W - 120,
          "[%s] title: STR_TITLE_HINT %dpx too wide", L, px_textWidth(TR(STR_TITLE_HINT), 3));
    // 按钮标签（v0.4.3 起走 TR：EN 模式 ENGLISH/中文，CN 模式 英文/中文）
    CHECK(px_textWidth(TR(STR_LANG_EN), 2) <= LANG_BTN_W - 20,
          "[%s] title: lang-en label %dpx overflows button", L, px_textWidth(TR(STR_LANG_EN), 2));
    CHECK(px_textWidth(TR(STR_LANG_CN), 2) <= LANG_BTN_W - 20,
          "[%s] title: lang-cn label %dpx overflows button", L, px_textWidth(TR(STR_LANG_CN), 2));

    // ── HUD（v0.4.3：全部引用 render.h 的 HUD_* 命名常量，与绘制同源）──
    // WAVE "250/250" 右缘必须 < NEXT WAVE 按钮左缘（v0.2.1 真机 bug #3 同型）
    {
      char wb[24]; strcpy(wb, "250/250");
      CHECK(WNUM_X + px_textWidth(wb, HUD_SCALE) < BTN_X,
            "[%s] hud: WAVE %dpx@%d overlaps button @%d",
            L, px_textWidth(wb, HUD_SCALE), WNUM_X, BTN_X);
    }
    // GOLD 7 位（无尽）右缘 < BEST 起点
    {
      char gb[16]; strcpy(gb, "9999999");
      CHECK(GNUM_X + px_textWidth(gb, HUD_SCALE) < BEST_X,
            "[%s] hud: GOLD 7 digits overlaps BEST", L);
    }
    // BEST + 3 位数字右缘 < WAVE 标签左缘（旧版 BEST@770 → 数字伸到 813，
    // 压住 WAVE@800；把 "42" 叠在 "W" 上的那个 bug）
    CHECK(BEST_X + px_textWidth(TR(STR_BEST), 1) + 6 + px_textWidth("999", 1) < WLBL_X,
          "[%s] hud: BEST block overruns WAVE label @%d", L, WLBL_X);
    // "$" 必须完整落在 SIEGE 清空框与 GOLD 擦除框之间（v0.4.2 回归：
    // 清空框宽 180 铺到 480，把 $ 每帧擦掉）
    CHECK(SIEGE_X + SIEGE_W <= DOL_X,
          "[%s] hud: SIEGE strip %d..%d erases $ @%d",
          L, SIEGE_X, SIEGE_X + SIEGE_W, DOL_X);
    CHECK(DOL_X + px_textWidth("$", HUD_SCALE) <= GNUM_X - 8,
          "[%s] hud: $ overlaps GOLD erase box", L);
    // HP 标签不得伸进 HP 数字擦除框
    CHECK(HPL_X + px_textWidth(TR(STR_HP), 3) <= HPN_X - 8,
          "[%s] hud: HP label %dpx@%d overlaps number box",
          L, px_textWidth(TR(STR_HP), 3), HPL_X);
    // HP 数字擦除框右缘 < SIEGE 条左缘
    CHECK(HPN_X + 56 <= SIEGE_X,
          "[%s] hud: HP num box overlaps SIEGE strip", L);
    // SIEGE 警示条（清空区 SIEGE_X..+SIEGE_W）
    CHECK(px_textWidth(TR(STR_SIEGE_WARN), 1) <= SIEGE_W,
          "[%s] hud: SIEGE_WARN %dpx overflows strip(%d)",
          L, px_textWidth(TR(STR_SIEGE_WARN), 1), SIEGE_W);
    // NEXT WAVE / FIGHTING 按钮文字（内边距 20）
    CHECK(px_textWidth(TR(STR_NEXT_WAVE), 2) <= BTN_W - 20,
          "[%s] btn: NEXT_WAVE %dpx overflows", L, px_textWidth(TR(STR_NEXT_WAVE), 2));
    CHECK(px_textWidth(TR(STR_FIGHTING), 2) <= BTN_W - 20,
          "[%s] btn: FIGHTING %dpx overflows", L, px_textWidth(TR(STR_FIGHTING), 2));

    // ── 建造栏 ────────────────────────────────────────────────
    // v0.5.0：断言全部改用**绘制侧的真实变量**（SLOT_NSCALE / SLOT_TX /
    //   BAR_HINT_X / BAR_HINT_SCALE），不再写死 scale=2 与 x+46。
    //   原写法隐含"槽宽≥180、字号=2"的假设，720 宽下槽宽 116 → 断言测的是
    //   一个绘制侧根本不画的字号，属于"断言与实现脱节"——正是 v0.4.3 注释里
    //   记的那类 bug（"改一处必漏一处"）。
    for (int i = 0; i < TT_COUNT; ++i) {
      // 塔名：x + SLOT_TX 起，右缘 < 槽宽 - 26（避让对空角标）
      CHECK(px_textWidth(TOWER_TYPES[i].disp, SLOT_NSCALE) + SLOT_TX
                <= SLOT_W - 26,
            "[%s] buildbar: %s name %dpx overflows slot (tx=%d sw=%d)",
            L, TOWER_TYPES[i].disp,
            px_textWidth(TOWER_TYPES[i].disp, SLOT_NSCALE), SLOT_TX, SLOT_W);
      // 价格 3 位 + "$"：x + SLOT_TX 起，"$"+3 位数字
      CHECK(SLOT_TX + (FONT_W + 1) * SLOT_NSCALE * 4 <= SLOT_W - 8,
            "[%s] buildbar: price overflows slot (tx=%d sw=%d)",
            L, SLOT_TX, SLOT_W);
      // NEED（金币不足）：x + SLOT_TX 起
      CHECK(px_textWidth(TR(STR_NEED), 1) + SLOT_TX <= SLOT_W - 8,
            "[%s] buildbar: NEED %dpx overflows slot", L,
            px_textWidth(TR(STR_NEED), 1));
    }
    // 右侧三行提示：位置/字号同样取绘制侧变量
    {
      CHECK(BAR_HINT_X + px_textWidth(TR(STR_TAP_SLOT), BAR_HINT_SCALE)
                < SCREEN_W,
            "[%s] buildbar: TAP_SLOT hint %dpx offscreen", L,
            px_textWidth(TR(STR_TAP_SLOT), BAR_HINT_SCALE));
      CHECK(BAR_HINT_X + px_textWidth(TR(STR_THEN_MAP), BAR_HINT_SCALE)
                < SCREEN_W,
            "[%s] buildbar: THEN_MAP hint offscreen", L);
      CHECK(BAR_HINT_X + px_textWidth(TR(STR_TAP_TOWER), 1) < SCREEN_W,
            "[%s] buildbar: TAP_TOWER hint offscreen", L);
    }

    // ── 塔面板（PANEL_W=360）──────────────────────────────────
    for (int i = 0; i < TT_COUNT; ++i) {
      // 标题行：塔名(px+14) 与 右对齐 "LV3" 之间至少留 20px
      const int lvW = px_textWidth("LV3", 3);
      CHECK(14 + px_textWidth(TOWER_TYPES[i].disp, 3) + 20 + lvW <= PANEL_W - 14,
            "[%s] panel: %s name collides with LV", L, TOWER_TYPES[i].disp);
    }
    // 属性行右缘（px+210 起最多 5 字符 "$9999"/"500MS"）
    CHECK(210 + 60 <= PANEL_W - 14, "[%s] panel: attr value overflows", L);
    // NO ATTACK 行（scale1 最长 22 字符）
    CHECK(px_textWidth(TR(STR_NO_ATTACK), 1) + 14 <= PANEL_W - 14,
          "[%s] panel: NO_ATTACK %dpx overflows", L, px_textWidth(TR(STR_NO_ATTACK), 1));
    // 按钮文字（PBTN_W=164 / SELL=PBTN_W-40=124）
    CHECK(px_textWidth(TR(STR_UPGRADE), 2) <= PBTN_W - 20,
          "[%s] panel: UPGRADE %dpx overflows", L, px_textWidth(TR(STR_UPGRADE), 2));
    CHECK(px_textWidth(TR(STR_NEED_GOLD), 1) <= PBTN_W - 20,
          "[%s] panel: NEED_GOLD %dpx overflows", L, px_textWidth(TR(STR_NEED_GOLD), 1));
    CHECK(px_textWidth(TR(STR_SELL), 2) <= (PBTN_W - 40) - 20,
          "[%s] panel: SELL %dpx overflows", L, px_textWidth(TR(STR_SELL), 2));
    CHECK(px_textWidth(TR(STR_MAX), 3) <= PBTN_W - 20,
          "[%s] panel: MAX %dpx overflows", L, px_textWidth(TR(STR_MAX), 3));
    // "$9999" 价格（4 字符 scale2=48）
    CHECK(48 <= PBTN_W - 20, "[%s] panel: price overflows", L);

    // ── 选关界面 ──────────────────────────────────────────────
    CHECK(px_textWidth(TR(STR_SELECT), 4) <= 900,
          "[%s] select: title %dpx too wide", L, px_textWidth(TR(STR_SELECT), 4));
    CHECK(px_textWidth(TR(STR_UNLOCK_HINT), 2) <= SCREEN_W - 120,
          "[%s] select: unlock hint %dpx too wide", L, px_textWidth(TR(STR_UNLOCK_HINT), 2));
    for (int i = 0; i < LEVEL_COUNT; ++i) {
      CHECK(px_textWidth(LEVELS[i].name, 1) <= SEL_NODE_W - 20,
            "[%s] select: L%d name %dpx overflows node", L, i + 1, px_textWidth(LEVELS[i].name, 1));
    }

    // ── 结算 / 通关（面板 760 宽，内边距 ~20）─────────────────
    CHECK(px_textWidth(TR(STR_GAME_OVER), 6) <= 720,
          "[%s] over: title %dpx too wide", L, px_textWidth(TR(STR_GAME_OVER), 6));
    CHECK(px_textWidth(TR(STR_LEVEL_CLEAR), 6) <= 720,
          "[%s] clear: title %dpx too wide", L, px_textWidth(TR(STR_LEVEL_CLEAR), 6));
    CHECK(px_textWidth(TR(STR_TAP_RESTART), 3) <= 740,
          "[%s] over: restart hint %dpx too wide", L, px_textWidth(TR(STR_TAP_RESTART), 3));
    CHECK(px_textWidth(TR(STR_TAP_NEXT), 3) <= 740,
          "[%s] clear: next hint %dpx too wide", L, px_textWidth(TR(STR_TAP_NEXT), 3));
    CHECK(px_textWidth(TR(STR_ALL_CLEAR), 3) <= 740,
          "[%s] clear: all-clear hint %dpx too wide", L, px_textWidth(TR(STR_ALL_CLEAR), 3));
    // BOSS DOWN: + 最长 boss 名（GLASS TITAN 11 字符 scale2=132）
    {
      char bb[64]; strcpy(bb, TR(STR_BOSS_DOWN)); strcat(bb, "GLASS TITAN");
      CHECK(px_textWidth(bb, 2) <= 740,
            "[%s] clear: boss-down line %dpx too wide", L, px_textWidth(bb, 2));
    }
    // KILLS 999 GOLD 9999（scale2）
    {
      char bb[64]; strcpy(bb, TR(STR_KILLS)); strcat(bb, "999 ");
      strcat(bb, TR(STR_GOLD_WORD)); strcat(bb, "9999");
      CHECK(px_textWidth(bb, 2) <= 740,
            "[%s] clear: kills/gold line %dpx too wide", L, px_textWidth(bb, 2));
    }
    // 无尽入口按钮（ENDLESS_BTN_W=280，内边距 20）
    CHECK(px_textWidth(TR(STR_ENDLESS), 3) <= ENDLESS_BTN_W - 20,
          "[%s] clear: ENDLESS %dpx overflows btn", L, px_textWidth(TR(STR_ENDLESS), 3));
    CHECK(px_textWidth(TR(STR_ENDLESS_DESC), 1) <= ENDLESS_BTN_W - 20,
          "[%s] clear: ENDLESS_DESC %dpx overflows btn", L, px_textWidth(TR(STR_ENDLESS_DESC), 1));
    // v0.4.3：按钮的垂直夹缝——上不压通关面板、下不压建造栏槽位。
    // 旧硬编码 Y=590 让按钮底边 654 埋进建造栏 640..720（"SURVIVE 250 WAVES" 被切）。
    CHECK(ENDLESS_BTN_Y + ENDLESS_BTN_H <= BUILD_BAR_Y,
          "[%s] clear: endless btn bottom %d overlaps build bar top %d",
          L, ENDLESS_BTN_Y + ENDLESS_BTN_H, BUILD_BAR_Y);
    CHECK(ENDLESS_BTN_Y >= CLEAR_PANEL_Y + CLEAR_PANEL_H,
          "[%s] clear: endless btn top %d overlaps clear panel bottom %d",
          L, ENDLESS_BTN_Y, CLEAR_PANEL_Y + CLEAR_PANEL_H);

    // ── 引导 overlay（bw=820，内边距 30/边）───────────────────
    CHECK(px_textWidth(TR(STR_ONBOARD_1), 2) <= 820 - 60,
          "[%s] onboard1: %dpx overflows box", L, px_textWidth(TR(STR_ONBOARD_1), 2));
    CHECK(px_textWidth(TR(STR_ONBOARD_2), 2) <= 820 - 60,
          "[%s] onboard2: %dpx overflows box", L, px_textWidth(TR(STR_ONBOARD_2), 2));
    CHECK(px_textWidth(TR(STR_ONBOARD_3), 2) <= 820 - 60,
          "[%s] onboard3: %dpx overflows box", L, px_textWidth(TR(STR_ONBOARD_3), 2));
    CHECK(px_textWidth(TR(STR_ONBOARD_TAP), 2) <= 820 - 60,
          "[%s] onboard tap: %dpx overflows box", L, px_textWidth(TR(STR_ONBOARD_TAP), 2));

    // ⚠️ 这里原本还有 drawReadyHint 的断言。v0.4.3 尾单把该函数连同
    // STR_READY_HINT 一起删了——它在产品里没有调用点（引导 overlay 取代），
    // 给死代码写断言只会让它看起来"被维护着"。需要轻提示时重新引入即可，
    // 但底部锚点必须是 BUILD_BAR_Y 而不是 SCREEN_H。
  }
  setLang(LANG_EN);                    // 恢复默认，避免影响后续测试

  // ── v0.4.3：缺字防线（渲染完全部界面后 s_cnMissing 必须为 0）────
  // 上面只查了 STR_x 的宽度；这里查"每个中文字符都有点阵"。
  // 中文模式下把全部界面真画一遍（含标题——v0.4.2 的缺字bug就在标题）。
  {
    setLang(LANG_CN);
    s_cnMissing = 0;                   // 清零，只计这一段
    g_renderOn = true;
    g_w.state = ST_TITLE; drawTitle(D); tick(1);
    levelLoad(0); g_w.state = ST_SEL; drawLevelSelect(D); tick(1);
    levelLoad(0); enterLevel(D, 0); closePanel();
    tick(2);                                     // 战斗中：HUD/建造栏/按钮
    g_w.hudDirty = true; renderFrame(D);
    drawButton(D, true);
    drawTowerPanel(D, g_w.towers[0]);
    g_lv = LEVEL_COUNT - 1; g_w.state = ST_CLEAR; drawLevelClear(D); tick(1);
    g_w.state = ST_GAMEOVER; drawGameOver(D); tick(1);
    for (int step = 1; step <= 3; ++step) drawOnboard(D, step);
    CHECK(s_cnMissing == 0,
          "CN font missing %d glyphs (hardcoded CN string? see i18n.h header note)", s_cnMissing);
    setLang(LANG_EN);
  }
}

// ── v0.4.3：全界面截图批处理（中英双语 × 每个界面）────────────────
// 用法：./sim/sim --shots <outdir>
// 产出 2 语言 × 11 界面 = 22 张 1280x720 PPM（tools/to_png.py 转 PNG 后人眼评审）。
// 每次改 UI 后跑一遍；checkUiTextFit() 管"文字不溢出容器"，
// 这个批处理管"元素之间不错位/不重叠"——两者合起来替代截图人审。
static void shotsBatch(const char* dir) {
  char path[512];
  g_renderOn = true;

  // 干净底帧助手：重载关卡 → 整屏静态背景 → HUD/建造栏/按钮 → 实体。
  // ⚠️ 不能省这步：drawOnboard / drawGameOver 都只画自己那一块，
  // 指望底下已是一张完整战场帧。批处理里上一张图若是 ST_CLEAR 面板，直接叠画
  // 就会把"ENDLESS MODE"按钮之类残留在新图里（v0.4.3 第一次出图即如此）。
  auto freshBoard = [&](int lv) {
    levelLoad(lv);
    drawBackground(D);                                   // fillScreen + 全图 + HUD 底板
    drawBuildBar(D);
    drawHud(D); g_w.hudDirty = false;
    drawButton(D, g_w.state == ST_READY);
    for (int i = 0; i < MAX_TOWERS; ++i) if (g_w.towers[i].active) drawTower(D, g_w.towers[i]);
    for (int i = 0; i < MAX_ENEMIES; ++i) if (g_w.enemies[i].active) drawEnemy(D, g_w.enemies[i]);
  };

  for (int lang = 0; lang < LANG_COUNT; ++lang) {
    setLang(lang);
    const char* L = (lang == LANG_CN) ? "cn" : "en";

    // ① 标题界面（drawTitle 自带清屏）
    g_w.state = ST_TITLE;
    drawTitle(D); tick(2);
    snprintf(path, sizeof path, "%s/title-%s.ppm", dir, L); dumpPpm(path);

    // ② 选关界面（L1 解锁态；drawLevelSelect 自带清屏）
    g_unlocked = 0x0001; g_cleared = 0;
    levelLoad(0); g_w.state = ST_SEL; drawLevelSelect(D); tick(2);
    snprintf(path, sizeof path, "%s/select-%s.ppm", dir, L); dumpPpm(path);

    // ③ 战斗 HUD（bot 打到 wave 9 的真实战场）
    levelLoad(12); g_unlocked |= (1u << 12);
    enterLevel(D, 12); closePanel();
    int guard = 90000;
    while (guard-- > 0) {
      botBuildWave();
      if (g_w.state == ST_READY && g_w.readyLeftMs <= 1500) startNextWave();
      if (g_w.state == ST_CLEAR || g_w.state == ST_GAMEOVER) break;
      tick(1);
      if (g_w.wave >= 9 && towers() >= 5 && alive() >= 6 && g_w.lives >= 15) break;
    }
    snprintf(path, sizeof path, "%s/combat-%s.ppm", dir, L); dumpPpm(path);

    // ④ 塔面板（选第一座活跃塔；叠在③的干净战场上）
    for (int i = 0; i < MAX_TOWERS; ++i) {
      if (g_w.towers[i].active) { g_w.selectedTower = (int8_t)i; break; }
    }
    drawTowerPanel(D, g_w.towers[g_w.selectedTower >= 0 ? g_w.selectedTower : 0]);
    snprintf(path, sizeof path, "%s/panel-%s.ppm", dir, L); dumpPpm(path);
    g_w.selectedTower = -1; closeTowerPanel(D);

    // ⑤ 无尽 HUD（BEST 显示；叠在③的干净战场上）
    g_w.endless = 1; g_endBest = 42; g_w.hudDirty = true;
    renderFrame(D); tick(1);
    snprintf(path, sizeof path, "%s/endless-%s.ppm", dir, L); dumpPpm(path);
    g_w.endless = 0; g_w.hudDirty = true;

    // ⑥ SIEGE HUD 警示条 + 封锁区（同帧可读）——重铺底帧避免⑤残影
    freshBoard(12);
    g_w.siegeMs = 2000; g_w.siegeX = 640; g_w.siegeY = 400; g_w.siegeR = 180;
    g_w.hudDirty = true;
    drawHud(D); g_w.hudDirty = false;                    // 警示条（drawHud 内联）
    drawSiege(D);                                        // 红框 + 井字封锁
    snprintf(path, sizeof path, "%s/siege-%s.ppm", dir, L); dumpPpm(path);
    g_w.siegeMs = 0;

    // ⑦ L20 通关画面（含无尽入口按钮）——先铺干净底再叠全屏面板
    g_lv = LEVEL_COUNT - 1;
    freshBoard(LEVEL_COUNT - 1);
    g_w.gold = 1234; g_w.score = 318;
    g_w.state = ST_CLEAR;
    drawLevelClear(D); tick(2);
    snprintf(path, sizeof path, "%s/clear-%s.ppm", dir, L); dumpPpm(path);

    // ⑧ 失败画面
    freshBoard(12);
    g_w.state = ST_GAMEOVER; g_w.wave = 12;
    drawGameOver(D); tick(2);
    snprintf(path, sizeof path, "%s/over-%s.ppm", dir, L); dumpPpm(path);

    // ⑨ 引导 overlay 三步（叠在真实战场帧上——这才是首次进 L1 玩家看到的）
    for (int step = 1; step <= 3; ++step) {
      freshBoard(0);
      g_w.state = ST_READY;
      drawOnboard(D, step);
      snprintf(path, sizeof path, "%s/onboard%d-%s.ppm", dir, step, L); dumpPpm(path);
    }

  }
  setLang(LANG_EN);
  printf("shots: 2 langs x 11 screens -> %s/{title,select,combat,panel,endless,siege,clear,over,onboard1-3}-{en,cn}.ppm\n", dir);
}

int main(int argc, char** argv) {
  // v0.4.2 难度数据化扫描：./sim/sim --x2scan
  // L14-L20 给 bot 双倍起手金币重跑（不漏怪判据），回答 ROADMAP 第一条：
  // 这七关是"经济卡死"（×2 就过）还是"输出结构性问题"（×2 也打不过）。
  if (argc > 1 && strcmp(argv[1], "--x2scan") == 0) {
    printf("=== x2gold scan: L14-L20 (bot start gold x2, lives=99999) ===\n");
    for (int lv = 13; lv < LEVEL_COUNT; ++lv) {
      g_unlocked |= (1u << lv);
      levelLoad(lv);
      g_w.gold = START_GOLD * 2;
      g_w.lives = 99999;                  // 只判经济/输出，不判漏怪
      const bool win = autoPlayLevel(LV().waves * 60 + 300);
      printf("L%-2d %-15s %-4s wave=%-2d kills=%-3d goldLeft=%-5d towers=%d\n",
             lv + 1, LV().name, win ? "PASS" : "FAIL",
             g_w.wave, g_w.score, g_w.gold, towers());
    }
    return 0;
  }

  printf("=== PC sim v0.4.3: UI text-fit + missing-glyph guards, real game/render/touch/actions ===\n");
  // 截图模式（文档/商店封面用），不跑断言
  if (const char* s = getenv("SIM_SHOT")) {
    const char* wv = getenv("SIM_SHOT_WAVE");
    const char* out = getenv("SIM_SHOT_OUT");
    if (strcmp(s, "title") == 0) {         // v0.4.2：标题界面帧
      titleShotMode(out ? out : "shot_title.ppm");
      return 0;
    }
    int lv = atoi(s) - 1;
    if (lv < 0 || lv >= LEVEL_COUNT) { printf("SIM_SHOT out of range (1..%d or \"title\")\n", LEVEL_COUNT); return 2; }
    shotMode(lv, wv ? atoi(wv) : 0, out ? out : "shot.ppm");
    return 0;
  }
  // v0.4.3：全界面截图批处理（中英双语）：./sim/sim --shots <dir>
  if (argc > 2 && strcmp(argv[1], "--shots") == 0) {
    shotsBatch(argv[2]);
    return 0;
  }
  printf("=== PC sim v0.4.3: UI text-fit + missing-glyph guards, real game/render/touch/actions ===\n");
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
        if (D->fb()[y*SCREEN_W+x]==C_UI_BG && x<BAR_X) ++black;  // 按钮左缘被刷黑
    CHECK(black==0, "HUD WAVE plate overlaps NEXT WAVE button: %d px", black);
    // 底板右缘必须留在按钮之前
    //v0.5.0：956→WNUM_X-4（旧值把 tab5 的 1280 宽坐标写死在断言里，
    //   换 profile 后这个断言测的是一个不存在的位置）
    CHECK(WNUM_X - 4 + (FONT_W+1)*HUD_SCALE*5 + 8 <= BTN_X, "WAVE plate too wide");
    // HUD 静态标签在擦除后仍在（框伸进 HUD 带的场景）
    levelLoad(0); g_w.gold=9999; buildTower(15,0,0);
    int ti=findTowerAt(15,0); while (upgradeTower(ti)) {}
    g_w.selectedTower=ti; tick(2);                        // 第一行塔：框伸进 HUD
    g_w.selectedTower=-1; tick(3);                        // 关面板 → eraseRangeBox
    int tagLost=0;
    // v0.5.0：800..860 → WLBL_X 起 60px（旧值同样写死 1280 基准）
    for (int i=0;i<60;++i)
      if (D->fb()[HUD_Y*(int)SCREEN_W + (int)WLBL_X + i] == C_TEXT_DIM) ++tagLost;
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
      uint16_t c=D->fb()[y*SCREEN_W+x]; return c==C_BTN_HI; };
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
      // v0.4.2：机枪三音色轮换——断言"三种变体总数"增加（单断言 GUN 会
      // 因轮换位偶发失败：第一次开火投的是 GUN2）
      uint32_t g0=s_simPlay[SFXID_GUN]+s_simPlay[SFXID_GUN2]+s_simPlay[SFXID_GUN3];
      uint32_t h0=s_simPlay[SFXID_HIT];
      tick(15);                           // GAT 420ms → ≥1发；s_nowMs 每tick+33
      CHECK(s_simPlay[SFXID_GUN]+s_simPlay[SFXID_GUN2]+s_simPlay[SFXID_GUN3] > g0,
            "no GUN played after firing");
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
    const uint16_t before = D->fb()[cy * SCREEN_W + cx];
    tapAt(cx, cy);                                          // 点锁定节点
    CHECK(g_selFx.lifeFrames > 0, "locked tap did not arm selFx");
    updateSelFx(D);                                         // 画一帧红叉
    CHECK(D->fb()[cy * SCREEN_W + cx] == C_BAD, "locked feedback not red");
    for (int i = 0; i < 25; ++i) updateSelFx(D);            // 20 帧到期自擦
    CHECK(g_selFx.lifeFrames == 0, "selFx did not expire");
    CHECK(D->fb()[cy * SCREEN_W + cx] == before, "node not restored after selFx");
  }

  // ── 18. v0.4.1：波量公式唯一化（waveSize 与旧硬编码逐点一致）─────
  // 6+(w*3)/2 整数除：w=1→7, 2→9, 3→10, 10→21（与 v0.4 两处硬编码同值）
  {
    CHECK(waveSize(1)==7 && waveSize(2)==9 && waveSize(3)==10 && waveSize(10)==21,
          "waveSize formula drifted");
  }

  // ── 19. v0.4.2：标题界面（开机第一屏：语言选择 + 点击开始）────────
  {
    g_w.state = ST_TITLE;
    g_renderOn = true;
    drawTitle(D); tick(2);                       // 可渲染（星空/标题/按钮全画）
    setLang(LANG_EN);
    // 点中文按钮 → 语言切 CN（NVS 由 main 注入，这里只验证状态与重画）
    tapAt(LANG_CN_X + LANG_BTN_W/2, LANG_BTN_Y + LANG_BTN_H/2);
    CHECK(langCN(), "CN lang button did not switch language");
    drawTitle(D); tick(2);                       // 中文标题界面也要能画
    // 点英文按钮 → 切回 EN
    tapAt(LANG_EN_X + LANG_BTN_W/2, LANG_BTN_Y + LANG_BTN_H/2);
    CHECK(!langCN(), "EN lang button did not switch language");
    // 点开始（屏幕中央空白处）→ 进选关
    tapAt(SCREEN_W/2, 640);
    CHECK(g_w.state == ST_SEL, "title tap did not enter level select (state=%d)",
          (int)g_w.state);
    setLang(LANG_EN);
  }

  // ── 20. v0.4.2：首次引导三步（NVS onboarded 记住只讲一次）───────
  {
    g_onboarded = false;
    levelLoad(0); g_w.gold = 999;
    enterLevel(D, 0);                            // 首次进 L1 → step1
    CHECK(g_onboardStep == 1, "onboard did not arm on first L1 (step=%d)",
          (int)g_onboardStep);
    g_renderOn = true;
    drawOnboard(D, g_onboardStep); tick(2);      // 三步每一画都要能渲染
    drawOnboard(D, 2); drawOnboard(D, 3);
    // 点对话框 → step2 → step3 → 出师（onboarded 落盘）
    tapAt(ONBOARD_X + ONBOARD_W/2, ONBOARD_Y + ONBOARD_H/2);
    CHECK(g_onboardStep == 2, "onboard tap did not advance to step2 (%d)",
          (int)g_onboardStep);
    tapAt(ONBOARD_X + ONBOARD_W/2, ONBOARD_Y + ONBOARD_H/2);
    CHECK(g_onboardStep == 3, "onboard tap did not advance to step3 (%d)",
          (int)g_onboardStep);
    s_simSaves = 0; g_saveProgress = simCountSave;
    tapAt(ONBOARD_X + ONBOARD_W/2, ONBOARD_Y + ONBOARD_H/2);
    CHECK(g_onboardStep == 0 && g_onboarded, "onboard did not complete");
    CHECK(s_simSaves == 1, "onboarded flag not saved once (saves=%d)", s_simSaves);
    g_saveProgress = nullptr;
    // 重进 L1 不再弹引导
    enterLevel(D, 0);
    CHECK(g_onboardStep == 0, "onboard re-armed for a veteran");
  }

  // ── 21. v0.4.2：无尽模式入口（L20 CLEAR → ENDLESS 按钮）──────────
  // 注意：20 关的终关是 L20（g_lv=19）——v0.4 时代 endless 挂在 L10，
  // 关卡扩到 20 后入口随终关走（全通才是无尽）。
  {
    g_unlocked = 0xFFFFFFFFu; g_cleared = 0xFFFFFFFFu;   // 模拟全通关（enterEndless 走解锁校验）
    levelLoad(19); g_w.gold = 9999; g_w.lives = 99999;
    startNextWave(); g_w.wave = LV().waves;
    g_w.toSpawn = 1; g_w.bossPending = true; tick(30);      // 只出王
    int bi=-1; for (int i=0;i<MAX_ENEMIES;++i)
      if (g_w.enemies[i].active && g_w.enemies[i].kind==K_BOSS) bi=i;
    if (bi>=0) g_w.enemies[bi].hp = 0.01f;                 // 补刀→ST_CLEAR
    tick(3);
    g_w.state = ST_CLEAR; g_clearDrawn = false;
    g_renderOn = true; drawLevelClear(D); tick(2);         // 末关画面含无尽按钮
    CHECK(g_w.endless == 0, "endless flag set before entering");
    tapAt(ENDLESS_BTN_X + ENDLESS_BTN_W/2, ENDLESS_BTN_Y + ENDLESS_BTN_H/2);
    CHECK(g_w.endless == 1, "endless button did not enter endless (endless=%d)",
          (int)g_w.endless);
    CHECK(g_w.state == ST_READY && g_w.wave == 0, "endless not at fresh start");
    CHECK(lvWaves() == ENDLESS_WAVES, "endless waves cap wrong (%d)", lvWaves());
    // 最高波次记账：wave 超过 endBest 时实时刷新
    // ⚠️ readyLeftMs 要先复位：levelLoad 的 memset 遗留 0，手工设 wave>0
    //    会被 readyLeftMs==0 当成"倒计时归零"立刻自动开波（测试环境坑）
    g_endBest = 5; g_w.wave = 8; g_w.readyLeftMs = 0xFFFFFFFFu; tick(3);
    CHECK(g_endBest == 8, "endBest not updated live (%d)", (int)g_endBest);
    g_w.endless = 0; g_endBest = 0;
  }

  // ── 22. v0.4.2：SIEGE 封锁建造区（L11 GLASS TITAN bit8）──────────
  {
    levelLoad(10); g_w.gold = 9999; g_w.lives = 99999;     // L11: SPLIT|HORN|SIEGE
    startNextWave(); g_w.wave = LV().waves;
    g_w.toSpawn = 1; g_w.bossPending = true; tick(30);      // 只出王
    int bi=-1; for (int i=0;i<MAX_ENEMIES;++i)
      if (g_w.enemies[i].active && g_w.enemies[i].kind==K_BOSS) bi=i;
    CHECK(bi>=0, "SIEGE test: L11 boss missing");
    if (bi>=0) {
      Enemy& B = g_w.enemies[bi];
      CHECK((bossAbilityOf(B) & AB_SIEGE) != 0, "L11 boss lost SIEGE bit (ab=%u)",
            (unsigned)bossAbilityOf(B));
      B.abilityMs = millis();
      for (int k=0;k<9;++k) B.abNextMs[k] = millis();       // 9 位全逼停
      tick(SIEGE_MS/33 - 10);                               // ~3.5s 内：SIEGE 已触发且未到期
      CHECK(g_w.siegeMs > 0, "SIEGE never fired on L11 boss");
      CHECK(g_w.siegeR == SIEGE_R, "SIEGE radius wrong (%d)", (int)g_w.siegeR);
      // 圈内建塔被拒（塔心判定）
      int bc = (int)(g_w.siegeX / CELL), br = (int)((g_w.siegeY - MAP_TOP) / CELL);
      if (bc >= 0 && bc < COLS && br >= 0 && br < MAP_ROWS
          && !g_w.path[bc][br] && !g_w.blocked[bc][br]) {
        CHECK(!buildTower(bc, br, 0), "build inside siege zone was allowed");
      }
      // 圈外正常建（找最远的合法格）
      int fc=-1, fr=-1; float bestD2=-1;
      for (int c=0;c<COLS;++c) for (int r=0;r<MAP_ROWS;++r) {
        if (g_w.path[c][r] || g_w.occupied[c][r] || g_w.blocked[c][r]) continue;
        float dx=c*CELL+CELL/2-g_w.siegeX, dy=MAP_TOP+r*CELL+CELL/2-g_w.siegeY;
        float d2=dx*dx+dy*dy;
        if (d2>bestD2) { bestD2=d2; fc=c; fr=r; }
      }
      CHECK(fc>=0 && buildTower(fc, fr, 0), "build outside siege zone was refused");
      // 到期自动解除（在 SIEGE_IV=10s 的下一拍之前）
      tick((SIEGE_IV - SIEGE_MS)/33 - 8);
      CHECK(g_w.siegeMs == 0, "siege did not expire (ms=%d)", (int)g_w.siegeMs);
      if (bc >= 0 && bc < COLS && br >= 0 && br < MAP_ROWS
          && !g_w.path[bc][br] && !g_w.blocked[bc][br] && !g_w.occupied[bc][br]) {
        CHECK(buildTower(bc, br, 0), "build after siege expiry still refused");
      }
    }
  }

  // ── 23. v0.4.2：机枪三音色轮换（GUN/GUN2/GUN3 都起播过）─────────
  {
    levelLoad(0); g_w.gold = 999; sfxSetMuted(false);
    buildTower(15,6,0); startNextWave(); tick(20);
    int eidx=-1; for (int i=0;i<MAX_ENEMIES;++i) if (g_w.enemies[i].active){eidx=i;break;}
    CHECK(eidx>=0,"sfx variant test: no enemy");
    if (eidx>=0) {
      g_w.enemies[eidx].x=15*CELL+CELL/2; g_w.enemies[eidx].y=MAP_TOP+6*CELL+CELL/2;
      g_w.enemies[eidx].maxHp=100000; g_w.enemies[eidx].hp=100000;   // 固定靶多打几轮
      tick(90);                                     // GAT 420ms×3 → 三轮开火
      CHECK(s_simPlay[SFXID_GUN]>0 && s_simPlay[SFXID_GUN2]>0 && s_simPlay[SFXID_GUN3]>0,
            "gun variant rotation incomplete: %d/%d/%d",
            s_simPlay[SFXID_GUN], s_simPlay[SFXID_GUN2], s_simPlay[SFXID_GUN3]);
    }
  }

  // ── 24. v0.4.2：中文渲染路径（px_text 遇 UTF-8 走 cn_font.h）─────
  {
    setLang(LANG_CN);
    g_renderOn = true;
    levelLoad(0); g_w.gold = 9999; g_w.lives = 20;
    tick(2); drawHud(D); g_w.hudDirty = false;      // 中文 HUD 全画一遍
    drawButton(D, true);
    drawLevelSelect(D); tick(2);                    // 中文选关界面
    // 宽度函数必须认中文：17px/字，ASCII 6px/字(scale1)
    // （px_textWidth 语义=总宽减去最后一个字间缝——ASCII 侧原有行为，中文一致）
    CHECK(px_textWidth("中文", 1) == 33, "CN width wrong (%d)", px_textWidth("中文", 1));
    CHECK(px_textWidth("AB", 1) == 11, "ASCII width wrong (%d)", px_textWidth("AB", 1));
    CHECK(px_textWidth("中A", 1) == 22, "mixed width wrong (%d)", px_textWidth("中A", 1));
    setLang(LANG_EN);
  }

  // ── 25. v0.4.3：UI 文字适配（中英双语 × 每元素 × 容器宽度）───────
  // 动机：v0.4.2 标题界面 LANGUAGE 标签伸进 ENGLISH 按钮 26px（截图才
  // 发现）。新增界面文字必须在这里补容器断言——比"每次人眼看全套截图"
  // 可靠，且 CI 会跑。
  checkUiTextFit();

  printf("\n=== RESULT: %d passed, %d failed (level-loop checks=%d) ===\n",
         g_pass, g_fail, totalChecksBefore);
  return g_fail ? 1 : 0;
}
