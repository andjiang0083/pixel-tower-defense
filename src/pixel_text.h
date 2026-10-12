/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 andjiang0083
 */
// pixel_text.h — 用 fillRect 逐像素画字（不加载任何字体/位图资源）
// 字形来自 tools/gen_font.py 生成的 pixel_font.h（5x7，bit4=最左列）
// v0.4.2：中文走 tools/gen_cnfont.py 生成的 cn_font.h（16x16）。
//   px_text/px_textWidth 遇 UTF-8 高位字节自动切中文路径——调用方零感知。
//   中文固定 16x16（不随 scale 放大）：与 scale3 的 ASCII（15x21）高度协调，
//   且汉字笔画密，放大会糊。混排宽度 = 17px/字（含 1px 缝）。
#pragma once
#include <M5GFX.h>
#include "pixel_font.h"
#include "cn_font.h"

// scale=2 → 10x14 px；scale=3 → 15x21 px。Tab5 是 720p，字要放大才够远看清
// v0.4.1：行内连续置位合并成一次 fillRect（RLE）——输出像素与逐列画完全
// 一致，但 HUD/面板大量文字的场景 fillRect 调用数降约 2/3。
static inline void px_char(M5GFX* g, int x, int y, char ch, uint16_t c, int scale) {
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

// ── v0.4.2 中文路径：UTF-8 3 字节序列 → 16x16 点阵 ─────────────────
// 线性扫小字表（<200 字，每字 3 字节比较；界面文本短，成本可忽略）
static inline const uint8_t* cnGlyphRows(const char* s) {
  const uint8_t* p = (const uint8_t*)s;
  for (int i = 0; i < CN_GLYPH_COUNT; ++i) {
    const uint8_t* q = (const uint8_t*)CN_GLYPHS[i].utf8;
    if (p[0] == q[0] && p[1] == q[1] && p[2] == q[2]) return CN_GLYPHS[i].rows;
  }
  return nullptr;                          // 未收字：按空格跳过（gen_cnfont 会收全）
}

// 与 px_char 同款 RLE 合并（每行 16 位 = 两字节拼）
static inline void px_cnChar(M5GFX* g, int x, int y, const char* s, uint16_t c) {
  const uint8_t* rows = cnGlyphRows(s);
  if (!rows) return;
  for (int r = 0; r < CN_FONT_H; ++r) {
    const uint16_t bits = (uint16_t)((rows[r * 2] << 8) | rows[r * 2 + 1]);
    int col = 0;
    while (col < CN_FONT_W) {
      if (!(bits & (1 << (CN_FONT_W - 1 - col)))) { ++col; continue; }
      int run = 0;
      while (col + run < CN_FONT_W && (bits & (1 << (CN_FONT_W - 1 - col - run)))) ++run;
      g->fillRect(x + col, y + r, run, 1, c);
      col += run;
    }
  }
}

#define CN_ADV (CN_FONT_W + 1)             // 中文字间距（同 ASCII 留 1px 缝）

// 返回绘制后的 x（便于链式排版）；遇到未映射字符按空格跳过
static inline int px_text(M5GFX* g, int x, int y, const char* s,
                          uint16_t c, int scale) {
  const int adv = (FONT_W + 1) * scale;          // 字间留 1 像素缝，更像点阵
  while (*s) {
    if ((uint8_t)*s >= 0x80) {                   // v0.4.2：UTF-8 中文序列
      px_cnChar(g, x, y, s, c);
      s += 3;                                    // 本项目只用 3 字节 UTF-8（CJK）
      x += CN_ADV;
      continue;
    }
    px_char(g, x, y, *s++, c, scale); x += adv;
  }
  return x;
}

static inline int px_textWidth(const char* s, int scale) {
  const int adv = (FONT_W + 1) * scale;
  int w = 0;
  for (const uint8_t* p = (const uint8_t*)s; *p; ) {
    if (*p >= 0x80) { w += CN_ADV; p += 3; continue; }   // 中文 17px
    w += adv; ++p;
  }
  return w ? w - scale : 0;                 // 去掉最后一个字间缝
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
