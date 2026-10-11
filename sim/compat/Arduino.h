// sim/compat/Arduino.h — PC 编译替身（仅提供 game.h/render.h 用到的最小面）
#pragma once
#include <stdint.h>
#include <stdlib.h>

static uint32_t g_simMs = 0;
static inline uint32_t millis(void) { return g_simMs; }
static inline void delay(uint32_t ms) { g_simMs += ms ? ms : 1; }
