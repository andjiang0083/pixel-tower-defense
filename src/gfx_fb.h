/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 andjiang0083
 */
// ============================================================================
// gfx_fb.h — Gfx 的帧缓冲实现（PC 仿真 与 Claw4 真机 共用同一份代码）
// ----------------------------------------------------------------------------
// 为什么两边能共用：
//   两块屏的工作模型相同 —— 帧缓冲常驻内存，由显示控制器连续扫描输出。
//     · PC sim → malloc 一块普通内存，截图时直接读
//     · Claw4  → 直接用 esp_lcd_dpi_panel_get_frame_buffer() 取回的那一块
//   画法完全一致，只有"像素是几个字节"和"fb 从哪来"不同。
//
// ── v0.5.1 性能重写：把每帧两笔全屏开销砍成零 ──────────────────────
//
// v0.5.0 的 Claw4 管线是这样的（每帧必做）：
//   ① 游戏画进 RGB565 的 fb（1.03MB）
//   ② commit() 把 720×720 全量 565→888 展开到 fb888（1.55MB）
//   ③ esp_lcd_panel_draw_bitmap() 再把 fb888 整块 memcpy 进驱动的 fb
// ② 是 518400 次"读 2 字节 → 6 次整数乘/除 → 写 3 字节"，③ 是 1.55MB
// 的 PSRAM→PSRAM 拷贝。两笔加起来占了帧时间的绝大部分 —— 这就是
// "真机能出图但很卡"的全部原因，跟游戏逻辑一点关系都没有。
//
// 读 esp_lcd_panel_dpi.c:520 发现的出路：驱动先判断 draw_bitmap 传进来的
// 指针是不是**它自己那块帧缓冲**：
//     if (draw_buffer >= fbs[0] && draw_buffer < fbs[0] + fb_size)
//         → 不 memcpy，只 esp_cache_msync() 回写，并切换 cur_fb_index
// 也就是说：**画进驱动的 fb，就同时免掉了 ② 和 ③**。
//
// 所以本文件现在：
//   · 支持 16bpp 与 24bpp 两种帧缓冲（PTD_FB_BPP），Claw4 用 24
//   · fillRect 用"指数倍增填充"代替逐像素写（见下）
//   · 记录脏矩形，让 commit 只推变化的那部分扫描行
//
// ⚠️ 仍然要守的老规矩（v0.4.x 真机崩过一次，别再犯）：
//   **别每帧大面积重写帧缓冲**。720×720 RGB888 是 1.55MB，全屏重写一次
//   就是 1.55MB 的 PSRAM 写 + 与 DSI 扫描 DMA 抢 cache。render.h 的
//   drawOnboard 压暗实现与 drawGameOver 的稀疏暗块都是这个约束的产物。
// ============================================================================
#pragma once
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "gfx.h"

// ── 帧缓冲像素格式 ─────────────────────────────────────────────────
//   16 = RGB565（PC 仿真 / Tab5：设计时的原生格式，PPM 截图按它解）
//   24 = RGB888（Claw4：DSI 流与面板 COLMOD 0x77 都是 888）
// 由包含者在 include 本文件之前 #define PTD_FB_BPP 选择，默认 16。
#ifndef PTD_FB_BPP
#define PTD_FB_BPP 16
#endif
#if PTD_FB_BPP == 24
#define PTD_FB_PX_BYTES 3
#elif PTD_FB_BPP == 16
#define PTD_FB_PX_BYTES 2
#else
#error "PTD_FB_BPP must be 16 or 24"
#endif

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
  void* fb_ = nullptr;
  bool owned_ = false;

  // 统计：验证"局部重绘"确实生效（每帧填了多少像素）。sim 的
  // checkLocalRedraw 断言在用——如果哪天有人改成全屏 fillScreen，
  // 这个计数器会先报警。
  //
  // v0.5.0：改成 **inline 变量**（C++17）。原先的定义放在 sim/gfx_fake.h，
  // 那是 PC 侧的"单一定义点"；真机固件编译同一份 gfx_fb.h 时没有那个文件，
  // 于是链接期报 undefined reference to `GfxFb::pxFilled`。
  // inline 变量由 C++ 保证"一个定义、多处可见"，两个平台都不需要额外 .cpp。
  static inline uint64_t pxFilled = 0;
  static inline int frames   = 0;

  // ── 脏矩形（v0.5.1）────────────────────────────────────────────
  // 本帧所有绘制操作的包围盒。空集用 drX1 < 0 表示。
  // ⚠️ 只按**整行**生效：esp_lcd_panel_dpi.c 的 msync 粒度是
  //    (y_end - y_start) 条整行，不是 (x,y) 矩形。所以这里记 y 范围
  //    才是有效信息，x 范围保留供将来做列级优化时直接用。
  static inline int32_t drX0 = 0, drY0 = 0, drX1 = -1, drY1 = -1;
  // SCREEN_W/H 来自 config_gen.h（尺寸真源），不用成员 w/h——
  // 这四个标记是静态的，BSP 侧没有 GfxFb 实例也要能读到。
  static inline void dirtyReset(void) { drX0 = 0; drY0 = 0; drX1 = -1; drY1 = -1; }
  static inline void dirtyAll(void)   { drX0 = 0; drY0 = 0; drX1 = SCREEN_W - 1; drY1 = SCREEN_H - 1; }
  static inline bool dirtyEmpty(void) { return drX1 < 0; }
  static inline void dirtyAdd(int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    if (drX1 < 0) { drX0 = x0; drY0 = y0; drX1 = x1; drY1 = y1; return; }
    if (x0 < drX0) drX0 = x0;
    if (y0 < drY0) drY0 = y0;
    if (x1 > drX1) drX1 = x1;
    if (y1 > drY1) drY1 = y1;
  }

  // 自管内存（PC 侧典型用法）
  GfxFb() : fb_(PTD_GFX_ALLOC((size_t)SCREEN_W * SCREEN_H * PTD_FB_PX_BYTES)),
            owned_(true) {
    if (fb_) memset(fb_, 0, (size_t)SCREEN_W * SCREEN_H * PTD_FB_PX_BYTES);
  }

  // 借用外部内存（Claw4 侧：驱动自己那块 DSI 帧缓冲）
  explicit GfxFb(void* external, bool owned = false)
      : fb_(external), owned_(owned) {}

  virtual ~GfxFb() { if (owned_) PTD_GFX_FREE(fb_); }

  // ── 实心矩形：整块填充的快路径 ─────────────────────────────────
  // v0.5.1：旧实现是逐像素 p[xx] = c。720×720 的屏上光画满背景就要
  //   518400 次存储，而游戏里绝大部分 fillRect 是**实心单色块**
  //   （草地/路径描边/擦除框/UI 底板/格子底色），每次都在重复写同一个值。
  //
  //   新做法分三档：
  //     窄块（≤16 字节/行）→ 逐像素直写。
  //       为什么不统一用 memcpy：memcpy 是**函数调用**，对 3~12 字节
  //       来说调用开销比直接写还贵。render.h 里 4x4 / 6x4 / 8x8 这种
  //       小矩形非常多，统一用 memcpy 反而会变慢。
  //     宽块 → 行内指数倍增（1→2→4→8…）铺满第一行，
  //            再整行 memcpy 复制给其余行。
  //             40px 宽的格子（24bpp 下 120 字节 × 40 行 = 4800 字节）
  //             从 1600 次逐像素写 → 1+6 次铺首行 + 39 次整行复制。
  //   为什么不用 memset：memset 只能填单字节，3 字节像素填不了
  //   （除非整块是 0，那条路 fillScreen 已经走了）。
  static inline void fillRowGrow(uint8_t* p, const uint8_t* px, size_t n) {
    for (size_t i = 0; i < PTD_FB_PX_BYTES; ++i) p[i] = px[i];
    size_t done = PTD_FB_PX_BYTES;
    while (done < n) {
      const size_t rest = n - done;
      const size_t chunk = (rest < done) ? rest : done;
      memcpy(p + done, p, chunk);
      done += chunk;
    }
  }

  virtual void fillRect(int32_t x, int32_t y, int32_t ww, int32_t hh,
                        uint16_t c) override {
    if (!fb_ || ww <= 0 || hh <= 0) return;      // 真机同样忽略非正尺寸
    int32_t x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int32_t x1 = x + ww > w ? w : x + ww;
    int32_t y1 = y + hh > h ? h : y + hh;
    if (x0 >= x1 || y0 >= y1) return;

    const size_t stride = (size_t)w * PTD_FB_PX_BYTES;
    const size_t n = (size_t)(x1 - x0) * PTD_FB_PX_BYTES;
#if PTD_FB_BPP == 24
    // 颜色 → 3 字节。只在这里展开一次（每次 fillRect 一次），不是每像素
    // 一次：这正是 24bpp 相对 16bpp 的额外代价，且已被下面的批量搬运摊薄。
    const uint8_t pix[3] = {
      (uint8_t)((uint32_t)((c >> 11) & 0x1F) * 255u / 31u),   // R
      (uint8_t)((uint32_t)((c >> 5)  & 0x3F) * 255u / 63u),   // G
      (uint8_t)((uint32_t)( c        & 0x1F) * 255u / 31u),   // B
    };
#else
    const uint8_t pix[2] = { (uint8_t)(c & 0xFF), (uint8_t)(c >> 8) };
#endif
    uint8_t* row0 = (uint8_t*)fb_ + (size_t)y0 * stride
                                  + (size_t)x0 * PTD_FB_PX_BYTES;
    if (n <= 16) {
      uint8_t* p = row0;
      for (int32_t yy = y0; yy < y1; ++yy) {
        for (size_t i = 0; i < n; i += PTD_FB_PX_BYTES)
          for (size_t k = 0; k < PTD_FB_PX_BYTES; ++k) p[i + k] = pix[k];
        p += stride;
      }
    } else {
      fillRowGrow(row0, pix, n);
      uint8_t* p = row0 + stride;
      for (int32_t yy = y0 + 1; yy < y1; ++yy) { memcpy(p, row0, n); p += stride; }
    }
    pxFilled += (uint64_t)(x1 - x0) * (uint64_t)(y1 - y0);
    dirtyAdd(x0, y0, x1 - 1, y1 - 1);
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
    const size_t total = (size_t)w * h * PTD_FB_PX_BYTES;
    pxFilled += (uint64_t)w * h;
    dirtyAll();
    if (c == 0) { memset(fb_, 0, total); return; }
    // 先铺第一行，再把整行 memcpy 复制给其余行（全屏一次 ≈ 720 次 memcpy）
    const size_t stride = (size_t)w * PTD_FB_PX_BYTES;
    fillRect(0, 0, w, 1, c);
    for (int32_t yy = 1; yy < h; ++yy)
      memcpy((uint8_t*)fb_ + (size_t)yy * stride, fb_, stride);
  }

  virtual void drawPixel(int32_t x, int32_t y, uint16_t c) override {
    if (!fb_ || x < 0 || y < 0 || x >= w || y >= h) return;
    uint8_t* p = (uint8_t*)fb_
               + ((size_t)y * w + (size_t)x) * PTD_FB_PX_BYTES;
#if PTD_FB_BPP == 24
    p[0] = (uint8_t)((uint32_t)((c >> 11) & 0x1F) * 255u / 31u);
    p[1] = (uint8_t)((uint32_t)((c >> 5)  & 0x3F) * 255u / 63u);
    p[2] = (uint8_t)((uint32_t)( c        & 0x1F) * 255u / 31u);
#else
    p[0] = (uint8_t)(c & 0xFF); p[1] = (uint8_t)(c >> 8);
#endif
    ++pxFilled;
    dirtyAdd(x, y, x, y);
  }

  // 原始指针（BSP / 截图都从这里取；GfxFb 不解释它的像素格式）
  virtual void* fb() override { return fb_; }
  // 16bpp 视图（PC 侧 PTD_FB_BPP=16 时用；24bpp 下不要调它）
  uint16_t* fb16() const { return (uint16_t*)fb_; }
};
