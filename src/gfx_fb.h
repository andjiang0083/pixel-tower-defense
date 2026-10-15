/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 andjiang0083
 */
// ============================================================================
// gfx_fb.h — Gfx 的帧缓冲实现（PC 仿真 与 Claw4 真机 共用同一份代码）
// ----------------------------------------------------------------------------
// 为什么两边能共用：
//   两块屏的工作模型相同 —— 帧缓冲常驻内存，由显示控制器连续扫描输出。
//     · PC sim → malloc 一块普通内存，截图时直接读
//     · Claw4  → BSP 在 PSRAM 里分配，交给 esp_lcd_dsi video mode 做常驻扫描，
//                绘制端只管往里写；出屏范围由 fillRect 的裁剪分支保证
//   画法完全一致，所以只有"这块 fb 从哪来"不同。
//
// DSI 集成要点（Claw4 侧 BSP 必须遵守，gfx_fb 只管画不管）：
//   1. fb 必须落在 PSRAM，且按 DSI DMA 的要求做 cache 对齐/属性配置。
//   2. 视频模式全屏扫描时 PSRAM 会被 DMA 高频读取。ESP32-P4 上如果 fb 所在
//      内存被 CPU 侧 cache 持有、又没有正确配置成 DMA 可访问属性，会出现
//      撕裂或 esp_cache_msync 争用。真机首屏务必先验证静态画面不撕裂。
//   3. 原项目在 1280x720 上踩过"每帧 14400 次 fillRect 压暗 → 与扫描 DMA
//      抢 cache → esp_cache_msync 自旋锁死 → WDT panic"。新屏 720x720 像素
//      总量反而更小，但同样的病根还在：**别每帧大面积重写帧缓冲**。
//      render.h 里 drawOnboard 的压暗实现见该函数注释。
// ============================================================================
#pragma once
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "gfx.h"

// ── 分配钩子 ───────────────────────────────────────────────────────────
// PC 侧：普通 malloc。
// Claw4 侧：在 BSP 里先 #define PTD_GFX_PSRAM 1，或在包含本文件前定义
//   PTD_GFX_ALLOC / PTD_GFX_FREE 宏指向 heap_caps_malloc(..., MALLOC_CAP_SPIRAM)。
//   这样 gfx_fb.h 本身不用包含任何 ESP-IDF 头，保持平台无关。
#ifndef PTD_GFX_ALLOC
#define PTD_GFX_ALLOC(n)        malloc(n)
#endif
#ifndef PTD_GFX_FREE
#define PTD_GFX_FREE(p)        free(p)
#endif

// ── GfxFb ──────────────────────────────────────────────────────────────
class GfxFb : public Gfx {
 public:
  uint16_t* fb_ = nullptr;
  bool owned_ = false;

  // 统计：验证"局部重绘"确实生效（每帧填了多少像素）。sim 的
  // checkLocalRedraw 断言在用——如果哪天有人改成全屏 fillScreen，
  // 这个计数器会先报警。
  //
  // v0.5.0：改成 **inline 变量**（C++17）。原先的定义放在 sim/gfx_fake.h，
  // 那是 PC 侧的"单一定义点"；真机固件编译同一份 gfx_fb.h 时没有那个文件，
  // 于是链接期报 undefined reference to `GfxFb::pxFilled`。
  // inline 变量由 C++ 保证"一个定义、多处可见"，两个平台都不需要额外 .cpp，
  // 也不用谁去记得补定义——这正是 header-only 项目想要的。
  static inline uint64_t pxFilled = 0;
  static inline int frames   = 0;

  // 自管内存（PC 侧典型用法）
  GfxFb() : fb_((uint16_t*)PTD_GFX_ALLOC((size_t)SCREEN_W * SCREEN_H * 2)),
            owned_(true) {
    if (fb_) memset(fb_, 0, (size_t)SCREEN_W * SCREEN_H * 2);
  }

  // 借用外部内存（Claw4 侧典型用法：BSP 分配 PSRAM 并交给 esp_lcd_dsi）
  explicit GfxFb(uint16_t* external, bool owned = false)
      : fb_(external), owned_(owned) {}

  virtual ~GfxFb() { if (owned_) PTD_GFX_FREE(fb_); }

  virtual void fillRect(int32_t x, int32_t y, int32_t ww, int32_t hh,
                        uint16_t c) override {
    if (!fb_ || ww <= 0 || hh <= 0) return;      // 真机同样忽略非正尺寸
    int32_t x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int32_t x1 = x + ww > w ? w : x + ww;
    int32_t y1 = y + hh > h ? h : y + hh;
    for (int32_t yy = y0; yy < y1; ++yy) {
      uint16_t* p = fb_ + (size_t)yy * w;
      for (int32_t xx = x0; xx < x1; ++xx) { p[xx] = c; ++pxFilled; }
    }
  }

  virtual void drawRect(int32_t x, int32_t y, int32_t ww, int32_t hh,
                        uint16_t c) override {
    if (ww <= 0 || hh <= 0) return;              // 与 LovyanGFX 一致：负尺寸忽略
    fillRect(x, y, ww, 1, c);
    fillRect(x, y + hh - 1, ww, 1, c);
    fillRect(x, y, 1, hh, c);
    fillRect(x + ww - 1, y, 1, hh, c);
  }

  virtual void fillScreen(uint16_t c) override {
    if (!fb_) return;
    pxFilled += (uint64_t)w * h;
    memset(fb_, 0, (size_t)w * h * 2);
    if (c == 0) return;
    for (int i = 0; i < w * h; ++i) fb_[i] = c;
  }

  virtual void drawPixel(int32_t x, int32_t y, uint16_t c) override {
    if (!fb_ || x < 0 || y < 0 || x >= w || y >= h) return;
    fb_[(size_t)y * w + x] = c;
    ++pxFilled;
  }

  virtual uint16_t* fb() override { return fb_; }
};