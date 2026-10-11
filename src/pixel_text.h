// pixel_text.h — 用 fillRect 逐像素画字（不加载任何字体/位图资源）
// 字形来自 tools/gen_font.py 生成的 pixel_font.h（5x7，bit4=最左列）
#pragma once
#include <M5GFX.h>
#include "pixel_font.h"

// scale=2 → 10x14 px；scale=3 → 15x21 px。Tab5 是 720p，字要放大才够远看清
static inline void px_char(M5GFX* g, int x, int y, char ch, uint16_t c, int scale) {
  const uint8_t* rows;
  if (ch == '\x01')      rows = FONT_HEART;   // ♥
  else if (ch == '\x02') rows = FONT_BLOCK;   // █
  else if (ch < FONT_FIRST || ch > FONT_LAST)  return;
  else rows = FONT5x7[ch - FONT_FIRST];

  for (int r = 0; r < FONT_H; ++r) {
    uint8_t bits = rows[r];
    for (int col = 0; col < FONT_W; ++col) {
      if (bits & (1 << (FONT_W - 1 - col))) {
        g->fillRect(x + col * scale, y + r * scale, scale, scale, c);
      }
    }
  }
}

// 返回绘制后的 x（便于链式排版）；遇到未映射字符按空格跳过
static inline int px_text(M5GFX* g, int x, int y, const char* s,
                          uint16_t c, int scale) {
  const int adv = (FONT_W + 1) * scale;          // 字间留 1 像素缝，更像点阵
  while (*s) { px_char(g, x, y, *s++, c, scale); x += adv; }
  return x;
}

static inline int px_textWidth(const char* s, int scale) {
  const int adv = (FONT_W + 1) * scale;
  int n = 0; for (const char* p = s; *p; ++p) ++n;
  return n ? n * adv - scale : 0;                 // 去掉最后一个字间缝
}

static inline void px_textCenter(M5GFX* g, int cx, int y, const char* s,
                                 uint16_t c, int scale) {
  px_text(g, cx - px_textWidth(s, scale) / 2, y, s, c, scale);
}

// 整数 → 文本（避免每次 vsnprintf 的开销与堆碎片）
static inline void px_itoa(int v, char* out) {
  if (v < 0) { *out++ = '-'; v = -v; }
  char tmp[12]; int i = 0;
  do { tmp[i++] = '0' + (v % 10); v /= 10; } while (v);
  while (i) *out++ = tmp[--i];
  *out = 0;
}
