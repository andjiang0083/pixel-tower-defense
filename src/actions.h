/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 andjiang0083
 */
// actions.h — 触摸事件 → 游戏动作的派发（唯一消费点）
// v0.2.0 点击优先级：选关界面 > Game Over / Level Clear >
//   升级面板按钮/关闭 > NEXT WAVE > 建造栏选塔种 > 已建塔(开面板) > 地图空地(建塔)
// 从 main.cpp 抽出来，是为了让 PC 仿真能跑到**同一份**判定代码。
#pragma once
#include "config_gen.h"
#include "touch.h"          // Tap / pxToCell / hitButton / hitSlot
#include "game.h"           // buildTower/upgradeTower/sellTower/startNextWave/levelLoad
#include "progress.h"       // v0.4.1：g_saveProgress 钩子 + 落盘去重（含仿真断言）
#include "render.h"         // drawBackground / flashCell / drawGameOver / panel

// 建造反馈：绿闪/红叉，若干帧后自行擦除
struct BuildFx { int16_t col, row; uint8_t lifeFrames; bool ok; };
static BuildFx g_buildFx = {-1, -1, 0, false};

// 选关界面锁定反馈（v0.4.1 修：旧实现画完红叉永留，只有整屏重画才擦得掉）
struct SelFx { int8_t node; uint8_t lifeFrames; };
static SelFx g_selFx = {-1, 0};

// Game Over 覆盖层只在进入时画一次（每帧压暗全屏太贵）
static bool g_overDrawn = false;
static bool g_clearDrawn = false;   // 通关画面同理

// v0.4.2：首次引导 overlay（三步教学；NVS "onboarded" 记住只讲一次）
static int8_t g_onboardStep = 0;    // 0=关闭；1..3=第几步
static bool   g_onboarded  = false;
// 引导对话框几何（render.h drawOnboard 同源，命中判定共用）
// v0.5.1：宽度随屏宽收缩（config_gen.h 的 ONBOARD_W），不再写死 820。
//   720 屏上 820 宽的框左右各出屏 50px（(720-820)/2 = -50），
//   玩家点最外侧那部分根本不在框内 → 教程推不动。
#define ONBOARD_X ((SCREEN_W - ONBOARD_W) / 2)
#define ONBOARD_Y (SCREEN_H - ONBOARD_H - 150)

// ── 重开/初始化后的强制 UI 全量刷新（renderFrame 的缓存归零）────
static void refreshAllUI(Gfx* d) {
  resetRenderCaches();          // render.h：建造栏/按钮静态缓存失效
  drawBackground(d);
  drawBuildBar(d);
  drawHud(d);       g_w.hudDirty = false;
  drawButton(d, true);
}

// ── v0.4.3：整屏重建（背景/建造栏/HUD/按钮 + 全部实体）────────────
// 为什么需要它：引导 overlay 是**画在已有画面之上**的（压暗块 + 对话框），
// 而脏格机制只擦"有实体走过的格子"。所以 overlay 一旦画上，
// 覆盖区域内**没有实体经过的地方就再也擦不掉**——玩家看到的就是
// 教程结束后画面残留一块，走到哪都不刷新。
// → 关闭 overlay（step 归零）时必须整屏重建，把压暗块一次性清掉。
static void redrawAll(Gfx* d) {
  refreshAllUI(d);
  // refreshAllUI 只画静态 UI，实体要补画（与 renderFrame 的顺序一致）
  for (int i = 0; i < MAX_TOWERS; ++i)  if (g_w.towers[i].active)  drawTower(d, g_w.towers[i]);
  for (int i = 0; i < MAX_ENEMIES; ++i) if (g_w.enemies[i].active) drawEnemy(d, g_w.enemies[i]);
  for (int i = 0; i < MAX_BULLETS; ++i) if (g_w.bullets[i].active) drawBullet(d, g_w.bullets[i]);
  for (int i = 0; i < MAX_FX; ++i)       if (g_w.fx[i].active)     drawFx(d, g_w.fx[i]);
}

// ── 选关节点命中（几何真源 config_gen.h selNodeX/Y）───────────────
static int hitSelNode(int x, int y) {
  for (int i = 0; i < LEVEL_COUNT; ++i) {
    const int nx = selNodeX(i), ny = selNodeY(i);
    if (x >= nx && x < nx + SEL_NODE_W && y >= ny && y < ny + SEL_NODE_H) return i;
  }
  return -1;
}

// ── 进入某关（解锁校验在这里，唯一入口）──────────────────────────
static bool enterLevel(Gfx* d, int idx) {
  if (idx < 0 || idx >= LEVEL_COUNT || !levelUnlocked(idx)) return false;
  levelLoad(idx);
  g_buildFx = {-1, -1, 0, false};
  g_selFx = {-1, 0};                 // v0.4.1：选关反馈不跨关残留
  g_overDrawn = false;
  g_clearDrawn = false;
  closeTowerPanel(d);
  refreshAllUI(d);
  // v0.4.2：第一次进 L1 弹引导（之后 NVS 记住，老玩家不被教程骚扰）
  if (!g_onboarded && idx == 0) g_onboardStep = 1;
  return true;
}

// ── v0.4.2：进入无尽模式（L10 通关画面入口）──────────────────────
// ⚠️ g_w.endless 必须在 enterLevel 之后设：levelLoad 会 memset 清零它。
static bool enterEndless(Gfx* d) {
  if (!enterLevel(d, LEVEL_COUNT - 1)) return false;
  g_w.endless = 1;
  g_w.gold = START_GOLD;             // 标准经济起手（不搞 Infinite 宽容）
  return true;
}

// ── 一次点击的全部规则。返回 true = 世界状态发生了变化 ─────────────
static bool handleTap(Gfx* d, const Tap& t) {
  if (!t.valid) return false;

  // ── v0.4.2：引导 overlay 打开时，对话框内点击=推进下一步 ────────
  // （对话框外点击放行给正常操作：step1 就是让玩家真的去点草地）
  if (g_onboardStep > 0 &&
      t.x >= ONBOARD_X && t.x < ONBOARD_X + ONBOARD_W &&
      t.y >= ONBOARD_Y && t.y < ONBOARD_Y + ONBOARD_H) {
    if (++g_onboardStep > 3) {                 // 第三步点完=出师
      g_onboardStep = 0;
      g_onboarded = true;
      progressSaveNow();                       // onboarded 落盘，永不重弹
      redrawAll(d);                            // v0.4.3：整屏重建，清掉 overlay 残留
    }
    return false;
  }

  // ── v0.4.2：标题界面——点击任意处开始 ────────────────────────
  // v0.5.1：语言按钮随中文支持一起删掉了（v0.5.0 起只有英文一种语言），
  //   命中分支与 drawTitle 里的按钮是同一份几何，删一处即可，不留孤儿。
  if (g_w.state == ST_TITLE) {
#if PTD_HAS_CJK
    // ── 语言按钮优先（v0.5.3 回归）：点了就切语言 + 立刻落盘 + 整屏重画 ──
    if (t.y >= LANG_BTN_Y && t.y < LANG_BTN_Y + LANG_BTN_H) {
      if (t.x >= LANG_EN_X && t.x < LANG_EN_X + LANG_BTN_W) {
        setLang(LANG_EN); drawTitle(d); progressSaveNow(); return false;
      }
      if (t.x >= LANG_CN_X && t.x < LANG_CN_X + LANG_BTN_W) {
        setLang(LANG_CN); drawTitle(d); progressSaveNow(); return false;
      }
    }
#endif
    g_w.state = ST_SEL;
    drawLevelSelect(d);
    return true;
  }

  // ── 选关界面：静音开关优先（角落小按钮，防误触进错关）──────────
  if (g_w.state == ST_SEL) {
    if (t.x >= MUTE_BTN_X && t.x < MUTE_BTN_X + MUTE_BTN_W &&
        t.y >= MUTE_BTN_Y && t.y < MUTE_BTN_Y + MUTE_BTN_H) {
      sfxSetMuted(!s_muted);                 // v0.3.1：点一下静音/恢复
      progressSaveNow();                     // 状态立刻落盘（v0.4.1：同步去重缓存）
      drawLevelSelect(d);                    // 整屏重画=按钮状态刷新（静态界面便宜）
      return false;
    }
    int node = hitSelNode(t.x, t.y);
    if (node < 0) return false;
    if (!levelUnlocked(node)) {          // 锁关反馈：节点内红叉（v0.4.1：20帧后自擦）
      g_selFx = { (int8_t)node, 20 };
      return false;
    }
    return enterLevel(d, node);
  }

  // ── 通关画面：点任意处 → 下一关（末关=无尽入口或回选关）────────
  if (g_w.state == ST_CLEAR) {
    progressSaveNow();                       // v0.4.1：确保进度已落盘再跳关
    // v0.4.2：L10 通关画面的无尽模式入口（引擎早已就绪，只缺这个按钮）
    if (g_lv + 1 >= LEVEL_COUNT &&
        t.x >= ENDLESS_BTN_X && t.x < ENDLESS_BTN_X + ENDLESS_BTN_W &&
        t.y >= ENDLESS_BTN_Y && t.y < ENDLESS_BTN_Y + ENDLESS_BTN_H) {
      return enterEndless(d);
    }
    if (g_lv + 1 < LEVEL_COUNT) return enterLevel(d, g_lv + 1);
    g_w.state = ST_SEL;
    drawLevelSelect(d);
    return true;
  }

  // ── Game Over：点屏幕任意处重开本关 ────────────────────────────
  if (g_w.state == ST_GAMEOVER) {
    return enterLevel(d, g_lv);
  }

  // ── 升级面板打开中：世界暂停，只响应面板 ───────────────────────
  if (g_w.selectedTower >= 0) {
    Tower& tw = g_w.towers[g_w.selectedTower];

    int ux, uy, sx, sy;
    panelBtnRects(&ux, &uy, &sx, &sy);

    if (t.x >= ux && t.x < ux + PBTN_W && t.y >= uy && t.y < uy + PBTN_H) {
      if (upgradeTower(g_w.selectedTower)) {   // 升级成功：面板内容重画
        drawTowerPanel(d, tw);
        return true;
      }
      flashCell(d, tw.col, tw.row, false);     // 金币不足：原地红叉提示
      return false;
    }
    if (t.x >= sx && t.x < sx + PBTN_W - 40 && t.y >= sy && t.y < sy + PBTN_H) {
      sellTower(g_w.selectedTower);            // 卖出：关面板 + 补画地图
      g_w.selectedTower = -1;
      closeTowerPanel(d);
      return true;
    }
    // 面板外任意点击 = 关闭面板，世界恢复运行（不建塔，防误触）
    g_w.selectedTower = -1;
    closeTowerPanel(d);
    return false;
  }

  // NEXT WAVE：优先于地图，避免与 HUD 网格混淆
  if (hitButton(t.x, t.y)) {
    if (g_w.state == ST_READY) { startNextWave(); return true; }
    return false;                       // 波次进行中：按钮无效，不扣金币
  }

  // ── 建造栏：选择要造的塔种（不花钱，只改指针）──────────────────
  int slot = hitSlot(t.x, t.y);
  if (slot >= 0) { g_w.buildSel = slot; return false; }

  // ── 地图：点已建塔=开面板；点空地=按选中塔种建造 ───────────────
  int col, row;
  if (!pxToCell(t.x, t.y, &col, &row)) return false;

  const int tw = findTowerAt(col, row);
  if (tw >= 0) {                                // 已有塔 → 打开升级面板（暂停世界）
    g_w.selectedTower = tw;
    drawTowerPanel(d, g_w.towers[tw]);
    return false;
  }

  const uint8_t type = (g_w.buildSel < 0) ? 0 : (uint8_t)g_w.buildSel;
  g_buildFx = { (int16_t)col, (int16_t)row, 8, buildTower(col, row, type) };
  return g_buildFx.ok;
}

// ── 每帧推进建造反馈动画；结束后把该格擦回底色（有塔则连塔重画）──
static void updateBuildFx(Gfx* d) {
  if (g_buildFx.lifeFrames == 0) return;

  flashCell(d, g_buildFx.col, g_buildFx.row, g_buildFx.ok);
  if (--g_buildFx.lifeFrames) return;

  drawCellBase(d, g_buildFx.col, g_buildFx.row);
  for (int i = 0; i < MAX_TOWERS; ++i) {
    Tower& tw = g_w.towers[i];
    if (tw.active && tw.col == g_buildFx.col && tw.row == g_buildFx.row)
      drawTower(d, tw);                        // 别把刚建的塔一起擦掉
  }
}

// ── 选关锁定反馈：红叉闪 20 帧后重画该节点自擦（v0.4.1 修残留）────
static void updateSelFx(Gfx* d) {
  if (g_selFx.lifeFrames == 0) return;
  if (--g_selFx.lifeFrames == 0) {             // 收尾：节点原样重画，红叉无痕
    drawSelNode(d, g_selFx.node);
    g_selFx.node = -1;
    return;
  }
  const int nx = selNodeX(g_selFx.node), ny = selNodeY(g_selFx.node);
  for (int i = 0; i < SEL_NODE_H - 40; i += 10) {
    d->fillRect(nx + SEL_NODE_W/2 - 30 + i, ny + SEL_NODE_H/2 - 30 + i, 8, 8, C_BAD);
    d->fillRect(nx + SEL_NODE_W/2 + 30 - i, ny + SEL_NODE_H/2 - 30 + i, 8, 8, C_BAD);
  }
}

// ── Game Over / Level Clear：只在第一次进入时画覆盖层 ─────────────
static void updateOverlays(Gfx* d) {
  if (g_w.state == ST_GAMEOVER && !g_overDrawn) { drawGameOver(d); g_overDrawn = true; }
  if (g_w.state == ST_CLEAR  && !g_clearDrawn) { drawLevelClear(d); g_clearDrawn = true; }
}
