// sim/compat/M5Unified.h — PC 替身：可编程驱动的假触摸屏，用来测 tap-on-release 状态机
#pragma once
#include "Arduino.h"
#include "M5GFX.h"

struct FakeTouch {
  bool down = false;
  int16_t x = 0, y = 0;

  void press(int px, int py) { down = true; x = px; y = py; }   // 测试用注入
  void release(void)        { down = false; }                     // 保持最后坐标

  uint8_t getCount(void) const { return down ? 1 : 0; }
  struct Detail { int16_t x, y; };
  Detail getDetail(void) const { return Detail{x, y}; }
};

struct FakeM5 {
  M5GFX Display;
  FakeTouch Touch;
  void update(void) {}                       // 真机在此采样；替身无需动作
  int getBoard(void) const { return 0x8B; } // board_M5Tab5
};

static FakeM5 M5;
