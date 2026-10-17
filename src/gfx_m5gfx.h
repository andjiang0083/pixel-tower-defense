/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 andjiang0083
 */
// ============================================================================
// gfx_m5gfx.h — Tab5 显示后端：把 M5GFX 适配成 Gfx（v0.5.2）
// ----------------------------------------------------------------------------
// 背景：v0.5.0 为 Claw4 移植引入了 Gfx 抽象（src/gfx.h），render.h/actions.h/
//   pixel_text.h 的签名从 `M5GFX*` 改成 `Gfx*`，显示侧有三条实现：
//     · PC 仿真   → GfxFb（sim/gfx_fake.h，帧缓冲在普通内存）
//     · Claw4 真机 → GfxClaw4（ptd-claw4-fw/main/bsp_claw4.h，自管 PSRAM 帧缓冲）
//     · **Tab5 真机 → 本文件**（一来就缺了这一条）
//   Tab5 的帧缓冲归 M5GFX/esp_lcd 所有（DSI video mode，DMA 连续扫描），
//   我们只是往它上面画矩形，所以这里**不做自己的帧缓冲**，纯转发。
//
// ⚠️ 这个文件只被 src/main.cpp（Tab5 专有入口）包含：
//   仿真与 Claw4 都看不到它 ⇒ 不会改动它们的任何一行行为。
//
// ⚠️ 别忘了 `-I build/tab5`（platformio.ini）：config_gen.h 自 v0.5.0 起
//   在 build/<profile>/ 里，不在 src/。
// ============================================================================
#pragma once
#include "gfx.h"
#include <M5Unified.h>          // 仅此一处：显示后端是唯一允许认得 M5GFX 的地方

// ── 性能探针（v0.5.3）：默认 0 ⇒ 发行固件零成本（连那几个自增都不编进来）──
// 为什么要数这两个量：Tab5 这条路的真实成本面就是**往那块正被 DSI 扫描的
// PSRAM 帧缓冲提交了多少次、多少像素**。次数决定"碎块税"（每次 fillRect 的
// 固定开销 + 缓存行读写回），像素数决定带宽账。两者分开量才判得出该砍谁。
// ⚠️ 探针只在 Tab5 入口（main.cpp）与这里，仿真/Claw4 看不到 ⇒ 它们零位移。
#ifndef PTD_PERF_PROBE
#define PTD_PERF_PROBE 0
#endif

class GfxM5 : public Gfx {
 public:
  explicit GfxM5(M5GFX* d) : d_(d) {}

#if PTD_PERF_PROBE
  // 每帧由 main.cpp 清零/读取（见 perfFrame）
  static inline uint32_t p_rects = 0;        // fillRect+drawRect+drawPixel 调用数
  static inline uint32_t p_small = 0;        // 其中"小碎块"（面积 ≤64px）的次数
  static inline uint64_t p_px    = 0;        // 提交的像素数（未按裁剪修正，上界）
  static inline uint32_t p_drect = 0;        // drawRect（线框，走 4 条边）次数
  static inline uint32_t p_blits = 0;        // pushImage（块拷贝）次数
#endif

  // Gfx 的四个原语 → M5GFX 同名方法（颜色 uint16_t 隐式升 uint32_t）
  void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c) override {
#if PTD_PERF_PROBE
    ++p_rects; if ((int64_t)w * h > 0) p_px += (uint64_t)w * (uint64_t)h;
    if ((int64_t)w * h > 0 && (int64_t)w * h <= 64) ++p_small;
#endif
    d_->fillRect(x, y, w, h, c);
  }
  void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c) override {
#if PTD_PERF_PROBE
    ++p_rects; ++p_drect;
    if (w > 0 && h > 0) p_px += (uint64_t)(w + h) * 2ull;
#endif
    d_->drawRect(x, y, w, h, c);
  }
  void fillScreen(uint16_t c) override { d_->fillScreen(c); }
  void drawPixel(int32_t x, int32_t y, uint16_t c) override {
#if PTD_PERF_PROBE
    ++p_rects; ++p_px; ++p_small;
#endif
    d_->drawPixel(x, y, c);
  }
  void setRotation(int r) override { d_->setRotation((uint8_t)r); }

  // ── 块拷贝（v0.5.3）：交给 M5GFX 自己的 pushImage（DSI 帧缓冲在 PSRAM，
  //    一次调用写完一整格 —— 这是"擦格"从 25+ 次 fillRect 降到 1 次的关键）──
  void pushImage(int32_t x, int32_t y, int32_t w, int32_t h,
                 const uint16_t* data) override {
#if PTD_PERF_PROBE
    ++p_blits; if (w > 0 && h > 0) p_px += (uint64_t)w * (uint64_t)h;
#endif
    d_->pushImage(x, y, w, h, data);
  }

  // ── 格子缓存优先放片内 SRAM（读 202MB/s vs PSRAM 63MB/s）；不够再退 PSRAM ──
  void* allocTile(size_t n) override {
    const size_t freeInt = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    void* p = heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#if PTD_PERF_PROBE
    Serial.printf("PERF tile alloc %u B -> %s (internal free=%u)\n",
                  (unsigned)n, p ? "INTERNAL" : "PSRAM-fallback", (unsigned)freeInt);
#endif
    if (!p) p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p;
  }
  void freeTile(void* p) override { if (p) heap_caps_free(p); }

  // ⚠️ Tab5 不交裸帧缓冲指针：它属于 M5GFX/esp_lcd（DSI 正在扫它），
  //    固件侧没有任何调用点需要（fb() 只被 PC 仿真的 PPM 截图与 Claw4 BSP 用）。
  //    将来若要在这里读回像素，走 M5GFX 自己的 readPixel，别去动扫描中的缓冲。
  void* fb() override { return nullptr; }

 private:
  M5GFX* d_;
};
