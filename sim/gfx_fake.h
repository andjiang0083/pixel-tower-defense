/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 andjiang0083
 */
// sim/gfx_fake.h — PC 端显示后端（v0.5.0）
//
// v0.4.x 时这里有一个"假的 M5GFX 类"，让 render.h/pixel_text.h 的
// `#include <M5GFX.h>` 命中替身。v0.5.0 抽出 gfx.h 抽象后，替身不再需要
// 模仿 M5GFX 的接口形状——直接实现 Gfx 即可。
//
// 本文件现在只剩一件事：把 GfxFb 的两个静态统计变量定义出来
// （它们是 header-only 约束下的"单一定义点"）。
//
// 布局：sim/compat/ 保留给尚未移植的 M5Unified 依赖（touch.h / sfx.h
// 阶段 2、3 才会脱掉）；显示侧已完全走 gfx_fb.h，与 M5 无关。
#pragma once
#include "gfx_fb.h"

uint64_t GfxFb::pxFilled = 0;
int      GfxFb::frames   = 0;