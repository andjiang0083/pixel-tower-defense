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

class GfxM5 : public Gfx {
 public:
  explicit GfxM5(M5GFX* d) : d_(d) {}

  // Gfx 的四个原语 → M5GFX 同名方法（颜色 uint16_t 隐式升 uint32_t）
  void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c) override {
    d_->fillRect(x, y, w, h, c);
  }
  void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c) override {
    d_->drawRect(x, y, w, h, c);
  }
  void fillScreen(uint16_t c) override { d_->fillScreen(c); }
  void drawPixel(int32_t x, int32_t y, uint16_t c) override { d_->drawPixel(x, y, c); }
  void setRotation(int r) override { d_->setRotation((uint8_t)r); }

  // ⚠️ Tab5 不交裸帧缓冲指针：它属于 M5GFX/esp_lcd（DSI 正在扫它），
  //    固件侧没有任何调用点需要（fb() 只被 PC 仿真的 PPM 截图与 Claw4 BSP 用）。
  //    将来若要在这里读回像素，走 M5GFX 自己的 readPixel，别去动扫描中的缓冲。
  void* fb() override { return nullptr; }

 private:
  M5GFX* d_;
};
