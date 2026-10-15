/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 andjiang0083
 */
// ============================================================================
// gfx.h — 显示后端抽象（v0.5.0 / Claw4 移植新增）
// ----------------------------------------------------------------------------
// 背景：原项目把 M5GFX 直接写进了渲染层的签名（`M5GFX* g`）。要移植到
//   Metalio Claw4（ESP-IDF + esp_lcd，不是 M5GFX），就得把 1058 行 render.h
//   全部改签名——不划算。
//
// 做法：把"往帧缓冲里画"这件事抽成接口 Gfx。渲染层只认 Gfx，不认任何具体
//   显示栈。全项目渲染只用 4 个原语（2026-10-15 全量 grep 确认）：
//     fillRect / drawRect / fillScreen / drawPixel
//     render.h 165×fillRect + 46×drawRect + 3×fillScreen
//     actions.h  2×fillRect，无 drawPixel 直接调用
//
// 实现见 gfx_fb.h（GfxFb）。约束：
//   1. render.h / actions.h / pixel_text.h 一律 include "gfx.h"，
//      绝不再出现 M5GFX / M5Unified 字样。
//   2. sim/ 编译的必须是同一份 src/*.h，否则断言没有意义。
//   3. 颜色一律 RGB565，不用真彩。
//   4. 本项目 header-only——不要在这里写非 inline 的函数体。
// ============================================================================
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "config_gen.h"     // SCREEN_W / SCREEN_H（尺寸真源 = tools/layout.py）

class Gfx {
 public:
  // 帧缓冲尺寸（逻辑像素）。Claw4 方形屏 = 720x720，由 config_gen.h 决定。
  // 这里假设"帧缓冲坐标 == 逻辑坐标"（无旋转、无偏移）。Claw4 的 native
  // 朝向正好是方形屏最终朝向，所以不需要旋转补偿。
  int w = SCREEN_W, h = SCREEN_H;

  virtual ~Gfx() {}

  // 实心矩形。越界自动裁剪；非正尺寸忽略（LovyanGFX / M5GFX 同行为，
  // 渲染层大量依赖"传负尺寸不崩"这个宽容契约）。
  virtual void fillRect(int32_t x, int32_t y, int32_t ww, int32_t hh, uint16_t c) = 0;

  // 空心矩形。非正尺寸忽略。
  virtual void drawRect(int32_t x, int32_t y, int32_t ww, int32_t hh, uint16_t c) = 0;

  // 全屏填某色。
  virtual void fillScreen(uint16_t c) = 0;

  // 单像素。越界忽略。
  virtual void drawPixel(int32_t x, int32_t y, uint16_t c) = 0;

  // 旋转。Claw4 方形屏用不上（native 朝向即最终朝向）。
  // 保留这个钩子是为了"上电方向不对"时有地方接，而不是去改渲染层。
  virtual void setRotation(int r) { (void)r; }

  // 帧缓冲首地址：双缓冲 / PSRAM 场景下 BSP 要把它交给显示控制器。
  virtual uint16_t* fb() = 0;
};