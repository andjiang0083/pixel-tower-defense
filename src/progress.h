/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 andjiang0083
 */
// progress.h — 关卡进度落盘（v0.4.1）
// ----------------------------------------------------------------------------
// 把"什么时候写 NVS"从 main.cpp 的 loop() 抽成纯逻辑层，让 PC 仿真能断言：
//   1) 通关画面停留期间只写一次（v0.4.1 修 P0 bug，见下）
//   2) 星级进步也会落盘（v0.4.1 修：旧去重只看 unlock/clear，
//      重打关卡刷高星级时两位图没变 → 静默丢失，见 FIXLOG-v0.4.1.md）
//
// v0.4.1 P0 bug 复盘（已修）：main.cpp 的去重缓存曾用 uint16_t，而 20 关的
// g_unlocked/g_cleared 是 uint32_t。L17 解锁后 bit16 置位，16bit 缓存装不下
// → 比较恒不相等 → ST_CLEAR 画面每帧（30fps）都写一次 NVS：flash 磨损 +
// 通关画面掉帧。本文件所有缓存与比较一律 uint32_t，并由 sim 断言看守。
// ----------------------------------------------------------------------------
#pragma once
#include <stdint.h>
#include "game.h"     // g_w / g_unlocked / g_cleared / g_stars / LEVEL_COUNT

// ── 落盘钩子：main.cpp 注入 NVS 写入；PC 仿真注入计数器 ──────────
// （v0.4.1 前声明在 actions.h，随进度逻辑一并迁到这里）
static void (*g_saveProgress)(void) = nullptr;

// ── 星级打包：3bit × 20 关 = 60bit > 32bit → 拆两个键（L1-10 / L11-20）──
static inline uint32_t packStars(int base) {
  uint32_t p = 0;
  for (int i = base; i < LEVEL_COUNT && i < base + 10; ++i)
    p |= (uint32_t)(g_stars[i] & 7) << ((i - base) * 3);
  return p;
}

// 进度签名：解锁位图 + 通关位图 + 两组星级打包（任一变化即需落盘）
static inline uint32_t progressSigA(void) {
  return g_unlocked ^ (g_cleared * 2654435761u) ^ packStars(0);
}
static inline uint32_t progressSigB(void) { return packStars(10) ^ 0x9E3779B9u; }

// ── 去重缓存（v0.4.1：uint32_t，勿改回窄类型——见文件头 P0 复盘）──────
static uint32_t s_savedSigA = 0xFFFFFFFFu;   // 初值=必写一次
static uint32_t s_savedSigB = 0xFFFFFFFFu;

// ── 主动落盘（选关界面静音切换等即时场景）后同步缓存 ───────────────
// 不缓存的直接 g_saveProgress() 调用会让下一帧 tick 重复写一遍。
static inline void progressSaveNow(void) {
  s_savedSigA = progressSigA();
  s_savedSigB = progressSigB();
  if (g_saveProgress) g_saveProgress();
}

// ── 每帧调用（仅 ST_CLEAR 有意义）：进度变化才落盘一次 ─────────────
// 通关画面停留多久都只写一次；不变则零成本返回。
static inline void progressTickOnClear(void) {
  if (g_w.state != ST_CLEAR) return;
  if (s_savedSigA == progressSigA() && s_savedSigB == progressSigB()) return;
  progressSaveNow();
}

// ── 缓存失效：换关/重开后强制下一次 tick 重新评估 ─────────────────
static inline void progressInvalidate(void) {
  s_savedSigA = 0xFFFFFFFFu;
  s_savedSigB = 0xFFFFFFFFu;
}
