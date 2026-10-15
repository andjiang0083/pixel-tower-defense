// sim/compat/M5Unified.h — PC 替身（v0.5.0：显示侧改走 Gfx 抽象）
//
// v0.4.x 时 M5.Display 是个"假 M5GFX 类"，形状照抄 M5GFX。
// v0.5.0 抽出 gfx.h 之后，render.h / pixel_text.h 已经不认识 M5GFX 了，
// 所以 Display 直接持有 GfxFb——它现在就是 Gfx 的一个具体实现，
// 没有任何 M5 血统。
//
// 本替身剩下的唯一职责：FakeTouch，测 tap-on-release 状态机。
// 阶段 2 会把 touch.h 换成 GT911 直读，届时本文件可整个删除。
#pragma once
#include "Arduino.h"
#include "gfx_fake.h"        // → gfx_fb.h，并给出 GfxFb 静态成员的单一定义

struct FakeTouch {
  bool down = false;
  int16_t x = 0, y = 0;

  void press(int px, int py) { down = true; x = px; y = py; }   // 测试用注入
  void release(void)        { down = false; }                     // 保持最后坐标

  uint8_t getCount(void) const { return down ? 1 : 0; }
  struct Detail { int16_t x, y; };
  Detail getDetail(void) const { return Detail{x, y}; }
};

// sim/run.cpp 仍按 M5.Display 取显示设备（照 Arduino 侧 main.cpp 的写法）。
// 保留这个薄壳是为了让阶段 1 的 diff 只动类型、不动结构——
// 真正搬 main.cpp 是阶段 3 的事。
struct FakeM5 {
  GfxFb Display;
  FakeTouch Touch;
  void update(void) {}                       // 真机在此采样；替身无需动作
  int getBoard(void) const { return 0x8B; } // board_M5Tab5
};

static FakeM5 M5;