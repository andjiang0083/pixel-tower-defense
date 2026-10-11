// sim/m5gfx_fake.h — PC 端替身：把 M5GFX 的绘制原语写进内存帧缓冲
// 目的：用**同一份 render.h / pixel_text.h / game.h**在 PC 上跑，
// 验证的是固件真实代码路径，不是另写一遍模拟器。
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define SCREEN_W_LOCAL 1280
#define SCREEN_H_LOCAL 720

class M5GFX {
public:
  uint16_t* fb = nullptr;
  int w = SCREEN_W_LOCAL, h = SCREEN_H_LOCAL;

  M5GFX() {
    fb = (uint16_t*)malloc((size_t)w * h * 2);
    memset(fb, 0, (size_t)w * h * 2);
  }

  // 统计：验证"局部重绘"确实生效（每帧填了多少像素）
  static uint64_t pxFilled;
  static int frames;

  void fillRect(int32_t x, int32_t y, int32_t ww, int32_t hh, uint16_t c) {
    if (!fb || ww <= 0 || hh <= 0) return;   // 真机同样忽略非正尺寸
    int32_t x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int32_t x1 = x + ww > w ? w : x + ww, y1 = y + hh > h ? h : y + hh;
    for (int32_t yy = y0; yy < y1; ++yy) {
      uint16_t* p = fb + (size_t)yy * w;
      for (int32_t xx = x0; xx < x1; ++xx) { p[xx] = c; ++pxFilled; }
    }
  }

  void drawPixel(int32_t x, int32_t y, uint16_t c) {
    if (!fb || x < 0 || y < 0 || x >= w || y >= h) return;
    fb[(size_t)y * w + x] = c; ++pxFilled;
  }

  void drawRect(int32_t x, int32_t y, int32_t ww, int32_t hh, uint16_t c) {
    if (ww <= 0 || hh <= 0) return;          // 与 LovyanGFX 一致：负尺寸直接忽略
    fillRect(x, y, ww, 1, c);
    fillRect(x, y + hh - 1, ww, 1, c);
    fillRect(x, y, 1, hh, c);
    fillRect(x + ww - 1, y, 1, hh, c);
  }

  void fillScreen(uint16_t c) { pxFilled += (uint64_t)w * h; memset(fb, 0, (size_t)w*h*2);
    if (c == 0) return; for (int i = 0; i < w*h; ++i) fb[i] = c; }

  void setRotation(int) {}
};

uint64_t M5GFX::pxFilled = 0;
int M5GFX::frames = 0;
