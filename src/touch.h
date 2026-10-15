/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 andjiang0083
 */
// touch.h — 触摸命中判定（平台无关）+ 采样策略（平台相关）
//
// ── v0.5.0（Claw4 移植）：拆成"平台无关的命中判定"与"平台相关的采样" ────
// 原文件把两者混在一起：`hitButton/hitSlot/pxToCell`（纯几何，跨平台通用）
// 与 `readTap()`（直接调 M5.Touch，Tab5 专属）。后果是 actions.h 想用前三个
// 纯函数，就必须连带把 M5Unified 拖进来——换平台时编译立刻炸在
// `#include <M5Unified.h>` 上，与调用方实际用到的能力完全无关。
//
// 现在：
//   · 命中判定（hitButton / hitSlot / pxToCell）留在本文件，**零平台依赖**
//   · readTap() 的实现按平台分块：Arduino 走 M5.Touch，
//     其他平台由调用方自己实现（Claw4 见 main/main.cpp 的 tapRead()）
//
// 关键设计约束不变：非阻塞。塔防世界必须持续运转，
// 绝不能因为手指压着屏幕而卡住主循环。判定方式 = tap-on-release：
// 记下按下点，抬手时若位移很小才算一次点击。
#pragma once
#include "config_gen.h"    // MAP_TOP / SCREEN_W / COLS / BTN_*（本文件直接用）

struct Tap {
  bool valid;    // 本次调用是否产生了一次有效点击
  int16_t x, y;  // 屏幕像素坐标（M5GFX 已按旋转/校准输出为逻辑坐标）
};

// ── NEXT WAVE 按钮命中区（几何真源在 config_gen.h）───────────────
static inline bool hitButton(int x, int y) {
  return x >= BTN_X && x < BTN_X + BTN_W && y >= BTN_Y && y < BTN_Y + BTN_H;
}

// ── 建造栏槽位命中（v0.1.2）：返回塔种下标，-1 = 不在槽位区 ──────
static inline int hitSlot(int x, int y) {
  if (y < BUILD_BAR_Y + 4 || y >= BUILD_BAR_Y + 4 + SLOT_H) return -1;
  for (int i = 0; i < TT_COUNT; ++i) {
    const int sx = SLOT_X0 + i * (SLOT_W + SLOT_GAP);
    if (x >= sx && x < sx + SLOT_W) return i;
  }
  return -1;
}

// ── 地图区命中：建造栏以上才算格子（防止误触槽位时顺手建塔）──────
static inline bool pxToCell(int x, int y, int* col, int* row) {
  if (y < MAP_TOP || x < 0 || x >= SCREEN_W || y >= BUILD_BAR_Y) return false;
  int c = x / CELL, r = (y - MAP_TOP) / CELL;
  if (c < 0 || c >= COLS || r < 0 || r >= MAP_ROWS) return false;
  *col = c; *row = r; return true;
}

// ── 抬指边沿检测：每帧调用一次，只在"按下→抬起"那一帧返回 valid ──
// v0.5.0：仅 Arduino/Tab5 平台编译。Claw4 与 PC 仿真各自实现采样
//（Claw4 见 main/main.cpp 的 tapRead()，PC 见 sim/compat/M5Unified.h 的 FakeTouch），
// 命中判定部分（上方）三个平台共用同一份实现。
#if defined(PTD_FAKE_TOUCH)
// ── PC 仿真：用 FakeTouch（M5.Touch.getCount/getDetail 的替身）──────────
// v0.5.0：readTap 从"无条件编译"改为"按平台条件编译"。PC 仿真侧通过
//   PTD_FAKE_TOUCH 打开——它依赖 sim/compat/M5Unified.h 提供的 FakeTouch，
//   与真机侧（M5.Touch 或 Claw4 的 GT911）走同一个 tap-on-release 状态机，
//   因此 PC 上测到的点击行为对真机仍然成立。
#include <M5Unified.h>
static inline Tap readTap(void) {
  static bool pressed = false;
  static int16_t px = 0, py = 0;
  static uint32_t downMs = 0;
  Tap t{false, -1, -1};
  M5.update();
  if (M5.Touch.getCount() > 0) {
    auto d = M5.Touch.getDetail();
    if (!pressed) { pressed = true; px = d.x; py = d.y; downMs = millis(); }
    return t;                                   // 还按着：不产生点击
  }
  if (!pressed) return t;
  pressed = false;
  auto d = M5.Touch.getDetail();                // 抬手后 detail 保留最后坐标
  int dx = d.x - px, dy = d.y - py;
  if (dx*dx + dy*dy > 24*24) return t;          // 大幅拖动 = 误触
  if (millis() - downMs > 1500) return t;       // 长按无响应
  t.valid = true; t.x = px; t.y = py;
  return t;
}
#elif defined(ARDUINO)
// ── Arduino / Tab5：M5.Touch（GT911 / ST7123，经 M5GFX 抽象）────────────
#include <M5Unified.h>
static inline Tap readTap(void) {
  static bool pressed = false;   // 上一帧是否有手指
  static int16_t px = 0, py = 0; // 按下时的位置
  static uint32_t downMs = 0;

  Tap t{false, -1, -1};
  M5.update();                                   // 驱动底层触摸采样

  if (M5.Touch.getCount() > 0) {
    auto d = M5.Touch.getDetail();
    if (!pressed) { pressed = true; px = d.x; py = d.y; downMs = millis(); }
    return t;                                  // 还按着：不产生点击，世界继续跑
  }

  if (!pressed) return t;                    // 本来就没按：无事发生
  pressed = false;

  // 抬手：以按下点为判定点（手指微动 <24px 视为原地点击）
  auto d = M5.Touch.getDetail();            // 抬起后 detail 保留最后坐标
  int dx = d.x - px, dy = d.y - py;
  if (dx*dx + dy*dy > 24*24) return t;     // 大幅拖动 = 误触，丢弃
  if (millis() - downMs > 1500) return t;  // 长按不响应（本作没有长按语义）

  t.valid = true; t.x = px; t.y = py;
  return t;
}
#endif  // PTD_FAKE_TOUCH / ARDUINO
