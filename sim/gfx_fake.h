/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 andjiang0083
 */
// sim/gfx_fake.h — PC 端显示后端（v0.5.0）
//
// v0.4.x 时这里有一个"假的 M5GFX 类"，让 render.h/pixel_text.h 的
// `#include <M5GFX.h>` 命中替身。v0.5.0 抽出 gfx.h 抽象后，替身不再需要
// 模仿 M5GFX 的接口形状——直接实现 Gfx 即可。
//
// v0.5.0 变更：GfxFb 的两个静态统计成员改成了 gfx_fb.h 里的 **inline 变量**
// （C++17），因此本文件**不再需要**提供它们的单一定义——真机固件编译同一份
// gfx_fb.h 时曾因缺这个定义而链接失败（undefined reference to
// `GfxFb::pxFilled`）。inline 变量让两个平台都不必记着补定义。
//
// 本文件现在只剩一个作用：让 sim/run.cpp 一行 include 就能拿到 GfxFb
// （历史上它还兼做 M5Unified 的 include 中转，见 sim/compat/M5Unified.h）。
#pragma once
#include "gfx_fb.h"
