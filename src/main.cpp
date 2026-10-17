/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 andjiang0083
 */
// ============================================================================
// PIXEL TOWER DEFENSE — M5Stack Tab5 (ESP32-P4)  v0.4.4
// ----------------------------------------------------------------------------
// ⚠️ 为什么不是 TFT_eSPI（重要）：
//   Tab5 的屏是 MIPI DSI（ILI9881C / ST7123），不是 SPI/QSPI。ESP32-P4 的
//   DSI 控制器只支持 Video Mode：帧缓冲常驻 PSRAM、由 DMA 连续扫描输出，
//   不存在"把矩形写进屏幕 GRAM"的通路 —— TFT_eSPI 只实现 SPI 时序，
//   因此在 Tab5 上无法工作（上游同样结论：Bodmer/TFT_eSPI issue #3719）。
//   Arduino 生态下 Tab5 唯一可用的显示层是 M5GFX（内部走 ESP-IDF esp_lcd/DSI）。
//   本项目仍然只用 fillRect / drawPixel 手画像素方块，不加载任何图片/位图，
//   连数字字母都是自带的 5x7 点阵（src/pixel_font.h，由 tools/gen_font.py 生成）。
//   ⚠️ M5GFX/M5Unified 的内置字体是位图资源，本项目刻意不使用它们。
//
// 编译配置（PlatformIO / pioarduino —— 不要用 Arduino IDE + TFT_eSPI）：
//   1) brew install platformio
//   2) board = esp32-p4-evboard + -DARDUINO_M5STACK_TAB5（见 platformio.ini），
//      让 M5GFX 自动识别 Tab5 的面板（ST7123/ILI9881C）与触控（GT911/ST7123）
//   3) 必须开 PSRAM：board_build.psram = enable / oct @200MHz —— 帧缓冲所在地，
//      不满足时 M5GFX 直接报 "M5Tab5 need PSRAM SPEED 200MHz"
//   4) pio run -e tab5 && pio run -e tab5 -t upload && pio device monitor
//   5) Tab5 无用户物理按键，全部交互走触摸屏（与本作操作方式一致）
// ----------------------------------------------------------------------------
// 模块划分：touch.h 采样 / actions.h 规则落地 / game.h 逻辑更新 / render.h 渲染
// PC 仿真（sim/）编译的是同一份 game/render/actions/touch，已跑通 25 项断言。
// ============================================================================

#include <M5Unified.h>
#include <esp_task_wdt.h>
#include <Preferences.h>       // v0.2.0：关卡解锁进度持久化到 NVS

#include "config_gen.h"     // 生成的网格/路径/数值（真源 tools/layout.py）
#include "pixel_text.h"     // 5x7 点阵文字 + v0.4.2 中文 16x16（逐位 fillRect）
#include "i18n.h"           // v0.4.2：界面文本双语表（标题界面选择语言）
#include "game.h"           // Enemy/Tower/Bullet/Fx + gameUpdate()
#include "touch.h"          // readTap()：非阻塞 tap-on-release
#include "render.h"         // drawBackground / renderFrame / drawTitle
#include "actions.h"        // handleTap / updateBuildFx / updateOverlays（内含 progress.h）
#include "gfx_m5gfx.h"      // v0.5.2：M5GFX → Gfx 适配（Tab5 显示后端，见该文件头注）

static Preferences s_prefs;

// v0.5.2：显示设备。render.h / actions.h / pixel_text.h 自 v0.5.0 起只认 Gfx*，
//   而 M5.Display 是 M5GFX —— 由 GfxM5 转发（Tab5 的帧缓冲归 M5GFX/esp_lcd 管）。
//   （v0.5.0 引入这条抽象时漏了 Tab5 这条后端：固件一直停在 `&M5.Display`，
//    编不过。仿真走 GfxFb、Claw4 走 GfxClaw4，Tab5 缺的就是本行。）
static GfxM5 g_disp(&M5.Display);

// v0.4.3：引导 overlay 的重画去重（见 loop 里的说明）
static int8_t s_lastOnboardStep = 0;

// ============================================================================
// 性能探针（v0.5.3）——默认全关，发行固件零成本（连一行都不编）
// ----------------------------------------------------------------------------
// 为什么要有它：v0.5.2 修 L10 雪崩时，我用"每帧 fillRect 次数"当负载代理量，
//   那是**PC 仿真**上的数。判断"这台机器到底能不能扛住 N 只同屏"，只有真机
//   负载态的分段毫秒数算数（见 m5stack-tab5 / esp32-p4-display-bandwidth 的
//   测量纪律：只认负载态仪表、一次只改一个变量、先量后改）。
// 打开方式（不改 platformio.ini，用环境变量注入）：
//   PLATFORMIO_BUILD_FLAGS="-DPTD_PERF_PROBE=1 -DPTD_PERF_SPAWN=48" pio run -e tab5 -t upload
// PTD_PERF_SPAWN=N>0 时启用**确定性合成负载**：直接进 L10、把场上敌人钉在 N 只、
//   血量钉在中后期档、生命锁死 ⇒ 不需要任何人碰屏幕就能长时间稳定重载。
// ⚠️ 测完必须用**默认开关重新构建并刷回**（探针档不许过夜）。
// ⚠️ 这两个宏与探针代码只在 Tab5 入口文件里 ⇒ 仿真与 Claw4 一行都不受影响。
// ============================================================================
#ifndef PTD_PERF_PROBE
#define PTD_PERF_PROBE 0
#endif
#ifndef PTD_BG_TILE_SELFCHECK
#define PTD_BG_TILE_SELFCHECK 0   // 1 = 开机跑一次块拷贝字节序自检（只在探针档用）
#endif

#if PTD_PERF_PROBE && PTD_BG_TILE_SELFCHECK
// ── 块拷贝字节序自检（已知输入 → 读回）──────────────────────────────
//  仿真（GfxFb）有自己的 16bpp 约定，**验不了 M5GFX 的转换** ⇒ 这条只能在设备上跑：
//  用 4 个已知颜色拼 8x8 图案 → ① fillRect 画一遍（参考：这条路颜色一直是对的）
//  ② 各候选 pushImage 形态各画一遍 → readPixel 逐点读回比对。
//  ⚠️ 参考自己也自证：参考读回与送进去的图案不一致 ⇒ readPixel 不可用，整轮作废。
// ── 双语标题上屏自检（已知输入 → 读回）────────────────────────────
//  仿真只能证明"渲染层把中文写进了帧缓冲"，证不了**面板上真的出现了笔画**
//  （字库/后端/驱动任一层出问题都可能只画出一片空框）。真机按同法自检：
//  · 中文模式：副标题带（"像素塔防"）必须有墨迹，且**列簇数 = 4**（= 4 个字形）
//  · 两种模式：当前语言的按钮必须有金色高亮，另一种必须没有
// 颜色比对：两边都是**调色板里的 RGB565**（config_gen.h: C_GOLD=0xFE69 这类），
//   逐场比、容差 2/3/2 位。
//   ⚠️ 这里踩过两次坑，都记在这：① 用"通道窗口"猜颜色 ⇒ 把金当成灰，全 0；
//      ② 把调色板值当 RGB888 再换算 ⇒ 期望值算成 0x07ED，而屏上读回就是 0xFE69。
//      两次都是**探针自己错**、看起来却像"产品坏了"。教训：确定底色先用
//      "画已知色 → 读回原始值"自证，别在探针里做任何颜色换算。
static bool nearColor(uint16_t c, uint16_t want) {
  const int r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
  const int er = (want >> 11) & 0x1F, eg = (want >> 5) & 0x3F, eb = want & 0x1F;
  return (r - er <= 2 && er - r <= 2) && (g - eg <= 3 && eg - g <= 3)
      && (b - eb <= 2 && eb - b <= 2);
}
static int inkClusters(int x0, int x1, int y0, int y1, int* inkOut) {
  int ink = 0, clusters = 0; bool prev = false;
  for (int x = x0; x < x1; ++x) {
    bool any = false;
    for (int y = y0; y < y1; ++y) {
      if (nearColor(g_disp.readPixel(x, y), C_GOLD)) { any = true; ++ink; }
    }
    if (any && !prev) ++clusters;
    prev = any;
  }
  *inkOut = ink;
  return clusters;
}
static void titleInkCheck(void) {
  // ── 探针自证①：先在同一块区域直接用 fillRect 画金色，读回必须 >0 ──
  //    这一条为 0 ⇒ "读不到"是读数/区域问题，后面的 0 一律不作数（别把探针的错
  //    当成产品的错 —— 反过来也一样）。
  g_disp.fillRect(LANG_EN_X, LANG_BTN_Y, LANG_BTN_W, LANG_BTN_H, C_GOLD);
  int ctl = 0;
  for (int y = LANG_BTN_Y; y < LANG_BTN_Y + LANG_BTN_H; ++y)
    for (int x = LANG_EN_X; x < LANG_EN_X + LANG_BTN_W; ++x)
      if (nearColor(g_disp.readPixel(x, y), C_GOLD)) ++ctl;
  Serial.printf("TITLEINK ctl_gold=%d (0 => 该区域读回不可用，下面的 0 不作数)\n", ctl);
  const uint8_t keep = g_lang;
  for (int li = 0; li < PTD_LANG_COUNT; ++li) {
    setLang((uint8_t)li); drawTitle(&g_disp);
    int subInk = 0;
    const int subCl = inkClusters(SCREEN_W / 2 - 90, SCREEN_W / 2 + 90,
                                  TITLE_CN_SUB_Y, TITLE_CN_SUB_Y + CN_FONT_H, &subInk);
    auto goldIn = [&](int x0) {
      int n = 0;
      for (int y = LANG_BTN_Y; y < LANG_BTN_Y + LANG_BTN_H; ++y)
        for (int x = x0; x < x0 + LANG_BTN_W; ++x) {
          if (nearColor(g_disp.readPixel(x, y), C_GOLD)) ++n;
        }
      return n;
    };
    const int gEn = goldIn(LANG_EN_X), gCn = goldIn(LANG_CN_X);
    const uint16_t s0 = g_disp.readPixel(LANG_EN_X, LANG_BTN_Y);            // 按钮边框左上角
    const uint16_t s1 = g_disp.readPixel(LANG_EN_X + 1, LANG_BTN_Y + 1);    // 边框内一圈
    const uint16_t s2 = g_disp.readPixel(LANG_EN_X + LANG_BTN_W / 2,
                                         LANG_BTN_Y + 20);                  // 文字中心
    const uint16_t s3 = g_disp.readPixel(6, 6);                             // 左上星空
    Serial.printf("TITLEINK lang=%s sub_ink=%d clusters=%d gold_en=%d gold_cn=%d"
                  " raw=%04X,%04X,%04X,%04X gold565=%04X\n",
                  li == LANG_CN ? "CN" : "EN", subInk, subCl, gEn, gCn,
                  s0, s1, s2, s3,
                  (unsigned)C_GOLD);   // 调色板本就是 565，直接印，不换算
  }
  setLang(keep); drawTitle(&g_disp);
}

// ── 产品真实路径自检：走 eraseDirty 的块拷贝，逐像素对比"直画" ──────────
//  为什么不能只测 pushImage 机制（BLITCHK 干的活）：首版把 prepareTile 调在
//  TileCanvas（Gfx 基类空实现）上，机制测试全绿、真机依旧紫色。这条走的是
//  游戏擦格那条路（eraseDirty → cellTile → 显示后端 pushImage），选一个"路"格
//  —— 症状正好出现在敌人走过的路上。
static void bgBlitPathCheck(void) {
  int pc = -1, pr = -1;
  for (int r = 0; r < MAP_ROWS && pc < 0; ++r)
    for (int c = 0; c < COLS; ++c) if (g_w.path[c][r]) { pc = c; pr = r; break; }
  if (pc < 0) { Serial.println("BGPATH 无路格，跳过"); return; }
  const int X = pc * CELL, Y = MAP_TOP + pr * CELL;
  eraseDirty(&g_disp);                     // 先建缓存（无脏格则缓存未建）
  g_w.dirty[pc][pr] = true;
  eraseDirty(&g_disp);                     // ← 产品路径：一次 pushImage 块拷贝
  static uint16_t got[CELL * CELL], ref[CELL * CELL];
  for (int y = 0; y < CELL; ++y)
    for (int x = 0; x < CELL; ++x) got[y * CELL + x] = g_disp.readPixel(X + x, Y + y);
  drawCellBaseAt(&g_disp, X, Y, pc, pr);   // 参考：同一格子的直画
  for (int y = 0; y < CELL; ++y)
    for (int x = 0; x < CELL; ++x) ref[y * CELL + x] = g_disp.readPixel(X + x, Y + y);
  int bad = 0, fx = -1, fy = -1; uint16_t e = 0, gv = 0;
  for (int i = 0; i < CELL * CELL; ++i)
    if (got[i] != ref[i]) { if (!bad) { fx = i % CELL; fy = i / CELL; e = ref[i]; gv = got[i]; } ++bad; }
  Serial.printf("BGPATH 路格(%d,%d) 块拷贝vs直画 mism=%d/%d first=(%d,%d) exp=%04X got=%04X\n",
                pc, pr, bad, CELL * CELL, fx, fy, e, gv);
  g_w.dirty[pc][pr] = true;                // 让正常帧修回来
}

static void blitSelfCheck(void) {
  const int N = 8;
  static uint16_t pat[64];
  const uint16_t cols[4] = { 0xF800 /*红*/, 0x07E0 /*绿*/, 0x001F /*蓝*/, TH().pathA };
  for (int j = 0; j < N; ++j)
    for (int i = 0; i < N; ++i) pat[j * N + i] = cols[((i / 2) + (j / 2)) & 3];
  const int Y0 = 8;
  for (int j = 0; j < N; ++j)
    for (int i = 0; i < N; ++i) g_disp.fillRect(i, Y0 + j, 1, 1, pat[j * N + i]);
  int refBad = 0;
  for (int j = 0; j < N; ++j)
    for (int i = 0; i < N; ++i)
      if (g_disp.readPixel(i, Y0 + j) != pat[j * N + i]) ++refBad;
  Serial.printf("BLITCHK ref self-test: %d/%d mismatch (readPixel %s)\n",
                refBad, N * N, refBad ? "UNUSABLE -> check void" : "ok");
  if (refBad) return;
  const int vs[3] = { 0, 1, 9 };                 // 9 = 生产路径（prepareTile + pushImage）
  for (int k = 0; k < 3; ++k) {
    const int v = vs[k];
    const int X = 16 + k * 12;
    g_disp.pushImageVariant(v, X, Y0, N, N, pat);
    int bad = 0, fx = -1, fy = -1; uint16_t e = 0, got = 0;
    for (int j = 0; j < N; ++j)
      for (int i = 0; i < N; ++i) {
        const uint16_t r = g_disp.readPixel(X + i, Y0 + j);
        if (r != pat[j * N + i]) { if (!bad) { fx = i; fy = j; e = pat[j * N + i]; got = r; } ++bad; }
      }
    Serial.printf("BLITCHK v%d %-18s mism=%2d first=(%d,%d) exp=%04X got=%04X\n",
                  v, v == 0 ? "plain(2Byte)" : v == 1 ? "selfswap+plain" : "PRODUCTION",
                  bad, fx, fy, e, got);
  }
}
#endif

#ifndef PTD_PERF_SPAWN
#define PTD_PERF_SPAWN 0        // 0 = 不造负载（只读探针）
#endif
#ifndef PTD_PERF_TOWERS
#define PTD_PERF_TOWERS 0       // >0 = 预置 N 座塔（真正的重载：开火+命中音效洪峰）
#endif
#ifndef PTD_PERF_TLVL
#define PTD_PERF_TLVL 1         // 预置塔升到几级（3 = 射速最快 ⇒ 子弹/音效最多）
#endif
#ifndef PTD_PERF_BOSS
#define PTD_PERF_BOSS 0         // 钉住的场里放几只 BOSS（72px 大件，擦脏格最贵）
#endif

#if PTD_PERF_PROBE
static void perfPlaceTowers(int want);      // 定义在 perfLoadTick 之后（要 buildTower 之类）

// 确定性合成负载：把场上敌人数钉在 PTD_PERF_SPAWN，直到测量结束
static void perfLoadTick(void) {
  static bool s_loaded = false;
  if (!s_loaded) {
    s_loaded = true;
    levelLoad(9);                     // L10 TERMINAL：兵种池最全（空袭/弹盾/奶妈/重甲）
    g_w.endless = 1;                  // 波数上限 250：避开"末波不压叠"那条守卫
    g_w.wave    = 11;                 // 中后期血量档（HP_GROWTH^11 ≈ 2.85×）
    startNextWave();                  // → ST_WAVE
    g_w.toSpawn = 100000;             // 永远"还在刷怪"⇒ 不触发波次完成/压叠逻辑
    g_w.lives   = 999999;             // 不许 game over 打断测量
#if PTD_BG_TILE_SELFCHECK
    blitSelfCheck();
    titleInkCheck();
    bgBlitPathCheck();
#endif
    Serial.printf("PERF build=PROBE spawn=%d towers=%d maxEnemies=%d capTowers=%d\n",
                  (int)PTD_PERF_SPAWN, (int)PTD_PERF_TOWERS, (int)MAX_ENEMIES, (int)MAX_TOWERS);
#if PTD_PERF_TOWERS > 0
    perfPlaceTowers(PTD_PERF_TOWERS);
#endif
  }
  int alive = 0;
  for (int i = 0; i < MAX_ENEMIES; ++i) if (g_w.enemies[i].active) ++alive;
#if PTD_PERF_BOSS > 0
  // 最贵的一种场：大件 BOSS（72px 精灵 + 擦脏格 3×3 格）
  int bosses = 0;
  for (int i = 0; i < MAX_ENEMIES; ++i) if (g_w.enemies[i].active && g_w.enemies[i].kind == 5) ++bosses;
  for (int i = bosses; i < PTD_PERF_BOSS; ++i) {
    int slot = -1;
    for (int k = 0; k < MAX_ENEMIES; ++k) if (!g_w.enemies[k].active) { slot = k; break; }
    if (slot < 0) break;
    spawnKind(slot, 5, (uint8_t)(i % LV().routes_n), false);   // 5 = K_BOSS
  }
#endif
  for (int i = alive; i < PTD_PERF_SPAWN; ++i) if (!spawnEnemy()) break;
}

// 预置塔（PTD_PERF_TOWERS>0）：按"机枪 LV1 射程覆盖多少路格"贪心选点，
// 与模拟器 bot 同一套选址口径 —— 塔要真的在打人，负载才有意义。
static void perfPlaceTowers(int want) {
  if (g_w.gold < 99999) g_w.gold = 99999;          // 探针里"钱"不是变量
  for (int n = 0; n < want; ++n) {
    int bc = -1, br = -1, best = -1;
    for (int r = 0; r < MAP_ROWS; ++r) for (int c = 0; c < COLS; ++c) {
      if (g_w.path[c][r] || g_w.occupied[c][r] || g_w.blocked[c][r]) continue;
      const float tx = c * CELL + CELL / 2.0f, ty = MAP_TOP + r * CELL + CELL / 2.0f;
      int sc = 0;
      for (int rr = 0; rr < MAP_ROWS; ++rr) for (int cc = 0; cc < COLS; ++cc) {
        if (!g_w.path[cc][rr]) continue;
        const float dx = cc * CELL + CELL / 2.0f - tx, dy = MAP_TOP + rr * CELL + CELL / 2.0f - ty;
        if (dx * dx + dy * dy <= 150.0f * 150.0f) ++sc;   // M.GUN LV1 range_px
      }
      if (sc > best) { best = sc; bc = c; br = r; }
    }
    if (bc < 0) break;
    // 机枪为主，每 3 座混一门加农（溅射 = BOOM 音效，音效洪峰更接近真实局）
    if (!buildTower(bc, br, (n % 3 == 2) ? 1 : 0)) break;
    // 升到 PTD_PERF_TLVL（探针里"钱"不是变量；LV3 射速最快 = 子弹与音效最多）
    for (int idx = 0; idx < MAX_TOWERS; ++idx)
      if (g_w.towers[idx].active && g_w.towers[idx].col == bc && g_w.towers[idx].row == br)
        for (int lv = 1; lv < (int)PTD_PERF_TLVL; ++lv) upgradeTower(idx);
  }
  int placed = 0;
  for (int i = 0; i < MAX_TOWERS; ++i) if (g_w.towers[i].active) ++placed;
  Serial.printf("PERF towers placed=%d gold=%d\n", placed, (int)g_w.gold);
}

// 每帧记账；每 30 帧（≈1s）打一行。分段计时照测量纪律拆开：
//   upd(gameUpdate) / draw(renderFrame+overlay) / sfx / tap，外加 GfxM5 记的
//   提交次数与像素数 —— "碎块税"与"带宽账"分开，才知道该砍谁。
static void perfFrame(uint32_t tapUs, uint32_t updUs, uint32_t drawUs,
                      uint32_t sfxUs, int cells, uint32_t dt, int alive) {
  static uint32_t n = 0, accTap = 0, accUpd = 0, accDraw = 0, accSfx = 0,
                  accCells = 0, accMaxDraw = 0, accDt = 0;
  static uint64_t accRects = 0, accSmall = 0, accPx = 0;
  static uint32_t sndMark = 0;
  static uint32_t eErase = 0, eTower = 0, eEnemy = 0, eBullet = 0, eFx = 0, eHud = 0;
  eErase += p_eraseUs; eTower += p_towerUs; eEnemy += p_enemyUs;
  eBullet += p_bulletUs; eFx += p_fxUs; eHud += p_hudUs;
  p_eraseUs = p_towerUs = p_enemyUs = p_bulletUs = p_fxUs = p_hudUs = 0;
  ++n; accTap += tapUs; accUpd += updUs; accDraw += drawUs; accSfx += sfxUs;
  accCells += (uint32_t)cells; accDt += dt;
  accRects += GfxM5::p_rects; accSmall += GfxM5::p_small; accPx += GfxM5::p_px;
  if (drawUs > accMaxDraw) accMaxDraw = drawUs;
  if (n < 30) return;
  const float f = (float)n;
  Serial.printf("PERF fps=%.1f frame=%.1fms | tap=%.2f upd=%.2f draw=%.2f sfx=%.2f "
                "| rects=%.0f small=%.0f px=%.0f cells=%.0f drawMax=%.1fms en=%d snd=%.1f\n",
                accDt ? 1000.0f * f / (float)accDt : 0.0f,
                (float)accDt / f,
                accTap / f, accUpd / f, accDraw / f, accSfx / f,
                (float)accRects / f, (float)accSmall / f, (float)accPx / f,
                (float)accCells / f, accMaxDraw / 1000.0f, alive,
                (float)(s_probePlayed - sndMark) / ((float)accDt / 1000.0f));   // 起播数/秒
  Serial.printf("     seg | erase=%.2f tower=%.2f enemy=%.2f bullet=%.2f fx=%.2f hud=%.2f ms/frame\n",
                eErase / f / 1000.0f, eTower / f / 1000.0f, eEnemy / f / 1000.0f,
                eBullet / f / 1000.0f, eFx / f / 1000.0f, eHud / f / 1000.0f);
  eErase = eTower = eEnemy = eBullet = eFx = eHud = 0;
  sndMark = s_probePlayed;
  n = 0; accTap = accUpd = accDraw = accSfx = accCells = accDt = 0; accMaxDraw = 0;
  accRects = accSmall = accPx = 0;
}
#endif  // PTD_PERF_PROBE

// 进度落盘：把解锁/通关/星级/引导/无尽记录写进 NVS
// （v0.4.1：去重逻辑在 progress.h，由仿真断言"通关画面只写一次"）
static void saveProgress(void) {
  s_prefs.begin("td", false);
  s_prefs.putUInt("unlock", g_unlocked);
  s_prefs.putUInt("clear",  g_cleared);
  s_prefs.putUInt("stars",  packStars(0));
  s_prefs.putUInt("stars2", packStars(10));
  s_prefs.putUChar("mute", s_muted ? 1 : 0);   // v0.3.1：静音偏好
#if PTD_HAS_CJK
  s_prefs.putUChar("lang", (uint8_t)g_lang);   // v0.5.3：界面语言（Claw4 单语时不写这个键）
#endif
  s_prefs.putUChar("onboarded", g_onboarded ? 1 : 0);  // v0.4.2：引导只讲一次
  s_prefs.putUShort("endbest", (uint16_t)g_endBest);   // v0.4.2：无尽最高波次
  s_prefs.end();
}

// ── 帧率控制：30fps 足够，显著降低 PSRAM 填充压力 ──────────────────
static const uint32_t FRAME_MS = 33;


// ============================================================================
// SETUP
// ============================================================================
void setup(void) {
  Serial.begin(115200);
  // USB-CDC 写阻塞会饿死 task WDT（关掉串口监视器就能复现）：超时=0 丢字节
  Serial.setTxTimeoutMs(0);

  esp_task_wdt_config_t twdt = { .timeout_ms = 8000,
                                  .idle_core_mask = (1 << 0) | (1 << 1),
                                  .trigger_panic = true };
  esp_task_wdt_init(&twdt);
  esp_task_wdt_add(NULL);

  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(3);              // Tab5 横屏 1280x720

  Serial.printf("=== PIXEL TOWER DEFENSE v0.5.4 | board=%d | %dx%d ===\n",
                (int)M5.getBoard(), SCREEN_W, SCREEN_H);

  // PSRAM 是帧缓冲所在地（DSI video mode）；没有就停下，别黑屏空转
  if (!psramFound()) {
    Serial.println("FATAL: no PSRAM - enable octal PSRAM @200MHz in platformio.ini");
    while (true) delay(1000);
  }

  // ── v0.2.0：读进度 → 标题界面（v0.4.2 起开机第一屏是标题）─────
  s_prefs.begin("td", true);
  g_unlocked = s_prefs.getUInt("unlock", 0x0001) | 0x0001;      // L1 永远解锁（v0.4：uint32）
  g_cleared  = s_prefs.getUInt("clear", 0);
  uint32_t packed = s_prefs.getUInt("stars", 0);               // v0.3.0：旧档无此键=0
  uint32_t packed2 = s_prefs.getUInt("stars2", 0);             // v0.4：L11-20
  sfxSetMuted(s_prefs.getUChar("mute", 0) != 0);              // v0.3.1
#if PTD_HAS_CJK
  setLang(s_prefs.getUChar("lang", LANG_EN));                 // v0.5.3：界面语言（默认英文）
#endif
  g_onboarded = s_prefs.getUChar("onboarded", 0) != 0;        // v0.4.2：引导只讲一次
  g_endBest   = (int16_t)s_prefs.getUShort("endbest", 0);     // v0.4.2：无尽最高波次
  s_prefs.end();
  for (int i = 0; i < LEVEL_COUNT && i < 10; ++i)  g_stars[i] = (uint8_t)((packed >> (i * 3)) & 7);
  for (int i = 10; i < LEVEL_COUNT && i < 20; ++i) g_stars[i] = (uint8_t)((packed2 >> ((i - 10) * 3)) & 7);
  g_saveProgress = saveProgress;

  levelLoad(0);                       // 预载 L1（选关前世界已就绪）
  g_w.state = ST_TITLE;               // v0.4.2：开机先进标题界面
  drawTitle(&g_disp);

#if PTD_HAS_CJK
  // 语言也要印出来：真机排障时"用户看到的是中文还是英文"直接影响判断
  Serial.printf("unlock=%04X cleared=%04X levels=%d endbest=%d%s\n",
                g_unlocked, g_cleared, LEVEL_COUNT, (int)g_endBest,
                langCN() ? " lang=CN" : " lang=EN");
#else
  // 单语形态：**逐字符与 v0.5.2 相同**（多一个 "" 实参都会让 claw4 位移，见零位移门禁）
  Serial.printf("unlock=%04X cleared=%04X levels=%d endbest=%d\n",
                g_unlocked, g_cleared, LEVEL_COUNT, (int)g_endBest);
#endif
  Serial.println("tap language = switch EN/中文 | tap = start");


  delay(1200);                            // 让开局界面可见
}

// ============================================================================
// LOOP — 输入 → 逻辑 → 渲染（顺序固定，每帧一次）
// ============================================================================
void loop(void) {
  static uint32_t lastFrame = 0;
  esp_task_wdt_reset();

#if PTD_PERF_PROBE && (PTD_PERF_SPAWN > 0)
  perfLoadTick();                         // 确定性合成负载（探针档专用）
#endif
#if PTD_PERF_PROBE
  const uint32_t tpTap = micros();
#endif
  Tap t = readTap();                      // 非阻塞：按住屏幕不会卡住世界
  if (t.valid) handleTap(&g_disp, t);

  // 标题/选关界面：静态画面，无世界逻辑（handleTap 已处理跳转）；
  // v0.4.1：锁关红叉反馈在选关界面推进（20 帧后自擦）
  if (g_w.state == ST_TITLE) { delay(20); return; }
  if (g_w.state == ST_SEL) { updateSelFx(&g_disp); delay(20); return; }

  uint32_t now = millis();
  uint32_t dt = now - lastFrame;
  if (dt < FRAME_MS) { delay(1); return; }   // 未到下一帧：让出 CPU
  if (dt > 60) dt = 60;                      // 限幅：防止卡顿后敌人瞬移
  lastFrame = now;

#if PTD_PERF_PROBE
  const uint32_t tTap = micros() - tpTap;
  int aliveNow = 0;
  for (int i = 0; i < MAX_ENEMIES; ++i) if (g_w.enemies[i].active) ++aliveNow;
  const uint32_t tpUpd = micros();
#endif

  gameUpdate(dt);                            // ── 逻辑（不碰绘制）──
#if PTD_PERF_PROBE
  const uint32_t tUpd = micros() - tpUpd;
  int dirtyCells = 0;                        // 本帧要擦回的格子数（**必须在 gameUpdate 之后**读）
  for (int c = 0; c < COLS; ++c)
    for (int r = 0; r < MAP_ROWS; ++r) if (g_w.dirty[c][r]) ++dirtyCells;
  GfxM5::p_rects = 0; GfxM5::p_small = 0; GfxM5::p_px = 0; GfxM5::p_drect = 0;
  const uint32_t tpDraw = micros();
#endif
  renderFrame(&g_disp);                  // ── 渲染（不碰逻辑）──
  // v0.4.2：首次引导 overlay（对话框区域点击推进；NVS 记住只讲一次）
  //
  // ⚠️ v0.4.3 修掉一个真机必现崩溃：原来这里**每帧**无条件调 drawOnboard。
  //    drawOnboard 的"半透明压暗"是 8px 网格逐块 fillRect，
  //    1280/8 × 720/8 = **14400 次 fillRect/帧**（30fps ≈ 43 万次/秒），
  //    每次都写 PSRAM 里的 DSI 帧缓冲，与扫描 DMA 疯狂抢 cache，
  //    最终死锁在 esp_cache_msync 的自旋锁上：
  //      MEPC 0x4ff0a57a → vPortExitCriticalMultiCore (port.c:569)
  //      RA   0x4ff07312 → esp_cache_msync (esp_cache_msync.c:135)
  //    loopTask 卡死 > TWDT 8s → task_wdt panic → 蓝屏重启。
  //    玩家看到的就是"一点『点击继续』就死"。
  //    引导期间世界静止（ST_READY），没有东西会盖掉 overlay，
  //    因此按 step 变化重画一次即可，无需每帧重刷。
  if (g_onboardStep > 0 &&
      (g_w.state == ST_READY || g_w.state == ST_WAVE)) {
    if (g_onboardStep != s_lastOnboardStep) {
      drawOnboard(&g_disp, g_onboardStep);
      s_lastOnboardStep = g_onboardStep;
    }
  } else {
    s_lastOnboardStep = 0;
  }
  updateBuildFx(&g_disp);              // ── 建造反馈动画 ──
  updateOverlays(&g_disp);             // ── Game Over 覆盖层（只画一次）──

#if PTD_PERF_PROBE
  const uint32_t tDraw = micros() - tpDraw;
  const uint32_t tpSfx = micros();
#endif
  // v0.3.1：音效派发放帧间隙（渲染之后，上次实测音频 ~4ms/帧塞得进）
  s_nowMs = now;
  sfxPump();

  // 通关瞬间落盘一次（不等点击，防断电丢进度；v0.4.1：去重逻辑与
  // 整型宽度在 progress.h，仿真有断言——停留 N 帧也只写一次）
  progressTickOnClear();
#if PTD_PERF_PROBE
  perfFrame(tTap, tUpd, tDraw, micros() - tpSfx, dirtyCells, dt, aliveNow);
#endif
}
