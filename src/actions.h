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
#include "render.h"         // drawBackground / flashCell / drawGameOver / panel

// 建造反馈：绿闪/红叉，若干帧后自行擦除
struct BuildFx { int16_t col, row; uint8_t lifeFrames; bool ok; };
static BuildFx g_buildFx = {-1, -1, 0, false};

// Game Over 覆盖层只在进入时画一次（每帧压暗全屏太贵）
static bool g_overDrawn = false;
static bool g_clearDrawn = false;   // 通关画面同理

// 进度落盘钩子：main.cpp 注入 NVS 写入（仿真里为空实现）
static void (*g_saveProgress)(void) = nullptr;

// ── 重开/初始化后的强制 UI 全量刷新（renderFrame 的缓存归零）────
static void refreshAllUI(M5GFX* d) {
  resetRenderCaches();          // render.h：建造栏/按钮静态缓存失效
  drawBackground(d);
  drawBuildBar(d);
  drawHud(d);       g_w.hudDirty = false;
  drawButton(d, true);
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
static bool enterLevel(M5GFX* d, int idx) {
  if (idx < 0 || idx >= LEVEL_COUNT || !levelUnlocked(idx)) return false;
  levelLoad(idx);
  g_buildFx = {-1, -1, 0, false};
  g_overDrawn = false;
  g_clearDrawn = false;
  closeTowerPanel(d);
  refreshAllUI(d);
  return true;
}

// ── 一次点击的全部规则。返回 true = 世界状态发生了变化 ─────────────
static bool handleTap(M5GFX* d, const Tap& t) {
  if (!t.valid) return false;

  // ── 选关界面：静音开关优先（角落小按钮，防误触进错关）──────────
  if (g_w.state == ST_SEL) {
    if (t.x >= MUTE_BTN_X && t.x < MUTE_BTN_X + MUTE_BTN_W &&
        t.y >= MUTE_BTN_Y && t.y < MUTE_BTN_Y + MUTE_BTN_H) {
      sfxSetMuted(!s_muted);                 // v0.3.1：点一下静音/恢复
      if (g_saveProgress) g_saveProgress(); // 状态立刻落盘（含下方 NVS key）
      drawLevelSelect(d);                    // 整屏重画=按钮状态刷新（静态界面便宜）
      return false;
    }
    int node = hitSelNode(t.x, t.y);
    if (node < 0) return false;
    if (!levelUnlocked(node)) {          // 锁关反馈：节点内红叉
      const int nx = selNodeX(node), ny = selNodeY(node);
      for (int i = 0; i < SEL_NODE_H - 40; i += 10) {
        d->fillRect(nx + SEL_NODE_W/2 - 30 + i, ny + SEL_NODE_H/2 - 30 + i, 8, 8, C_BAD);
        d->fillRect(nx + SEL_NODE_W/2 + 30 - i, ny + SEL_NODE_H/2 - 30 + i, 8, 8, C_BAD);
      }
      return false;
    }
    return enterLevel(d, node);
  }

  // ── 通关画面：点任意处 → 下一关（末关回选关）──────────────────
  if (g_w.state == ST_CLEAR) {
    if (g_saveProgress) g_saveProgress();          // 解锁进度落盘
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
static void updateBuildFx(M5GFX* d) {
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

// ── Game Over / Level Clear：只在第一次进入时画覆盖层 ─────────────
static void updateOverlays(M5GFX* d) {
  if (g_w.state == ST_GAMEOVER && !g_overDrawn) { drawGameOver(d); g_overDrawn = true; }
  if (g_w.state == ST_CLEAR  && !g_clearDrawn) { drawLevelClear(d); g_clearDrawn = true; }
}
