// gfx_24bpp_test.cpp — 24bpp 渲染内核的独立测试（CI 门禁）
// ----------------------------------------------------------------------------
// 为什么需要一个**独立编译单元**而不是塞进 sim/run.cpp：
//   PTD_FB_BPP 是编译期常量，而 sim/run.cpp 整个是按 16bpp 编的（PPM 截图
//   按 RGB565 解）。24bpp 那条路在 CI 里**从来没有被执行过** —— 这正是
//   v0.5.1 那个"红色全部显示成蓝色"的 bug 能活到真机上的原因：
//   x86 仿真全绿、单元测试也全绿（因为期望值是我自己按错的序写的），
//   只有玩家眼睛能发现。
//
// 所以这里单独用 -DPTD_FB_BPP=24 编一份，把两件事钉死：
//   ① 内存字节序是 B,G,R（IDF/LVGL RGB888 的通用布局，本板厂商同款）
//   ② 填充/描边/画点三条路径的**落盘结果**都符合这个序
//
// 用法：g++ -std=c++17 -O2 -DPTD_FB_BPP=24 -I build/<profile> -I src sim/gfx_24bpp_test.cpp
// 退出码非 0 = 失败。
#define PTD_FB_BPP 24
#include "config_gen.h"
#include "gfx.h"
#include "gfx_fb.h"
#include <stdio.h>

static int g_fail = 0;

static void CHECK(bool ok, const char* what) {
  if (ok) return;
  ++g_fail;
  printf("  FAIL: %s\n", what);
}

// 读出 (x,y) 的 3 个字节，按内存序返回
static void readPx(GfxFb& g, int x, int y, uint8_t out[3]) {
  const uint8_t* p = (uint8_t*)g.fb()
                   + ((size_t)y * SCREEN_W + (size_t)x) * 3;
  out[0] = p[0]; out[1] = p[1]; out[2] = p[2];
}

// 按内存序比较（B,G,R）
static bool pxEq(const uint8_t a[3], uint8_t b, uint8_t gr, uint8_t r) {
  return a[0] == b && a[1] == gr && a[2] == r;
}

int main(void) {
  GfxFb g;
  uint8_t px[3];

  // ── ① 字节序：三原色各画一个点 ──────────────────────────────────
  // 颜色常量仍是 RGB565（游戏侧的通用货币）：
  //   0xF800 = 纯红   0x07E0 = 纯绿   0x001F = 纯蓝
  // 落到内存里必须是 B,G,R —— 所以纯红在内存里是 [00,00,FF]。
  g.fillScreen(0);
  g.drawPixel(1, 1, 0xF800);
  readPx(g, 1, 1, px);
  CHECK(pxEq(px, 0x00, 0x00, 0xFF), "RED 0xF800 must land as B,G,R = 00,00,FF");

  g.drawPixel(2, 1, 0x07E0);
  readPx(g, 2, 1, px);
  CHECK(pxEq(px, 0x00, 0xFF, 0x00), "GREEN 0x07E0 must land as B,G,R = 00,FF,00");

  g.drawPixel(3, 1, 0x001F);
  readPx(g, 3, 1, px);
  CHECK(pxEq(px, 0xFF, 0x00, 0x00), "BLUE 0x001F must land as B,G,R = FF,00,00");

  // ── ② fillRect 的三条路径（窄块直写 / 宽块倍增 / 整行复制）────────
  // 宽度 4 → 12 字节/行，走"窄块直写"（≤16B）
  g.fillScreen(0);
  g.fillRect(10, 10, 4, 4, 0xF800);
  readPx(g, 10, 10, px);
  CHECK(pxEq(px, 0x00, 0x00, 0xFF), "fillRect narrow path: byte order");
  readPx(g, 13, 13, px);
  CHECK(pxEq(px, 0x00, 0x00, 0xFF), "fillRect narrow path: last pixel");
  readPx(g, 14, 10, px);
  CHECK(pxEq(px, 0x00, 0x00, 0x00), "fillRect narrow path: must not spill right");

  // 宽度 40 → 120 字节/行，走"行内倍增 + 整行 memcpy"
  g.fillScreen(0);
  g.fillRect(100, 100, 40, 40, 0x001F);
  readPx(g, 100, 100, px);
  CHECK(pxEq(px, 0xFF, 0x00, 0x00), "fillRect wide path: first pixel");
  readPx(g, 139, 139, px);
  CHECK(pxEq(px, 0xFF, 0x00, 0x00), "fillRect wide path: last pixel (rows memcpy'd)");
  readPx(g, 99, 100, px);
  CHECK(pxEq(px, 0x00, 0x00, 0x00), "fillRect wide path: must not spill left");
  readPx(g, 140, 100, px);
  CHECK(pxEq(px, 0x00, 0x00, 0x00), "fillRect wide path: must not spill right");

  // ── ③ fillScreen 走整行复制：四角都要对 ──────────────────────────
  g.fillScreen(0xF800);
  readPx(g, 0, 0, px);
  CHECK(pxEq(px, 0x00, 0x00, 0xFF), "fillScreen: top-left");
  readPx(g, SCREEN_W - 1, SCREEN_H - 1, px);
  CHECK(pxEq(px, 0x00, 0x00, 0xFF), "fillScreen: bottom-right (last row memcpy'd)");

  // ── ④ drawRect 只描边，内部必须留空 ──────────────────────────────
  g.fillScreen(0);
  g.drawRect(50, 50, 20, 20, 0x07E0);
  readPx(g, 50, 50, px);
  CHECK(pxEq(px, 0x00, 0xFF, 0x00), "drawRect: top-left corner");
  readPx(g, 60, 60, px);
  CHECK(pxEq(px, 0x00, 0x00, 0x00), "drawRect: interior must stay empty");

  // ── ⑤ 裁剪：越界的矩形不许写出边界（真机上是踩内存）──────────────
  g.fillScreen(0);
  g.fillRect(-5, -5, 30, 30, 0xF800);
  readPx(g, 0, 0, px);
  CHECK(pxEq(px, 0x00, 0x00, 0xFF), "clip: negative origin fills from (0,0)");
  g.fillScreen(0);
  g.fillRect(SCREEN_W - 5, SCREEN_H - 5, 50, 50, 0xF800);
  readPx(g, SCREEN_W - 1, SCREEN_H - 1, px);
  CHECK(pxEq(px, 0x00, 0x00, 0xFF), "clip: overlong rect stops at screen edge");

  // ── ⑥ 脏矩形：按整行记（msync 的粒度就是整行）────────────────────
  GfxFb::dirtyReset();
  CHECK(GfxFb::dirtyEmpty(), "dirty: fresh reset is empty");
  g.fillRect(30, 30, 10, 10, 0x001F);
  CHECK(GfxFb::drY0 == 30 && GfxFb::drY1 == 39, "dirty: row range is inclusive");

  if (g_fail == 0) {
    printf("gfx_24bpp_test: OK (B,G,R byte order + 3 fill paths + clipping + dirty)\n");
    return 0;
  }
  printf("gfx_24bpp_test: %d FAILED\n", g_fail);
  return 1;
}
