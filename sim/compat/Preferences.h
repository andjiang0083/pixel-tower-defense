// SPDX-License-Identifier: MIT
// 仿真/预处理门禁用的 Preferences(NVS) 替身（真身来自 Arduino-ESP32）。
// 只提供 main.cpp 用到的那几个存取接口；返回值由内存字典给，仿真重启即清空。
#pragma once
#include <string>
#include <map>
#include <cstdint>

class Preferences {
 public:
  bool begin(const char*, bool = false) { return true; }
  void end() {}
  uint32_t getUInt(const char* k, uint32_t d = 0) const { auto i = m.find(k); return i == m.end() ? d : i->second; }
  void putUInt(const char* k, uint32_t v) { m[k] = v; }
  uint8_t getUChar(const char* k, uint8_t d = 0) const { auto i = m.find(k); return i == m.end() ? (uint8_t)d : (uint8_t)i->second; }
  void putUChar(const char* k, uint8_t v) { m[k] = v; }
  uint16_t getUShort(const char* k, uint16_t d = 0) const { auto i = m.find(k); return i == m.end() ? d : (uint16_t)i->second; }
  void putUShort(const char* k, uint16_t v) { m[k] = v; }
 private:
  std::map<const char*, uint32_t> m;
};
