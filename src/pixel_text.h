/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 andjiang0083
 */
// pixel_text.h — 用 fillRect 逐像素画字（不加载任何字体/位图资源）
// 字形来自 tools/gen_font.py 生成的 pixel_font.h（5x7，bit4=最左列）
// scale=2 → 10x14 px；scale=3 → 15x21 px。
//
// v0.5.0：删掉中文 16x16 路径（cn_font.h / px_cnChar / CN_ADV / s_cnMissing）。
//   原因见 i18n.h 头注——Claw4 的 720 宽放不下 17px/字的固定宽度中文。
//   现在 px_text 遇到 >=0x80 的字节直接跳过（当作 1 字符宽的空白），
//   不再是"静默不画还把游标推 3 字节"的中文分支。
#pragma once
#include "gfx.h"
#include "pixel_font.h"

// scale=2 → 10x14 px；scale=3 → 15x21 px。Tab5 是 720p，字要放大才够远看清
// v0.4.1：行内连续置位合并成一次 fillRect（RLE）——输出像素与逐列画完全
// 一致，但 HUD/面板大量文字的场景 fillRect 调用数降约 2/3。
static inline void px_char(Gfx* g, int x, int y, char ch, uint16_t c, int scale) {
  const uint8_t* rows;
  if (ch == '\x01')      rows = FONT_HEART;   // ♥
  else if (ch == '\x02') rows = FONT_BLOCK;   // █
  else if (ch < FONT_FIRST || ch > FONT_LAST)  return;
  else rows = FONT5x7[ch - FONT_FIRST];

  for (int r = 0; r < FONT_H; ++r) {
    const uint8_t bits = rows[r];
    int col = 0;
    while (col < FONT_W) {
      if (!(bits & (1 << (FONT_W - 1 - col)))) { ++col; continue; }   // 空列跳过
      int run = 0;                                                       // 连续实列合并
      while (col + run < FONT_W && (bits & (1 << (FONT_W - 1 - col - run)))) ++run;
      g->fillRect(x + col * scale, y + r * scale, run * scale, scale, c);
      col += run;
    }
  }
}

// 返回绘制后的 x（便于链式排版）；遇到未映射字符按空格跳过
static inline int px_text(Gfx* g, int x, int y, const char* s,
                          uint16_t c, int scale) {
  const int adv = (FONT_W + 1) * scale;          // 字间留 1 像素缝，更像点阵
  for (; *s; ++s) { px_char(g, x, y, *s, c, scale); x += adv; }
  return x;
}

static inline int px_textWidth(const char* s, int scale) {
  const int adv = (FONT_W + 1) * scale;
  int w = 0;
  for (const uint8_t* p = (const uint8_t*)s; *p; ++p) w += adv;
  return w ? w - scale : 0;                 // 去掉最后一个字间缝
}

static inline void px_textCenter(Gfx* g, int cx, int y, const char* s,
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
