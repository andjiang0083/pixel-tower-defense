/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 andjiang0083
 */
// render.h — 画面渲染（只调用 fillRect / drawPixel，全部矩形像素风）
// Tab5 是 RAM 帧缓冲（MIPI DSI video mode），画面常驻内存，
// 所以静态地图只在开局画一次；每帧只擦除"脏格子"并重画活动实体。
#pragma once
#include <M5GFX.h>
#include "config_gen.h"
#include "i18n.h"        // v0.4.2：界面文本双语表（语言在标题界面选择）
#include "game.h"        // g_w / 实体类型（本模块只读不写游戏状态）
#include "pixel_text.h"

// ── 静态 UI 缓存（重开一局时必须归零，否则新界面不刷新）──────────
static int8_t  s_lastSel = -2;         // 建造栏选中缓存
static int32_t s_lastAfford = -1;     // 建造栏可负担档位缓存
static int8_t  s_lastBtn = -1;        // NEXT WAVE 按钮状态缓存
static int32_t s_lastPanelIdx = -1;   // 面板目标塔缓存
static uint8_t s_lastLv = 0;          // 面板等级缓存

// ── 主题色（v0.2.0）：逐关换装，真源 layout.py THEMES ────────────
static inline const Theme& TH(void) { return THEMES[LV().theme]; }

static inline void resetRenderCaches(void) {
  s_lastSel = -2; s_lastAfford = -1; s_lastBtn = -1;
  s_lastPanelIdx = -1; s_lastLv = 0;
}

// v0.4.2：无尽模式入口按钮几何（drawLevelClear 画 / actions.h 命中，唯一真源）
// v0.4.3：Y 从建造栏反推——旧值硬编码 590 让按钮底边(654)压进建造栏(640..720)，
// "SURVIVE 250 WAVES" 那行正好被槽位切掉（真机同款：renderFrame 先画建造栏、
// updateOverlays 后画通关面板，按钮画在槽位之上）。任何一边几何改动都不会再撞。
// 通关面板底边 = MAP_TOP + (SCREEN_H-MAP_TOP-320)/2 + 320 = 540，按钮区在 540..640。
#define ENDLESS_BTN_X 380
#define ENDLESS_BTN_Y (BUILD_BAR_Y - ENDLESS_BTN_H - 16)   // = 560，距槽位 16px、距面板 20px
#define ENDLESS_BTN_W 280
#define ENDLESS_BTN_H 64
#define CLEAR_PANEL_H 320                                   // drawLevelClear 面板高（断言用）
#define CLEAR_PANEL_Y (MAP_TOP + (SCREEN_H - MAP_TOP - CLEAR_PANEL_H) / 2)

// v0.4.2 前向声明（定义在文件末：renderFrame 在中间要调用）
static void drawSiege(M5GFX* g);

// ── 画一个格子的底色（草地棋盘纹 / 土路抖动纹）───────────────────
static void drawCellBase(M5GFX* g, int col, int row) {
  const int x = col * CELL;
  const int y = MAP_TOP + row * CELL;

  if (g_w.path[col][row]) {
    g->fillRect(x, y, CELL, CELL, TH().pathA);
    // 抖动土纹：错开的 4x4 暗块，纯矩形拼出颗粒感
    for (int i = 0; i < CELL; i += 8) {
      int off = ((i / 8) & 1) ? 4 : 0;
      for (int j = off; j < CELL; j += 8) g->fillRect(x + i, y + j, 4, 4, TH().pathB);
    }
    // 路径描边：只在邻格不是路的那条边上画，转角自然连成整圈
    if (col == 0        || !g_w.path[col-1][row]) g->fillRect(x + CELL - 3, y, 3, CELL, C_PATH_EDGE);
    if (col == COLS - 1 || !g_w.path[col+1][row]) g->fillRect(x, y, 3, CELL, C_PATH_EDGE);
    if (row == 0           || !g_w.path[col][row-1]) g->fillRect(x, y, CELL, 3, C_PATH_EDGE);
    if (row == MAP_ROWS - 1 || !g_w.path[col][row+1]) g->fillRect(x, y + CELL - 3, CELL, 3, C_PATH_EDGE);
    return;
  }

  // v0.4：禁建岩层（深灰乱石）——好位置稀缺要"看得见"，别无提示地拒绝
  if (g_w.blocked[col][row]) {
    g->fillRect(x, y, CELL, CELL, C_ROCK);
    g->fillRect(x + 4, y + 5, 12, 5, C_ROCK_HI);
    g->fillRect(x + CELL - 18, y + CELL - 13, 9, 4, C_ROCK_HI);
    g->fillRect(x + 2, y + CELL - 6, CELL - 4, 3, C_ROCK_LO);
    g->fillRect(x, y, CELL, 1, C_ROCK_LO);
    return;
  }

  g->fillRect(x, y, CELL, CELL, ((col + row) & 1) ? TH().grassB : TH().grassA);
  g->fillRect(x, y, CELL, 1, C_GRASS_EDGE);          // 网格线（横）
  g->fillRect(x, y, 1, CELL, C_GRASS_EDGE);          // 网格线（竖）
}

// ── HUD 横向布局（v0.4.3：从散落的硬编码 x 收敛为命名常量）────────
// 起因：三处硬编码 x 互相踩雷——
//   ① SIEGE 警示条的清空框 fillRect(300,14,180,..) 覆盖到 480，把静态 HUD 的
//      "$"(x=420) 每帧擦掉：v0.4.2 起金币符号在真机与仿真里都消失（K金变没有）。
//   ② 无尽模式的 BEST 数字从 770 起画、伸到 ~813，压住 WAVE 标签(x=800)——
//      截图放大可见 "42" 叠在 "W" 上。
//   ③ 静态标签原本在 drawBackground 与射程框重画两处复制，改一处必漏一处。
// 现在：布局是命名常量（仿真断言引用同一批宏），标签只有一个绘制函数。
#define HUD_HEART_X     16      // ♥ 心跳图标
#define HUD_HP_LBL_X    52      // "生命" / "HP"
#define HUD_HP_NUM_X    204     // 生命数字（擦除框 196..260）
#define HUD_SIEGE_X     300     // SIEGE 警示条：HP 数字与 $ 之间
#define HUD_SIEGE_W     112     // 放得下 "BUILD ZONE LOCKED"(101px)；右缘 412 < $ (420)
#define HUD_DOLLAR_X    420     // "$" 金币符号
#define HUD_GOLD_NUM_X  500     // 金币数字（擦除框 492..626）
#define HUD_BEST_X      640     // 无尽 BEST（GOLD 框 626 之后、WAVE 标签 800 之前）
#define HUD_WAVE_LBL_X  800     // "波次" / "WAVE"
#define HUD_WAVE_NUM_X  900     // 波次数字（擦除框 896..1030）

// HUD 静态标签（♥ / HP / $ / WAVE）——只画一次，两处重画共用同一份
static void drawHudLabels(M5GFX* g) {
  px_char(g, HUD_HEART_X, 24, '\x01', C_BAD, 4);                 // ♥
  px_text(g, HUD_HP_LBL_X, 20, TR(STR_HP),     C_TEXT_DIM, 3);
  px_text(g, HUD_DOLLAR_X, 20, "$",            C_GOLD,     3);
  px_text(g, HUD_WAVE_LBL_X, 20, TR(STR_WAVE), C_TEXT_DIM, 3);
}

// ── 静态背景：整张地图 + HUD 底板，只在开局 / 重开时画一次 ────────
static void drawBackground(M5GFX* g) {
  g->fillScreen(C_BG);
  for (int r = 0; r < MAP_ROWS; ++r)
    for (int c = 0; c < COLS; ++c) drawCellBase(g, c, r);

  // HUD 底板（两行高）
  g->fillRect(0, 0, SCREEN_W, MAP_TOP, C_UI_BG);
  g->fillRect(0, MAP_TOP - 2, SCREEN_W, 2, C_GRASS_EDGE);

  // 底部建造栏底板（v0.1.2）
  g->fillRect(0, BUILD_BAR_Y, SCREEN_W, BUILD_BAR_H, C_UI_BG);
  g->fillRect(0, BUILD_BAR_Y, SCREEN_W, 2, C_GRASS_EDGE);

  // 出生/终点标记：右缘红块（每条 route）→ 左缘金快，一眼看清攻防向
  for (int rr = 0; rr < LV().routes_n; ++rr) {
    const int cnt = routeCount(rr);
    const Waypoint& sp = routeWP(rr, 0);
    for (int i = 0; i < CELL; i += 8) g->fillRect(SCREEN_W - 4, MAP_TOP + sp.row*CELL + (i % 16), 4, 8, C_BAD);
    for (int i = 0; i < CELL; i += 8) g->fillRect(SCREEN_W - 8, MAP_TOP + sp.row*CELL + ((i+8) % 16), 4, 8, C_WARN);
    const Waypoint& ex = routeWP(rr, cnt - 1);
    for (int i = 0; i < CELL; i += 8) g->fillRect(0, MAP_TOP + ex.row*CELL + (i % 16), 4, 8, C_BAD);
    for (int i = 0; i < CELL; i += 8) g->fillRect(4, MAP_TOP + ex.row*CELL + ((i+8) % 16), 4, 8, C_GOLD);
  }

  // HUD 静态标签：♥ / $ / WAVE（数字每帧按需刷新）+ 关卡名
  drawHudLabels(g);
}

// ── 建造栏（v0.1.2）：槽位 = 塔种图标 + 名称 + 价格 ───────────────
// 只在「选中项变化」或「金币跨过可负担档位」时重画，不是每帧全刷。
static inline uint16_t towerColor(int type) {
  switch (type) {
    case 1:  return C_TOWER_CAN;
    case 2:  return C_TOWER_FRO;
    case 3:  return C_TOWER_ENERGY;   // v0.3.0
    case 4:  return C_TOWER_LASER;    // v0.3.0
    default: return C_TOWER_GAT;
  }
}

static inline int slotX(int i) { return SLOT_X0 + i * (SLOT_W + SLOT_GAP); }

static void drawBuildBar(M5GFX* g) {
  for (int i = 0; i < TT_COUNT; ++i) {
    const TowerType& T = TOWER_TYPES[i];
    const int x = slotX(i), y = BUILD_BAR_Y + 4;
    const bool afford   = (g_w.gold >= T.cost);
    const bool selected = (g_w.buildSel == i || (g_w.buildSel < 0 && i == 0));
    const uint16_t col  = afford ? towerColor(i) : C_TEXT_DIM;

    g->fillRect(x, y, SLOT_W, SLOT_H, C_SLOT_BG);
    // 选中框：金色双线（未选中只留一条暗边）
    g->drawRect(x, y, SLOT_W, SLOT_H, selected ? C_SLOT_SEL : C_GRASS_EDGE);
    if (selected) g->drawRect(x + 2, y + 2, SLOT_W - 4, SLOT_H - 4, C_SLOT_SEL);

    // 塔种图标：用缩小的塔外观（基座+塔身），保持与地图上的塔同形
    const int ix = x + 10, iy = y + 16;
    g->fillRect(ix, iy + 4, 28, 26, C_TOWER_BASE);
    g->fillRect(ix + 4, iy, 20, 26, col);
    if (i == 1)      g->fillRect(ix + 20, iy + 8, 14, 6, col);      // 加农：粗炮管
    else if (i == 2) { g->fillRect(ix + 10, iy - 6, 8, 34, col);     // 冰霜：十字晶
                       g->fillRect(ix + 2, iy + 8, 24, 8, col); }
    else if (i == 3) {                                             // 能量塔：电池块+闪电
      g->fillRect(ix + 8, iy - 4, 12, 6, col);                  // 电池头
      g->fillRect(ix + 4, iy + 2, 20, 22, col);                 // 电池身
      g->fillRect(ix + 10, iy + 6, 8, 4, C_BG);                 // 闪电刻痕（负形）
      g->fillRect(ix + 8, iy + 12, 8, 4, C_BG);
    } else if (i == 4) {                                         // 激光塔：聚焦透镜+光束
      g->fillRect(ix + 6, iy - 8, 16, 8, col);                 // 透镜座
      g->fillRect(ix + 12, iy - 14, 4, 8, C_LASER_BEAM);       // 光束尖
    } else             g->fillRect(ix + 12, iy - 8, 6, 30, col);        // 机枪：细枪管

    px_text(g, x + 46, y + 10, T.disp, afford ? C_TEXT : C_TEXT_DIM, 2);
    char buf[16]; px_itoa(T.cost, buf);
    g->fillRect(x + 46, y + 38, 90, FONT_H * 2 + 4, C_SLOT_BG);
    px_text(g, x + 46, y + 38, "$", afford ? C_GOLD : C_TEXT_DIM, 2);
    px_text(g, x + 64, y + 38, buf, afford ? C_GOLD : C_TEXT_DIM, 2);
    if (!afford) px_text(g, x + 46, y + 52, TR(STR_NEED), C_BAD, 1);
    // v0.3.0 对空角标：能打天的塔右上角一个小白飞机（决策一眼可读）
    if (TOWER_AIR_OK[i]) {
      const int ax = x + SLOT_W - 26, ay = y + 6;
      g->fillRect(ax, ay + 3, 18, 4, C_TEXT);
      g->fillRect(ax + 6, ay, 5, 10, C_TEXT);
    }
  }

  // 建造栏右侧操作提示（常驻，替代旧的浮动 hint）
  px_text(g, slotX(TT_COUNT) + 8, BUILD_BAR_Y + 14, TR(STR_TAP_SLOT), C_TEXT_DIM, 2);
  px_text(g, slotX(TT_COUNT) + 8, BUILD_BAR_Y + 34, TR(STR_THEN_MAP), C_TEXT_DIM, 2);
  px_text(g, slotX(TT_COUNT) + 8, BUILD_BAR_Y + 54, TR(STR_TAP_TOWER), C_TEXT_DIM, 1);
}

// ── HUD 数字（只在数值变化时调用）────────────────────────────────
// v0.4.1：三个擦除框一律按"最长可能内容"配宽并留余量——
//   HP 两位数字宽 36px 但旧框只有 35px：生命 10→9 时旧数字右缘 6px
//   擦不掉，留一道旧 '0' 残边（v0.4.1 修）。
//   GOLD 按 7 位配（无尽模式金币会超 5 位）；WAVE 按 "250/250"（7 字）
//   配并把文本左移，右缘仍必须 < BTN_X——伸进 NEXT WAVE 按钮区会重演
//   v0.2.1 真机 bug #3（按钮左缘被刷成黑底）。
static void drawHud(M5GFX* g) {
  char buf[16];

  // HP：数字前留心跳图标位
  g->fillRect(HUD_HP_NUM_X - 8, 14, 64, FONT_H * 3 + 6, C_UI_BG);
  px_itoa(g_w.lives, buf);
  px_text(g, HUD_HP_NUM_X, 20, buf, g_w.lives <= 5 ? C_HP_LOW : C_TEXT, 3);

  // GOLD
  g->fillRect(HUD_GOLD_NUM_X - 8, 14, (FONT_W + 1) * 3 * 7 + 8, FONT_H * 3 + 6, C_UI_BG);
  px_itoa(g_w.gold, buf);
  px_text(g, HUD_GOLD_NUM_X, 20, buf, C_GOLD, 3);

  // v0.4.2：无尽模式显示历史最高波次（NVS 持久）
  // v0.4.3：x 从 770 移到 HUD_BEST_X(640)——旧位置让数字伸到 ~813，
  // 压住静态的 WAVE 标签(800)，截图里 "42" 叠在 "W" 上。
  if (g_w.endless) {
    px_text(g, HUD_BEST_X, 26, TR(STR_BEST), C_TEXT_DIM, 1);
    px_itoa(g_endBest, buf);
    px_text(g, HUD_BEST_X + px_textWidth(TR(STR_BEST), 1) + 6, 26, buf, C_STAR, 1);
  }

  // WAVE x/N（本关总波数一目了然；无尽模式总波数走 lvWaves()=250）
  g->fillRect(HUD_WAVE_NUM_X - 4, 14, (FONT_W + 1) * 3 * 7 + 8, FONT_H * 3 + 6, C_UI_BG);
  char wbuf[24]; px_itoa(g_w.wave, wbuf);
  strcat(wbuf, "/"); char tbuf[8]; px_itoa(lvWaves(), tbuf); strcat(wbuf, tbuf);
  px_text(g, HUD_WAVE_NUM_X, 20, wbuf, C_TEXT, 3);

  // v0.4.2：SIEGE 警示条（HP 与 $ 之间的空档；常态清空，封锁中画红字）
  // v0.4.3：宽度 180→HUD_SIEGE_W(112)。旧框铺到 480，把静态 "$"(420) 擦了。
  g->fillRect(HUD_SIEGE_X, 14, HUD_SIEGE_W, FONT_H * 3 + 6, C_UI_BG);
  if (g_w.siegeMs > 0) px_text(g, HUD_SIEGE_X, 26, TR(STR_SIEGE_WARN), C_BAD, 1);
}

// ── NEXT WAVE 按钮（v0.2.1：波间变倒计时条）───────────────────────
// 几何真源在 config_gen.h（tools/layout.py），渲染与命中判定共用同一组宏。
// 首波：静态 "NEXT WAVE"（手动，留布塔时间）。
// 之后：绿色进度条随 readyLeftMs 递减，归零自动开下一波 —— 制造压迫感。
#define BAR_X  (BTN_X + 8)
#define BAR_Y  (BTN_Y + 34)
#define BAR_W  (BTN_W - 16)
#define BAR_H  14

static void drawButton(M5GFX* g, bool enabled) {
  uint16_t bg = enabled ? C_BTN : C_UI_BG;
  g->fillRect(BTN_X, BTN_Y, BTN_W, BTN_H, bg);
  // 双线描边（像素风按钮）
  g->drawRect(BTN_X, BTN_Y, BTN_W, BTN_H, enabled ? C_BTN_HI : C_GRASS_EDGE);
  g->drawRect(BTN_X + 2, BTN_Y + 2, BTN_W - 4, BTN_H - 4, enabled ? C_BTN_HI : C_GRASS_EDGE);
  const char* s = (g_w.state == ST_WAVE) ? TR(STR_FIGHTING) : TR(STR_NEXT_WAVE);
  px_textCenter(g, BTN_X + BTN_W / 2, BTN_Y + 8, s,
                enabled ? C_BTN_TEXT : C_TEXT_DIM, 2);

  // 倒计时区底（先铺满暗槽，再由 drawWaveCountdown 填绿）
  g->fillRect(BAR_X, BAR_Y, BAR_W, BAR_H, C_SLOT_BG);
  g->drawRect(BAR_X, BAR_Y, BAR_W, BAR_H, enabled ? C_GRASS_EDGE : C_UI_BG);
}

// 每帧刷新倒计时条（仅 ST_READY 且已过首波）；绿块从右往左消退到 0。
static void drawWaveCountdown(M5GFX* g) {
  if (g_w.state != ST_READY || g_w.wave <= 0) return;
  // 槽底重铺（擦掉上一帧的绿）
  g->fillRect(BAR_X + 1, BAR_Y + 1, BAR_W - 2, BAR_H - 2, C_SLOT_BG);
  const uint32_t left = (g_w.readyLeftMs == 0xFFFFFFFFu) ? AUTO_NEXT_MS : g_w.readyLeftMs;
  const int fill = (int)((long)(BAR_W - 2) * left / AUTO_NEXT_MS);
  const uint16_t col = (left < AUTO_NEXT_MS / 3) ? C_BAD :   // 最后 1/3 变红告急
                       (left < AUTO_NEXT_MS * 2 / 3) ? C_GOLD : C_HP_FULL;
  if (fill > 0) g->fillRect(BAR_X + 1, BAR_Y + 1, fill, BAR_H - 2, col);
  // 剩余秒数：scale1（7px）居中压在条内，不越界、不压标题
  char buf[8]; px_itoa((int)((left + 999) / 1000), buf);
  px_textCenter(g, BAR_X + BAR_W / 2, BAR_Y + (BAR_H - FONT_H) / 2, buf, C_BG, 1);
}

// ── 塔：按种类换色 + 等级方块，全矩形堆叠 ────────────────────────
static void drawTower(M5GFX* g, const Tower& t) {
  const int x = t.col * CELL;
  const int y = MAP_TOP + t.row * CELL;
  const int cx = x + CELL / 2, cy = y + CELL / 2;
  const uint16_t col = towerColor(t.type);

  g->fillRect(x + 4, y + 8, CELL - 8, CELL - 12, C_TOWER_BASE);   // 基座
  g->fillRect(x + 8, y + 4, CELL - 16, CELL - 12, col);             // 塔身（种类色）
  g->fillRect(x + 14, y + 8, CELL - 28, 6, C_TOWER_TOP);            // 高光

  if (t.type == 2) {
    // ❄️ 冰霜塔：中心十字晶，无枪管（它是光环脉冲，不发弹）
    g->fillRect(cx - 2, cy - 10, 4, 20, C_FROST_RING);
    g->fillRect(cx - 10, cy - 2, 20, 4, C_FROST_RING);
    if (t.cooldownMs > towerFireMs(t) - 40)          // 脉冲瞬间：扩散方框
      g->drawRect(cx - 16, cy - 16, 32, 32, C_FROST_RING);
  } else if (t.type == 3) {
    // 🔋 v0.3.0 能量塔：电池外形 + 产金脉冲（无枪管）
    g->fillRect(cx - 8, cy - 14, 16, 6, col);              // 电池头
    g->fillRect(cx - 12, cy - 8, 24, 20, col);            // 电池身
    g->fillRect(cx - 4, cy - 4, 8, 3, C_BG);              // 闪电刻痕
    g->fillRect(cx - 7, cy + 2, 8, 3, C_BG);
    if (t.cooldownMs > towerFireMs(t) - 120) {            // 产金瞬间：金色扩散框
      g->drawRect(cx - 18, cy - 18, 36, 36, C_GOLD);
      g->fillRect(cx - 2, cy - 24, 4, 6, C_GOLD);
    }
  } else if (t.type == 4) {
    // ⚡ v0.3.0 激光塔：四爪聚焦透镜，中心亮点随充能呼吸
    g->fillRect(cx - 12, cy - 12, 8, 8, col);             // 四角聚焦爪
    g->fillRect(cx + 4, cy - 12, 8, 8, col);
    g->fillRect(cx - 12, cy + 4, 8, 8, col);
    g->fillRect(cx + 4, cy + 4, 8, 8, col);
    const bool charging = t.cooldownMs > towerFireMs(t) / 2;
    g->fillRect(cx - 5, cy - 5, 10, 10, charging ? C_TOWER_LASER : C_LASER_BEAM);
    g->fillRect(cx - 2, cy - 2, 4, 4, C_FX_WHITE);        // 透镜芯
    if (t.beamMs > 0) {                                    // 🔴 光束残留：塔心→目标
      const int bx = (int)t.bx, by = (int)t.by;
      // Bresenham 步进画 2px 粗光束（纯 fillRect，无 drawLine 依赖）
      const int ax = cx, ay = cy;
      int dx = abs(bx - ax), dy = abs(by - ay);
      int sx = ax < bx ? 1 : -1, sy = ay < by ? 1 : -1;
      int err = dx - dy, steps = 0;
      int px = ax, py = ay;
      const int maxSteps = dx + dy + 2;
      while (steps++ < maxSteps) {
        g->fillRect(px - 1, py - 1, 3, 3, C_LASER_BEAM);
        if (px == bx && py == by) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; px += sx; }
        if (e2 < dx)  { err += dx; py += sy; }
      }
      g->fillRect(bx - 3, by - 3, 7, 7, C_FX_WHITE);    // 命中端亮斑
    }
  } else {
    // 枪管：沿瞄准八分位从塔心伸出（像素步进，360° 全向可见）
    const int8_t* d = TURRET_DIR[t.aoct & 7];
    const int barLen = (t.type == 1) ? 4 : 3;        // 加农炮管更长更粗
    const int barW   = (t.type == 1) ? 6 : 4;
    for (int i = 1; i <= barLen; ++i)
      g->fillRect(cx + d[0] * i - barW/2, cy + d[1] * i - barW/2, barW, barW, col);

    if (t.recoil > 0) {                                // 枪口焰在枪口端
      const int mx = cx + d[0] * (barLen + 1), my = cy + d[1] * (barLen + 1);
      g->fillRect(mx - 3, my - 3, 6, 6, C_TOWER_MUZZLE);
      if (t.type == 1) g->fillRect(mx + d[0] * 3 - 4, my + d[1] * 3 - 4, 8, 8, C_SPLASH);
    }
  }

  // 等级方块：塔基下沿，LV2=两块 LV3=三块（远看也能读出等级）
  for (int lv = 1; lv < t.level; ++lv)
    g->fillRect(x + 8 + (lv - 1) * 9, y + CELL - 6, 7, 4, C_GOLD);
}

// ── 子弹：机枪=亮金小弹 / 加农=暗铁炮弹，都带短拖尾 ──────────────
static void drawBullet(M5GFX* g, const Bullet& b) {
  int x = (int)b.x, y = (int)b.y;
  if (b.splashR > 0) {                               // 炮弹：更大更暗的铁壳
    g->fillRect(x - 5, y - 4, 10, 8, C_TOWER_CAN);
    g->fillRect(x - 3, y - 2, 6, 4, C_SPLASH);   // 引信亮芯
  } else {
    g->fillRect(x - 4, y - 3, 8, 6, C_TOWER_MUZZLE);   // 暗金外壳
    g->fillRect(x - 3, y - 2, 6, 4, C_BULLET);          // 亮核
  }
  int tx = b.vx ? (b.vx > 0 ? -9 : 5) : -7;           // 拖尾方向随飞行方向
  int ty = b.vy ? (b.vy > 0 ? -7 : 3) : -1;
  g->fillRect(x + tx, y + ty, 4, 2, C_HP_FULL);       // 短拖尾，非粒子
}

// ── 敌人：逐关尺寸 + 兵种配色/外形；ELITE 金边；血条黑描边 ────────
static void drawEnemy(M5GFX* g, const Enemy& e) {
  const int s = e.size;                    // v0.2.0：BOSS/普通逐关尺寸
  const int x = (int)e.x - s / 2, y = (int)e.y - s / 2;

  uint16_t body;
  switch (e.kind) {
    case 1:        body = C_ENEMY_B;   break;   // RUNNER 橙
    case 2:        body = C_ENEMY_C;   break;   // TANK   紫
    case K_SWARM:  body = C_ENEMY_SWARM; break; // SWARM  黄绿
    case K_ARMOR:  body = C_ENEMY_ARMOR; break; // ARMOR  钢灰
    case K_BOSS:   body = C_ENEMY_BOSS; break;  // BOSS   品红
    case K_HEAL:   body = C_ENEMY_HEAL; break;  // HEALER 绿
    case K_CHOPPER:body = C_ENEMY_CHOPPER; break;// CHOPPER 天青
    case K_SHIELD: body = C_ENEMY_SHIELD; break;  // SHIELD  土金
    default:       body = C_ENEMY_A;   break;   // GRUNT  红
  }
  if (e.flash > 0) body = C_FX_WHITE;

  // v0.3.0 空中单位：贴地阴影（分离感的关键）+ 机身抬高已在坐标里
  if (e.flying) {
    const int shW = s + 6, shX = (int)e.x - shW / 2, shY = (int)e.y + FLY_LIFT + s/2 - 4;
    g->fillRect(shX, shY, shW, 6, C_SHADOW);
    g->fillRect(shX + 4, shY - 2, shW - 8, 2, C_SHADOW);
    // 旋翼：两叶横条 + 高速闪烁（奇偶帧换长度，读起来就是在转）
    const int rw = (e.flash & 1) ? s + 20 : s + 8;
    g->fillRect((int)e.x - rw / 2, (int)e.y - s / 2 - 4, rw, 3, C_TEXT_DIM);
    g->fillRect((int)e.x - 2, (int)e.y - s / 2 - 8, 4, 6, C_TEXT_DIM);   // 旋翼轴
  }

  g->fillRect(x, y, s, s, body);
  g->drawRect(x, y, s, s, C_BG);                 // 暗色描边，和土路拉开层次
  if (e.elite) {                                 // ELITE：双层金边（一眼认出硬茬）
    g->drawRect(x - 2, y - 2, s + 4, s + 4, C_ELITE);
    g->fillRect(x - 2, y - 2, 4, 4, C_ELITE);
    g->fillRect(x + s - 2, y - 2, 4, 4, C_ELITE);
    g->fillRect(x - 2, y + s - 2, 4, 4, C_ELITE);
    g->fillRect(x + s - 2, y + s - 2, 4, 4, C_ELITE);
  }

  const int es = (s >= 28) ? 4 : 3;
  if (e.kind == K_ARMOR) {                       // 护甲兵：横向装甲板条纹
    for (int i = 4; i < s - 6; i += 8) g->fillRect(x + i, y + s / 2 - 2, 6, 4, C_BG);
    g->drawRect(x + 3, y + 3, s - 6, s - 6, C_TEXT_DIM);   // 钢板内框
  } else if (e.kind == K_BOSS) {                // BOSS：大眼 + 顶部尖齿
    g->fillRect(x + s / 4 - 3, y + s / 3, 6, 8, C_BG);
    g->fillRect(x + s * 3 / 4 - 3, y + s / 3, 6, 8, C_BG);
    for (int i = 0; i < s; i += 12) g->fillRect(x + i, y - 6, 8, 6, body);
    g->drawRect(x + 4, y + 4, s - 8, s - 8, C_GOLD);       // 金边 = BOSS 身份
    if (LV().ability & AB_HEAL)                              // 治疗王：胸口绿核
      g->fillRect(x + s / 2 - 4, y + s * 2 / 3 - 4, 8, 8, C_ENEMY_HEAL);
    if (LV().ability & AB_BROOD)                             // 产卵王：腹下卵点
      for (int i = -1; i <= 1; ++i) g->fillRect(x + s / 2 + i * 8 - 2, y + s - 4, 4, 4, C_ENEMY_SWARM);
  } else if (e.kind == K_HEAL) {                 // 奶妈：白十字（治疗者符号）
    g->fillRect(x + s / 2 - 2, y + 4, 4, s - 8, C_FX_WHITE);
    g->fillRect(x + 4, y + s / 2 - 2, s - 8, 4, C_FX_WHITE);
  } else if (e.kind == K_SHIELD) {              // v0.3.2 弹盾兵：正面金属盾+铆钉
    g->fillRect(x - 4, y + 2, 5, s - 4, C_TEXT_DIM);      // 盾板（行进方向）
    g->fillRect(x - 4, y + 2, 5, 3, C_ELITE);              // 盾沿高光
    g->fillRect(x - 3, y + s/2 - 1, 3, 3, C_BG);           // 铆钉
    g->drawRect(x + 2, y + 4, s - 6, s - 8, C_BG);         // 盾兵躯干内框
    if (e.flash > 0) g->fillRect(x - 4, y + s/2 - 3, 5, 6, C_FX_WHITE);   // 挡弹火花
  } else if (e.kind == K_SWARM) {               // 虫群：单眼小方块
    g->fillRect(x + s / 2 - 2, y + s / 3, 4, 4, C_BG);
  } else {                                       // GRUNT/RUNNER/TANK：双眼
    g->fillRect(x + s / 4 - es / 2, y + s / 3, es, es, C_BG);
    g->fillRect(x + s * 3 / 4 - es / 2, y + s / 3, es, es, C_BG);
    if (e.kind == 2) g->drawRect(x + 3, y + 3, s - 6, s - 6, C_BG); // TANK 内框
  }

  if (e.slowMs > 0) g->drawRect(x - 2, y - 2, s + 4, s + 4, C_FROST_RING); // 减速=青框

  // 血条：满血不画（减少视觉噪声）；外圈 1px 黑框 —— 土路上没有描边
  // 时暗红残血条对比度趋零（视觉评审实测），描边是必要项不是装饰
  if (e.hp < e.maxHp) {
    const int bw = s, bx = x - 1, by = y - 9;
    g->fillRect(bx, by, bw + 2, 6, C_BG);          // 描边底板
    g->fillRect(bx + 1, by + 1, bw, 4, C_HP_BACK);
    int fw = (int)((long)bw * e.hp / e.maxHp);
    if (fw < 2) fw = 2;                             // 残血也留可见宽度
    g->fillRect(bx + 1, by + 1, fw & ~1, 4, (e.hp * 3 < e.maxHp) ? C_HP_LOW : C_HP_FULL);
  }
}

// ── 命中/爆炸闪光：扩散空心方框，4~5 帧消失（kind1=橙溅射 kind2=治疗）─
static void drawFx(M5GFX* g, const Fx& f) {
  int r = (4 - f.lifeFrames) * 5 + 4;
  if (f.kind == 1) {                       // 溅射：双层方框，更大
    g->drawRect((int)f.x - r, (int)f.y - r, r * 2, r * 2, C_SPLASH);
    g->drawRect((int)f.x - r + 4, (int)f.y - r + 4, (r-4) * 2, (r-4) * 2, C_TOWER_MUZZLE);
  } else if (f.kind == 2) {               // 治疗：绿十字上升小段
    const uint16_t c = C_ENEMY_HEAL;
    g->fillRect((int)f.x - 2, (int)f.y - r + 8, 4, 10, c);
    g->fillRect((int)f.x - 5, (int)f.y - r + 12, 10, 4, c);
  } else if (f.kind == 3) {              // v0.3.0 能量塔产金：金色方块上浮
    const int lift = (4 - f.lifeFrames) * 6;
    g->fillRect((int)f.x - 4, (int)f.y - 8 - lift, 8, 8, C_GOLD);
    g->fillRect((int)f.x - 2, (int)f.y - 6 - lift, 4, 4, C_UI_BG);   // $ 负形
    g->fillRect((int)f.x - 1, (int)f.y - 3 - lift, 2, 6, C_GOLD);
  } else if (f.kind == 4) {              // v0.4 📢 HORN：金色扩散波纹环
    const int rr = r * 2;
    g->drawRect((int)f.x - rr, (int)f.y - rr, rr * 2, rr * 2, C_GOLD);
    g->fillRect((int)f.x - 3, (int)f.y - rr - 6, 6, 6, C_GOLD);      // 号口亮点
  } else if (f.kind == 5) {             // v0.4 🔇 EMP：紫色电环
    const int rr = r + 6;
    g->drawRect((int)f.x - rr, (int)f.y - rr, rr * 2, rr * 2, C_TOWER_LASER);
    for (int a = -rr; a <= rr; a += 8)                                  // 放射电刺
      g->fillRect((int)f.x + a - 1, (int)f.y - rr - 4, 3, 5, C_FX_WHITE);
  } else if (f.kind == 6) {             // v0.4 💰 GREED：-$红字上浮
    const int lift = (4 - f.lifeFrames) * 5;
    g->fillRect((int)f.x - 8, (int)f.y - lift, 16, 9, C_UI_BG);      // 底板
    px_text(g, (int)f.x - 8, (int)f.y - lift, "-$", C_BAD, 1);
  } else if (f.kind == 7) {             // v0.4 🛡 MOLT：蜕壳裂纹白闪
    g->drawRect((int)f.x - r, (int)f.y - r, r * 2, r * 2, C_FX_WHITE);
    g->fillRect((int)f.x - r, (int)f.y - 1, r * 2, 2, C_FX_WHITE);   // 横裂
    g->fillRect((int)f.x - 1, (int)f.y - r, 2, r * 2, C_FX_WHITE);   // 竖裂
  } else if (f.kind == 8) {             // v0.4.2 🚧 SIEGE：封锁红框脉冲
    g->drawRect((int)f.x - r, (int)f.y - r, r * 2, r * 2, C_BAD);
    g->drawRect((int)f.x - r + 6, (int)f.y - r + 6, (r-6) * 2, (r-6) * 2, C_WARN);
  } else {
    g->drawRect((int)f.x - r, (int)f.y - r, r * 2, r * 2, C_FX_WHITE);
  }
}

// ── 擦除脏格子（把上一帧的实体痕迹恢复成地图底色）────────────────
static void eraseDirty(M5GFX* g) {
  for (int r = 0; r < MAP_ROWS; ++r)
    for (int c = 0; c < COLS; ++c) {
      if (!g_w.dirty[c][r]) continue;
      g_w.dirty[c][r] = false;
      drawCellBase(g, c, r);
    }
}

// ── 塔升级面板（v0.1.2）：点已建塔弹出，打开=世界暂停 ─────────────
// 面板贴在塔的旁边（自动避开屏幕边缘）；关闭时把盖住的地图格
// 和塔原样补画回去 —— 纯矩形 UI，不留残影。
static bool g_panelOpen = false;
static int16_t g_panelX = 0, g_panelY = 0;

// ── 射程预览框残留治理（v0.2.1，真机 bug #3）─────────────────────
// 预览框画在面板底板之外（射程半径 > 半屏宽），关面板/升级换半径时
// 旧框不会被面板补画覆盖 → 永久残留。解法：记住上一次预览框的包围盒，
// 重画面板或关面板时先把该盒子区域刷回底色 + 补画区内塔。
static int16_t g_rbX0=-1, g_rbY0=0, g_rbX1=-1, g_rbY1=0;

static void eraseRangeBox(M5GFX* g) {
  if (g_rbX1 < 0) return;
  // ⚠️ v0.2.1：预览框会伸进 HUD 带（塔在第一行、射程 > MAP_TOP）和建造栏。
  //   地图格补画管不到这两条带 → 顶部黑底残留（真机 bug #5）。
  //   解法：把越界的那一段单独刷回 UI 底色，再把该带上的动态内容重画。
  if (g_rbY0 < MAP_TOP) {
    g->fillRect(g_rbX0, 0, g_rbX1 - g_rbX0 + 1, MAP_TOP, C_UI_BG);
    drawHud(g); g_w.hudDirty = false;                       // 数字补回
    drawButton(g, s_lastBtn >= 0 ? (bool)s_lastBtn : true); // NEXT WAVE / 倒计时槽
    if (g_w.state == ST_READY && g_w.wave > 0) drawWaveCountdown(g);
    drawHudLabels(g);                                      // HUD 静态标签（共用）
  }
  if (g_rbY1 >= BUILD_BAR_Y) {
    g->fillRect(g_rbX0, BUILD_BAR_Y, g_rbX1 - g_rbX0 + 1, SCREEN_H - BUILD_BAR_Y, C_UI_BG);
    g->fillRect(g_rbX0, BUILD_BAR_Y, g_rbX1 - g_rbX0 + 1, 2, C_GRASS_EDGE);
    drawBuildBar(g);                                        // 塔种槽整条重画（便宜且无残影）
  }
  const int c0 = g_rbX0 / CELL, c1 = (g_rbX1 + CELL - 1) / CELL;
  const int r0 = (g_rbY0 - MAP_TOP) / CELL, r1 = (g_rbY1 - MAP_TOP) / CELL;
  for (int c = c0; c <= c1; ++c) {
    if (c < 0 || c >= COLS) continue;
    for (int r = r0; r <= r1; ++r) {
      if (r < 0 || r >= MAP_ROWS) continue;
      drawCellBase(g, c, r);
    }
  }
  for (int i = 0; i < MAX_TOWERS; ++i) {          // 区内塔补画
    Tower& t = g_w.towers[i];
    if (!t.active) continue;
    const int tx = t.col * CELL, ty = MAP_TOP + t.row * CELL;
    if (tx <= g_rbX1 && tx + CELL >= g_rbX0 && ty <= g_rbY1 && ty + CELL >= g_rbY0)
      drawTower(g, t);
  }
  for (int i = 0; i < MAX_ENEMIES; ++i) {         // 区内敌人补画（面板暂停时它们静止，但产卵/漏怪帧可能重叠）
    Enemy& e = g_w.enemies[i];
    if (!e.active) continue;
    const int hs = e.size / 2 + 4;
    if (e.x + hs >= g_rbX0 && e.x - hs <= g_rbX1 && e.y + hs >= g_rbY0 && e.y - hs <= g_rbY1)
      drawEnemy(g, e);
  }
  g_rbX1 = -1;                                     // box cleared
}

// 面板内两个按钮的全屏几何（actions.h 命中判定共用）
static inline void panelBtnRects(int* upX, int* upY, int* sellX, int* sellY) {
  *upX   = g_panelX + PBTN_UP_X;  *upY   = g_panelY + PBTN_UP_Y;
  *sellX = g_panelX + PBTN_UP_X + PBTN_W + 8;
  *sellY = g_panelY + PBTN_UP_Y;
}

static void drawTowerPanel(M5GFX* g, const Tower& t) {
  eraseRangeBox(g);                                 // v0.2.1：先擦旧预览框（升级换半径场景）

  // 贴在塔的右侧；放不下就翻到左侧 / 夹紧到屏内
  int px = t.col * CELL + CELL + 10;
  if (px + PANEL_W > SCREEN_W) px = t.col * CELL - PANEL_W - 10;
  if (px < 4) px = 4;
  int py = MAP_TOP + t.row * CELL - PANEL_H / 2;
  if (py < MAP_TOP + 4) py = MAP_TOP + 4;
  if (py + PANEL_H > BUILD_BAR_Y - 4) py = BUILD_BAR_Y - 4 - PANEL_H;
  g_panelX = px; g_panelY = py; g_panelOpen = true;

  // 底板 + 双线边框（种类色 = 塔的身份证）
  g->fillRect(px, py, PANEL_W, PANEL_H, C_UI_BG);
  const uint16_t col = towerColor(t.type);
  g->drawRect(px, py, PANEL_W, PANEL_H, col);
  g->drawRect(px + 2, py + 2, PANEL_W - 4, PANEL_H - 4, col);

  char buf[24];
  // 标题行：塔名 + LV
  px_text(g, px + 14, py + 12, TOWER_TYPES[t.type].disp, C_TEXT, 3);
  strcpy(buf, "LV"); px_itoa(t.level, buf + 2);
  px_text(g, px + PANEL_W - px_textWidth(buf, 3) - 14, py + 12, buf, C_GOLD, 3);

  // 属性行：DMG / RNG（冰霜塔显示 SLOW，能量塔显示产金）
  bool noRange = false;                          // v0.3.0：能量塔没有射程预览
  px_itoa(towerDamage(t), buf);
  if (t.type == 3) {                                   // v0.3.0 能量塔
    px_text(g, px + 14, py + 58, "+$", C_GOLD, 2);
    px_text(g, px + 70, py + 58, buf, C_GOLD, 2);
    px_text(g, px + 150, py + 58, TR(STR_PULSE), C_TEXT_DIM, 2);
    px_itoa((int)towerFireMs(t), buf);
    strcat(buf, "MS");
    px_text(g, px + 210, py + 58, buf, C_TEXT, 2);
    px_text(g, px + 14, py + 84, TR(STR_NO_ATTACK), C_TEXT_DIM, 1);
    noRange = true;
  } else if (t.type == 2) {
    px_text(g, px + 14, py + 58, TR(STR_SLOW), C_FROST_RING, 2);
    px_text(g, px + 14, py + 80, TR(STR_PULSE), C_TEXT_DIM, 2);
    px_itoa((int)towerRange(t), buf);
    px_text(g, px + 150, py + 80, TR(STR_RNG), C_TEXT_DIM, 2);
    px_text(g, px + 210, py + 80, buf, C_TEXT, 2);
  } else {
    px_text(g, px + 14, py + 58, TR(STR_DMG), C_TEXT_DIM, 2);
    px_text(g, px + 70, py + 58, buf, C_TEXT, 2);
    px_itoa((int)towerRange(t), buf);
    px_text(g, px + 150, py + 58, TR(STR_RNG), C_TEXT_DIM, 2);
    px_text(g, px + 210, py + 58, buf, C_TEXT, 2);
    if (t.type == 1) px_text(g, px + 70, py + 84, TR(STR_SPLASH), C_SPLASH, 2);
    if (TOWER_TYPES[t.type].splash_r > 0) px_text(g, px + 150, py + 84, TR(STR_NO_ARMOR), C_SPLASH, 1);
    else px_text(g, px + 70, py + 84, TR(STR_SINGLE), C_TEXT_DIM, 1);
  }

  // UP / SELL 按钮
  int ux = px + PBTN_UP_X, uy = py + PBTN_UP_Y;
  int sx = ux + PBTN_W + 8, sy = uy;
  const int upc = upgradeCost(t);
  if (upc < 0) {                                  // 已满级
    g->fillRect(ux, uy, PBTN_W, PBTN_H, C_SLOT_BG);
    g->drawRect(ux, uy, PBTN_W, PBTN_H, C_TEXT_DIM);
    px_textCenter(g, ux + PBTN_W / 2, uy + 14, TR(STR_MAX), C_GOLD, 3);
    px_textCenter(g, ux + PBTN_W / 2, uy + 48, TR(STR_LEVEL), C_TEXT_DIM, 1);
  } else {
    const bool ok = (g_w.gold >= upc);
    g->fillRect(ux, uy, PBTN_W, PBTN_H, ok ? C_BTN : C_SLOT_BG);
    g->drawRect(ux, uy, PBTN_W, PBTN_H, ok ? C_BTN_HI : C_TEXT_DIM);
    px_textCenter(g, ux + PBTN_W / 2, uy + 8, TR(STR_UPGRADE), ok ? C_BTN_TEXT : C_TEXT_DIM, 2);
    strcpy(buf, "$"); px_itoa(upc, buf + 1);
    px_textCenter(g, ux + PBTN_W / 2, uy + 36, buf, ok ? C_GOLD : C_BAD, 2);
    if (!ok) px_textCenter(g, ux + PBTN_W / 2, uy + 56, TR(STR_NEED_GOLD), C_BAD, 1);
  }

  g->fillRect(sx, sy, PBTN_W - 40, PBTN_H, C_SLOT_BG);
  g->drawRect(sx, sy, PBTN_W - 40, PBTN_H, C_BAD);
  px_textCenter(g, sx + (PBTN_W - 40) / 2, sy + 8, TR(STR_SELL), C_BAD, 2);
  strcpy(buf, "$"); px_itoa(sellRefund(t), buf + 1);
  px_textCenter(g, sx + (PBTN_W - 40) / 2, sy + 36, buf, C_GOLD, 2);

  // 射程预览：空心方框（比圆便宜，且像素风一致）；记住包围盒供擦除
  // v0.3.0：能量塔射程=0，不画预览框（会退化成一个点且无意义）
  if (!noRange) {
    const int rr = (int)towerRange(t);
    const int tcx = t.col * CELL + CELL / 2, tcy = MAP_TOP + t.row * CELL + CELL / 2;
    g->drawRect(tcx - rr, tcy - rr, rr * 2, rr * 2,
                t.type == 2 ? C_FROST_RING : (t.type == 4) ? C_TOWER_LASER : C_BTN_HI);
    g_rbX0 = tcx - rr; g_rbY0 = tcy - rr;
    g_rbX1 = tcx + rr; g_rbY1 = tcy + rr;
    // ⚠️ v0.2.1：只夹到屏幕边界，绝不能夹 MAP_TOP / BUILD_BAR_Y——
    //   预览框本身会画进 HUD 带和建造栏，夹掉擦除区就永远擦不到（真机 bug #5）
    if (g_rbX0 < 0) g_rbX0 = 0;
    if (g_rbY0 < 0) g_rbY0 = 0;
    if (g_rbX1 > SCREEN_W - 1) g_rbX1 = SCREEN_W - 1;
    if (g_rbY1 > SCREEN_H - 1) g_rbY1 = SCREEN_H - 1;
  } else {
    eraseRangeBox(g);       // 从别的塔切过来时，旧预览框先清掉
  }
}

// ── 关闭面板：把盖住的地图格补回底色，塔重画（残留敌人由脏格机制自愈）
static void closeTowerPanel(M5GFX* g) {
  if (!g_panelOpen && g_rbX1 < 0) return;
  // v0.2.1：先擦射程预览框（它会伸到面板外，光靠补画面板盖不住）
  eraseRangeBox(g);
  if (!g_panelOpen) return;
  const int c0 = g_panelX / CELL, c1 = (g_panelX + PANEL_W) / CELL;
  const int r0 = (g_panelY - MAP_TOP) / CELL, r1 = (g_panelY + PANEL_H - MAP_TOP) / CELL;
  for (int c = c0; c <= c1; ++c) {
    if (c < 0 || c >= COLS) continue;
    for (int r = r0; r <= r1; ++r) {
      if (r < 0 || r >= MAP_ROWS) continue;
      drawCellBase(g, c, r);
    }
  }
  // 范围内（含射程框可能穿过面板的）塔重画
  for (int i = 0; i < MAX_TOWERS; ++i) {
    Tower& t = g_w.towers[i];
    if (!t.active) continue;
    const int tx = t.col * CELL, ty = MAP_TOP + t.row * CELL;
    if (tx < g_panelX + PANEL_W && tx + CELL > g_panelX &&
        ty < g_panelY + PANEL_H && ty + CELL > g_panelY) drawTower(g, t);
  }
  // 射程预览框擦不干净没关系：脏格机制会在任何实体路过时把它恢复
  g_panelOpen = false;
}

// ── 每帧渲染主体：擦脏格 → 塔 → 敌人 → 子弹 → 特效 ──────────────
static void renderFrame(M5GFX* g) {
  // v0.4.2：标题界面是静态屏（真机 loop 在 ST_TITLE 直接 return，永不进来）。
  // 这里是防御性早退：万一别的路径调到 renderFrame，也不会把世界/HUD
  // 画到标题界面上（仿真曾因此把上一局 HUD 混进标题帧截图）。
  if (g_w.state == ST_TITLE) return;
  eraseDirty(g);

  for (int i = 0; i < MAX_TOWERS; ++i) if (g_w.towers[i].active) drawTower(g, g_w.towers[i]);
  for (int i = 0; i < MAX_ENEMIES; ++i) if (g_w.enemies[i].active) drawEnemy(g, g_w.enemies[i]);
  for (int i = 0; i < MAX_BULLETS; ++i) if (g_w.bullets[i].active) drawBullet(g, g_w.bullets[i]);
  for (int i = 0; i < MAX_FX; ++i)     if (g_w.fx[i].active)       drawFx(g, g_w.fx[i]);

  // v0.4.2：SIEGE 封锁区（最上层：压着塔与兵，一眼看见"这里不能建"）
  drawSiege(g);

  // ── 建造栏刷新：选中变化 or 金币跨过可负担档位（不是每帧全刷）──
  int32_t mask = 0;
  for (int i = 0; i < TT_COUNT; ++i) if (g_w.gold >= TOWER_TYPES[i].cost) mask |= (1 << i);
  const int8_t sel = (g_w.buildSel < 0) ? 0 : (int8_t)g_w.buildSel;
  if ((int32_t)s_lastSel != (int32_t)sel || s_lastAfford != mask) {
    drawBuildBar(g);
    s_lastSel = sel; s_lastAfford = mask;
  }

  if (g_w.hudDirty) { drawHud(g); g_w.hudDirty = false; }

  // 按钮只在状态变化时重画：它位于 HUD 带内，不会被脏格擦除
  const bool enabled = (g_w.state == ST_READY);
  if ((int8_t)enabled != s_lastBtn) { drawButton(g, enabled); s_lastBtn = (int8_t)enabled; }
  // v0.2.1：倒计时条每帧刷新（仅波间；自己管槽底重铺，无残留）
  if (enabled && g_w.wave > 0) drawWaveCountdown(g);

  // 升级面板：打开期间世界暂停，选中变化（升级）时重画内容
  if (g_w.selectedTower >= 0) {
    Tower& t = g_w.towers[g_w.selectedTower];
    if (s_lastPanelIdx != g_w.selectedTower || s_lastLv != t.level) {
      drawTowerPanel(g, t);
      s_lastPanelIdx = g_w.selectedTower; s_lastLv = t.level;
    }
  } else {
    if (g_panelOpen) closeTowerPanel(g);
  }
}

// ── 建造反馈：合法=绿闪，非法（路径/占用/缺金币）=红叉 ────────────
static void flashCell(M5GFX* g, int col, int row, bool ok) {
  const int x = col * CELL, y = MAP_TOP + row * CELL;
  if (ok) {
    g->drawRect(x, y, CELL, CELL, C_HP_FULL);
    g->fillRect(x + CELL/2 - 3, y + CELL/2 - 3, 6, 6, C_FX_WHITE);
  } else {
    for (int i = 0; i < CELL - 8; i += 6) {          // X 形拒绝标记
      g->fillRect(x + 4 + i, y + 4 + i, 5, 5, C_BAD);
      g->fillRect(x + CELL - 9 - i, y + 4 + i, 5, 5, C_BAD);
    }
  }
}

// ── Game Over：盖一层半透明感的深色块 + 大字，点击任意处重开 ──────
// v0.4.1：压暗点阵从 2x2@4px 步进（57,600 次 fillRect，约百毫秒卡顿）
// 改 4x4@8px 步进（14,400 次）——25% 覆盖率不变，像素风下肉眼无差。
static void drawGameOver(M5GFX* g) {
  // 用稀疏暗块模拟半透明压暗（无 alpha，保持纯矩形）
  for (int y = MAP_TOP; y < SCREEN_H; y += 8)
    for (int x = 0; x < SCREEN_W; x += 8) g->fillRect(x, y, 4, 4, C_BG);

  const int bw = 760, bh = 300;
  const int bx = (SCREEN_W - bw) / 2, by = MAP_TOP + (SCREEN_H - MAP_TOP - bh) / 2;
  g->fillRect(bx, by, bw, bh, C_UI_BG);
  g->drawRect(bx, by, bw, bh, C_BAD);
  g->drawRect(bx + 3, by + 3, bw - 6, bh - 6, C_BAD);

  px_textCenter(g, SCREEN_W / 2, by + 40, TR(STR_GAME_OVER), C_BAD, 6);

  char buf[32];
  strcpy(buf, TR(STR_WAVE_WORD)); px_itoa(g_w.wave, buf + strlen(TR(STR_WAVE_WORD)));
  px_textCenter(g, SCREEN_W / 2, by + 130, buf, C_TEXT_DIM, 3);

  strcpy(buf, TR(STR_KILLS)); px_itoa(g_w.score, buf + strlen(TR(STR_KILLS)));
  px_textCenter(g, SCREEN_W / 2, by + 170, buf, C_TEXT_DIM, 3);

  px_textCenter(g, SCREEN_W / 2, by + 230, TR(STR_TAP_RESTART), C_BTN_HI, 3);
}

// ── 星星（v0.3.0）：5×7 像素五角星的矩形近似 —— 三块菱形拼出尖角 ──
// size=1 → 约 20px；size=2 → 40px。纯 fillRect，无字体依赖。
static void drawStar(M5GFX* g, int cx, int cy, int size, uint16_t c) {
  const int u = size;                       // 单元像素
  g->fillRect(cx - u / 2, cy - u * 3 / 2, u, u * 3, c);        // 竖干
  g->fillRect(cx - u * 3 / 2, cy - u / 2, u * 3, u, c);         // 横干
  g->fillRect(cx - u / 2, cy - u * 3 / 4, u, u * 3 / 2, c);     // 竖干加厚中段
  g->fillRect(cx - u, cy - u / 4, u * 2, u / 2, c);             // 横干加厚中段
}

// 一行 n 颗星（filled=点亮数），居中于 cx。返回整行宽度
static int drawStarRow(M5GFX* g, int cx, int cy, int n, int filled, int size) {
  const int step = size * 3 + 8;
  const int x0 = cx - (step * n) / 2 + step / 2;
  for (int i = 0; i < n; ++i) {
    const uint16_t c = (i < filled) ? C_STAR : C_STAR_EMPTY;
    drawStar(g, x0 + i * step, cy, size, c);
  }
  return step * n;
}

// ── v0.3.1 静音按钮：喇叭方块+号角口，纯矩形；静音=红斜杠 ─────────
// ⚠️ 必须先于 drawLevelSelect 定义（头文件顺序编译，无原型不可前向调用）
static void drawMuteBtn(M5GFX* g) {
  const int x = MUTE_BTN_X, y = MUTE_BTN_Y;
  g->fillRect(x, y, MUTE_BTN_W, MUTE_BTN_H, C_UI_BG);
  g->drawRect(x, y, MUTE_BTN_W, MUTE_BTN_H, s_muted ? C_BAD : C_GRASS_EDGE);
  const int cx = x + MUTE_BTN_W / 2, cy = y + MUTE_BTN_H / 2;
  const uint16_t c = s_muted ? C_BAD : C_TEXT_DIM;
  g->fillRect(cx - 16, cy - 5, 8, 10, c);          // 喇叭体
  g->fillRect(cx - 8, cy - 10, 12, 20, c);         // 号角
  if (s_muted) {                                     // 红斜杠（阶梯像素线）
    for (int i = -18; i <= 18; i += 4) g->fillRect(cx + i, cy - i / 2 - 1, 4, 3, C_BAD);
  } else {                                           // 两道声波弧（竖条近似）
    g->fillRect(cx + 12, cy - 8, 3, 16, c);
    g->fillRect(cx + 18, cy - 12, 3, 24, c);
  }
}

// ── 单个选关节点（v0.4.1 从 drawLevelSelect 抽出，供锁关反馈自擦复用）──
static void drawSelNode(M5GFX* g, int i) {
  const int x = selNodeX(i), y = selNodeY(i);
  const bool unlocked = levelUnlocked(i);
  const bool cleared  = levelCleared(i);        // v0.2.0：通关位图真源 game.h
  const bool cur = unlocked && !cleared;

  uint16_t bgc  = unlocked ? C_SLOT_BG : (int)C_LOCK;
  uint16_t bord = cleared ? C_GOLD : cur ? C_HP_FULL : C_LOCK;

  g->fillRect(x, y, SEL_NODE_W, SEL_NODE_H, bgc);
  g->drawRect(x, y, SEL_NODE_W, SEL_NODE_H, bord);
  if (cur) g->drawRect(x + 3, y + 3, SEL_NODE_W - 6, SEL_NODE_H - 6, C_HP_FULL);

  char buf[8]; px_itoa(i + 1, buf);
  if (unlocked) {
    // v0.4：5×4=20 节点（170×110），字号/间距按新节点收窄
    px_textCenter(g, x + SEL_NODE_W / 2, y + 14, buf, C_TEXT, 4);
    px_textCenter(g, x + SEL_NODE_W / 2, y + 62, LEVELS[i].name,
                  cleared ? C_GOLD : C_TEXT_DIM, 1);
    // v0.3.0：星级取代 "CLEAR"（未通关=三颗空星，收集进度可视化）
    drawStarRow(g, x + SEL_NODE_W / 2, y + 82, 3, cleared ? g_stars[i] : 0, 7);
  } else {
    // 锁图标：方块+横梁，纯矩形
    const int lx = x + SEL_NODE_W / 2 - 18, ly = y + SEL_NODE_H / 2 - 10;
    g->fillRect(lx, ly + 8, 36, 26, C_TEXT_DIM);
    g->fillRect(lx + 8, ly - 6, 20, 14, C_LOCK);
    g->fillRect(lx + 12, ly - 2, 12, 10, C_BG);
    g->fillRect(lx + 16, ly + 14, 4, 12, C_BG);
  }

  // BOSS 预告点：通关玩家知道每关有王（小方块提示）
  if (unlocked) g->fillRect(x + SEL_NODE_W - 14, y + 8, 6, 6, C_ENEMY_BOSS);
}

// ── 关卡选择（v0.2.0）：5×2 节点图，锁定=灰+锁，通关=金，当前=绿框 ─
static void drawLevelSelect(M5GFX* g) {
  g->fillScreen(C_BG);
  px_textCenter(g, SCREEN_W / 2, 36, TR(STR_SELECT), C_TEXT, 4);

  for (int i = 0; i < LEVEL_COUNT; ++i) drawSelNode(g, i);

  px_textCenter(g, SCREEN_W / 2, selNodeY(LEVEL_COUNT - 1) + SEL_NODE_H + 16,
                TR(STR_UNLOCK_HINT), C_TEXT_DIM, 2);

  // v0.3.1：右下角静音开关（喇叭+斜杠=静音；选中态红圈）
  drawMuteBtn(g);
}

// ── 通关画面（v0.2.0）：金色战报，点击回选关 ─────────────────────
static void drawLevelClear(M5GFX* g) {
  for (int y = MAP_TOP; y < SCREEN_H; y += 8)
    for (int x = 0; x < SCREEN_W; x += 8) g->fillRect(x, y, 4, 4, C_BG);

  const int bw = 760, bh = CLEAR_PANEL_H;
  const int bx = (SCREEN_W - bw) / 2, by = CLEAR_PANEL_Y;
  g->fillRect(bx, by, bw, bh, C_UI_BG);
  g->drawRect(bx, by, bw, bh, C_GOLD);
  g->drawRect(bx + 3, by + 3, bw - 6, bh - 6, C_GOLD);

  px_textCenter(g, SCREEN_W / 2, by + 30, TR(STR_LEVEL_CLEAR), C_GOLD, 6);
  px_textCenter(g, SCREEN_W / 2, by + 108, LV().name, C_TEXT, 3);

  // v0.3.0：三颗大星居中（本次评级点亮，历史最高见选关界面）
  drawStarRow(g, SCREEN_W / 2, by + 160, 3, g_stars[g_lv], 14);

  char buf[64];
  strcpy(buf, TR(STR_BOSS_DOWN)); strncat(buf, LV().boss_name,
          sizeof(buf) - strlen(buf) - 1);
  px_textCenter(g, SCREEN_W / 2, by + 196, buf, C_ENEMY_BOSS, 2);

  strcpy(buf, TR(STR_KILLS)); px_itoa(g_w.score, buf + strlen(TR(STR_KILLS)));
  strcat(buf, " "); strcat(buf, TR(STR_GOLD_WORD));
  char g2[8]; px_itoa(g_w.gold, g2); strcat(buf, g2);
  px_textCenter(g, SCREEN_W / 2, by + 230, buf, C_TEXT_DIM, 2);

  // v0.4.2：L10 通关后加"无尽模式"入口（引擎早已就绪，只缺这个按钮）
  if (g_lv + 1 >= LEVEL_COUNT) {
    const int ex = ENDLESS_BTN_X, ey = ENDLESS_BTN_Y;
    g->fillRect(ex, ey, ENDLESS_BTN_W, ENDLESS_BTN_H, C_SLOT_BG);
    g->drawRect(ex, ey, ENDLESS_BTN_W, ENDLESS_BTN_H, C_HP_FULL);
    g->drawRect(ex + 3, ey + 3, ENDLESS_BTN_W - 6, ENDLESS_BTN_H - 6, C_HP_FULL);
    px_textCenter(g, ex + ENDLESS_BTN_W / 2, ey + 14, TR(STR_ENDLESS), C_HP_FULL, 3);
    px_textCenter(g, ex + ENDLESS_BTN_W / 2, ey + 46, TR(STR_ENDLESS_DESC), C_TEXT_DIM, 1);
  }

  px_textCenter(g, SCREEN_W / 2, by + 270,
                (g_lv + 1 < LEVEL_COUNT) ? TR(STR_TAP_NEXT) : TR(STR_ALL_CLEAR),
                C_BTN_HI, 3);
}

// ── v0.4.2 标题界面 ───────────────────────────────────────────────
// 开机第一屏：像素大标题 + 版本 + 语言选择（EN/中文，NVS 记忆）+ 点击开始。
// 语言按钮几何（render 画 / actions 命中，唯一真源同 MUTE_BTN 惯例）
#define LANG_BTN_Y  330
#define LANG_BTN_H  56
#define LANG_EN_X   430
#define LANG_CN_X   650
#define LANG_BTN_W  200
// v0.4.3：LANGUAGE/语言 标签居中在按钮组上方。y 命名化，仿真断言守着
// "标签底边（含 16px 中文点阵）不得贴到按钮顶边"。
#define TITLE_LANG_LBL_Y 302

// v0.4.2：无尽模式入口按钮（L10 通关画面面板下方；actions 命中共用）
// （几何宏已前置到文件头，供 drawLevelClear 使用）

// 彩虹像素字：逐字母循环取色（先画 6px 黑色投影，再画彩色本体）
static void drawRainbow(M5GFX* g, int cx, int y, const char* s, int scale,
                        const uint16_t* cols, int ncol) {
  const int adv = (FONT_W + 1) * scale;
  const int w = px_textWidth(s, scale);
  int x = cx - w / 2, i = 0;
  px_textCenter(g, cx + 6, y + 6, s, C_BG, scale);       // 投影（右下 6px）
  while (*s) {
    if ((uint8_t)*s >= 0x80) {                           // 中文 16px：单色金
      px_cnChar(g, x, y + (FONT_H * scale - CN_FONT_H) / 2, s, C_GOLD);
      s += 3; x += CN_ADV; continue;
    }
    px_char(g, x, y, *s++, cols[i % ncol], scale);
    x += adv; ++i;
  }
}

static void drawTitle(M5GFX* g) {
  g->fillScreen(C_BG);

  // 像素星空：确定性伪随机点（坐标哈希，不用随机库——构建可复现）
  for (int i = 0; i < 90; ++i) {
    const int x = (i * 137 + 61) % SCREEN_W;
    const int y = (i * 89 + 23) % (int)(MAP_TOP * 3);
    const uint16_t c = (i % 5 == 0) ? C_GOLD : (i % 3 == 0 ? C_TEXT_DIM : C_GRASS_EDGE);
    g->fillRect(x, y, 3, 3, c);
  }

  // 大标题：两行彩虹像素字（英文专名不翻译；中文模式加一行"像素塔防"）
  static const uint16_t RC[8] = { C_BAD, C_GOLD, C_HP_FULL, C_TOWER_FRO,
                                  C_TEXT, C_ENEMY_BOSS, C_TOWER_CAN, C_TOWER_LASER };
  drawRainbow(g, SCREEN_W / 2, 110, "PIXEL TOWER", 7, RC, 8);
  drawRainbow(g, SCREEN_W / 2, 180, "DEFENSE", 7, RC, 8);
  // v0.4.3：中文标题必须走 TR(STR_TITLE_CN)——硬编码中文不进 STR_CN 表，
  // gen_cnfont 就收不到字（v0.4.2 曾因此缺 3 字）。y=245：与版本号留 11px。
  if (langCN()) px_textCenter(g, SCREEN_W / 2, 245, TR(STR_TITLE_CN), C_GOLD, 2);

  // 版本号（金色，v0.4.3 起每次发版手改这里 + banner + publish.yaml）
  char vb[24]; strcpy(vb, "v0.4.3");
  px_textCenter(g, SCREEN_W / 2, 272, vb, C_GOLD, 3);

  // 语言按钮：当前语言金色高亮
  for (int i = 0; i < LANG_COUNT; ++i) {
    const int bx = (i == LANG_EN) ? LANG_EN_X : LANG_CN_X;
    const bool cur = ((int)g_lang == i);
    g->fillRect(bx, LANG_BTN_Y, LANG_BTN_W, LANG_BTN_H, C_SLOT_BG);
    g->drawRect(bx, LANG_BTN_Y, LANG_BTN_W, LANG_BTN_H, cur ? C_GOLD : C_GRASS_EDGE);
    if (cur) g->drawRect(bx + 3, LANG_BTN_Y + 3, LANG_BTN_W - 6, LANG_BTN_H - 6, C_GOLD);
    // v0.4.3：按钮标签走 TR——显示"目标语言"名（EN 模式：ENGLISH/中文；
    // CN 模式：英文/中文）。硬编码会让字库漏收（见 i18n.h 头注教训）。
    const char* label = (i == LANG_EN) ? TR(STR_LANG_EN) : TR(STR_LANG_CN);
    px_textCenter(g, bx + LANG_BTN_W / 2, LANG_BTN_Y + 20, label,
                  cur ? C_TEXT : C_TEXT_DIM, 2);
  }
  // v0.4.3：LANGUAGE 标签从"按钮左侧硬编码 x=360"改为**按钮组上方居中**
  // ——旧版 8 字符×12px=94px 伸到 456，压进 ENGLISH 按钮（430）26px。
  // 居中放置同时解决"按钮位置随语言宽度跳动"（固定左缘的方案会跳）。
  px_textCenter(g, SCREEN_W / 2, TITLE_LANG_LBL_Y, TR(STR_LANG), C_TEXT_DIM, 2);

  // 装饰带：几座塔剪影 + 小兵 + 地面线（全部矩形，复用塔的配色语言）
  const int gy = 500;
  g->fillRect(0, gy + 44, SCREEN_W, 4, C_GRASS_EDGE);
  for (int i = 0; i < 5; ++i) {                          // 塔剪影（间隔排布）
    const int tx = 300 + i * 170, ty = gy + 10;
    const uint16_t col = towerColor(i == 4 ? 4 : i % 4);
    g->fillRect(tx + 8, ty + 18, 24, 22, C_TOWER_BASE);
    g->fillRect(tx + 12, ty + 6, 16, 20, col);
    if (i % 4 == 1) g->fillRect(tx + 22, ty + 10, 12, 5, col);      // 炮管
    else if (i % 4 == 2) { g->fillRect(tx + 15, ty, 5, 28, col);
                           g->fillRect(tx + 8, ty + 10, 20, 5, col); }
    else g->fillRect(tx + 17, ty, 5, 26, col);                       // 枪管
  }
  for (int i = 0; i < 4; ++i) {                          // 小兵剪影（从右向左）
    const int ex = 1120 - i * 90, ey = gy + 22;
    g->fillRect(ex, ey, 18, 18, i == 3 ? C_ENEMY_BOSS : (i & 1 ? C_ENEMY_A : C_ENEMY_B));
    g->drawRect(ex, ey, 18, 18, C_BG);
    g->fillRect(ex + 4, ey + 5, 4, 4, C_BG);
    g->fillRect(ex + 10, ey + 5, 4, 4, C_BG);
    if (i == 3) { g->fillRect(ex - 4, ey - 6, 6, 6, C_ENEMY_BOSS);   // 王冠尖
                  g->fillRect(ex + 16, ey - 6, 6, 6, C_ENEMY_BOSS); }
  }

  // 开始提示。**只画一次**。
  // ⚠️ v0.4.3 修过这里，两件事都别再犯：
  //   ① 原来同一条文字画了两遍做"双色叠字/描边"，但暗层是后画的且只左移 3px
  //      （字宽 15px）→ 整个字身被盖成暗色、只在右缘漏 3px，玩家看到的是
  //      "字被画了两次、错位重叠"。
  //   ② 把顺序翻过来（暗在下、亮在上）**也不行**：偏移 3px ≈ 英文笔画宽 3px，
  //      亮层笔画正好落进暗层笔画的缝隙，两层互相穿插，依然是糊的重影。
  //      5x7 字模放大到 15px 后，笔画间距本来就 3px 量级——**这个尺寸下没有
  //      能做描边的余量**。星空是深底，亮色本身对比度已经够，不需要描边。
  px_textCenter(g, SCREEN_W / 2, 580, TR(STR_TITLE_HINT), C_BTN_HI, 3);

  // 底部：仓库地址（开源项目惯例）
  px_textCenter(g, SCREEN_W / 2, SCREEN_H - 26,
                "github.com/andjiang0083/pixel-tower-defense", C_TEXT_DIM, 1);
}

// ── v0.4.2 首次引导 overlay：三步教学（actions.h 推进）────────────
// NVS "onboarded" 记住已完成；之后永不弹出（老玩家不被教程骚扰）。
static void drawOnboard(M5GFX* g, int step) {
  const char* s = (step == 1) ? TR(STR_ONBOARD_1) :
                  (step == 2) ? TR(STR_ONBOARD_2) : TR(STR_ONBOARD_3);
  const int bw = 820, bh = 130;
  const int bx = (SCREEN_W - bw) / 2, by = SCREEN_H - bh - 150;
  // 半透明感压暗（稀疏暗块，同 Game Over 手法）
  // ⚠️ v0.4.3：网格 8px → 16px（块 4×4 → 8×8，覆盖率同为 25%，观感几乎一致）。
  //   8px 时是 160×90 = **14400 次 fillRect**，单次 overlay 绘制就要几十毫秒；
  //   叠加"每帧无条件重画"（见 main.cpp loop）后，PSRAM 写入风暴会与 DSI 扫描
  //   DMA 抢 cache，死锁在 esp_cache_msync 的自旋锁上 → task_wdt panic 蓝屏。
  for (int y = 0; y < SCREEN_H; y += 16)
    for (int x = 0; x < SCREEN_W; x += 16) g->fillRect(x, y, 8, 8, C_BG);
  g->fillRect(bx, by, bw, bh, C_UI_BG);
  g->drawRect(bx, by, bw, bh, C_BTN_HI);
  g->drawRect(bx + 3, by + 3, bw - 6, bh - 6, C_BTN_HI);
  px_textCenter(g, SCREEN_W / 2, by + 26, s, C_TEXT, 2);
  px_textCenter(g, SCREEN_W / 2, by + 74, TR(STR_ONBOARD_TAP), C_BTN_HI, 2);
}

// ── v0.4.2 SIEGE 封锁区渲染：闪烁红框 + 井字封号 ──────────────────
// 几何与 game.h siegeBlocksPx 同源（g_w.siegeX/Y/R）。
static void drawSiege(M5GFX* g) {
  if (g_w.siegeMs == 0) return;
  const int x0 = (int)g_w.siegeX - g_w.siegeR, y0 = (int)g_w.siegeY - g_w.siegeR;
  const int w = g_w.siegeR * 2;
  const bool blink = (millis() / 200) & 1;              // 5Hz 危险闪烁
  const uint16_t c = blink ? C_BAD : C_WARN;
  g->drawRect(x0, y0, w, w, c);
  g->drawRect(x0 + 4, y0 + 4, w - 8, w - 8, c);
  for (int i = 0; i < 36; i += 6) {                     // 四角加粗（像素风）
    g->fillRect(x0 - 2, y0 + i, 6, 6, c);
    g->fillRect(x0 + w - 4, y0 + i, 6, 6, c);
    g->fillRect(x0 + i, y0 - 2, 6, 6, c);
    g->fillRect(x0 + i, y0 + w - 4, 6, 6, c);
  }
  const int cx = (int)g_w.siegeX, cy = (int)g_w.siegeY;  // 井字 = 禁止建造
  g->drawRect(cx - 16, cy - 16, 32, 32, c);
  g->fillRect(cx - 2, cy - 14, 4, 28, c);
  g->fillRect(cx - 14, cy - 2, 28, 4, c);
}
