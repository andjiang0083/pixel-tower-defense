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

  // ── 块拷贝（v0.5.3）────────────────────────────────────────────────
  // 把 w×h 的 RGB565 像素写进 (x,y)，行优先。
  // 存在的唯一理由：**擦背景格子**。真机实测（Tab5，44 只同屏 + 20 塔）：
  //   擦格占整个渲染的 70%，而瓶颈不是带宽是**调用次数** —— 一格 40x40
  //   用 fillRect 拼抖动纹理要 25+ 次调用，改成一次块拷贝后同负载
  //   24ms→10ms，像素数几乎没变（见 docs/FIXLOG-v0.5.3.md）。
  // 默认实现逐像素 drawPixel（正确但慢）：**只有声明 PTD_BG_TILE_BLIT 的
  // profile 会用到它**，其余后端（Claw4）不覆写也不调用 ⇒ 那边零位移。
  virtual void pushImage(int32_t x, int32_t y, int32_t w, int32_t h,
                         const uint16_t* data) {
    for (int32_t j = 0; j < h; ++j)
      for (int32_t i = 0; i < w; ++i) drawPixel(x + i, y + j, data[(size_t)j * w + i]);
  }

  // ── 格子缓存的分配钩子（同上，只被 PTD_BG_TILE_BLIT 的 profile 用）──
  // 为什么不让 render.h 直接 malloc：真机要**优先片内 SRAM**（读 202MB/s
  // vs PSRAM 63MB/s），而 heap_caps_* 是 ESP 专有头 —— 放进后端实现，
  // 引擎层保持平台无关（这条纪律见 gfx.h 头注）。
  virtual void* allocTile(size_t n) { return malloc(n); }
  virtual void  freeTile(void* p)    { free(p); }

  // 旋转。Claw4 方形屏用不上（native 朝向即最终朝向）。
  // 保留这个钩子是为了"上电方向不对"时有地方接，而不是去改渲染层。
  virtual void setRotation(int r) { (void)r; }

  // 帧缓冲首地址：双缓冲 / PSRAM 场景下 BSP 要把它交给显示控制器。
  // v0.5.1：返回类型从 uint16_t* 改成 void*。
  //   帧缓冲的像素格式现在是 PTD_FB_BPP 决定的（16 或 24），接口层
  //   不该假装它一定是 16 位。知道格式的那一侧自己去解释指针。
  virtual void* fb() = 0;
};