// SPDX-License-Identifier: MIT
// 仿真/预处理门禁用的 esp_task_wdt 替身（真身来自 ESP-IDF）。
// ---------------------------------------------------------------------------
// 为什么需要：真机 main.cpp 里有看门狗（`esp_task_wdt_init`），而 PC 仿真/门禁
// 只做预处理与逻辑测试，不跑看门狗。没有这个替身，main.cpp 在门禁里**根本进不来**
// （那正是板名/能力宏最容易泄漏、却最没人看的文件）。
#pragma once
#include <stdint.h>
#include <stddef.h>

struct esp_task_wdt_config_t {
  uint32_t timeout_ms;
  uint32_t idle_core_mask;
  bool trigger_panic;
};

static inline int esp_task_wdt_init(const esp_task_wdt_config_t*) { return 0; }
static inline int esp_task_wdt_reconfigure(const esp_task_wdt_config_t*) { return 0; }
static inline int esp_task_wdt_add(void*) { return 0; }
static inline int esp_task_wdt_delete(void*) { return 0; }
static inline int esp_task_wdt_reset(void) { return 0; }
static inline bool esp_task_wdt_status(void*) { return false; }
