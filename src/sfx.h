/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 andjiang0083
 */
// sfx.h — 程序化音效运行时：事件环形队列 + 帧间隙派发 + 节流防吵
// 音频数据零文件资源：tools/gen_sfx.py 合成 → sfx_data.h。
// 硬件路径 = M5.Speaker.playRaw（M5Unified 自带混音任务，多轨重叠 OK）；
// PC 仿真下派发为空实现（ARDUINO_M5STACK_TAB5 未定义），游戏逻辑零感知。
#pragma once
#include <stdint.h>
#include "config_gen.h"     // SCREEN_W/H（静音按钮几何）

#ifdef ARDUINO_M5STACK_TAB5
#include <M5Unified.h>
#endif
#include "sfx_data.h"

// ── 音效 ID（与 sfx_data.h 的表一一对应）─────────────────────────
// ⚠️ GUN/GUN2/GUN3 必须 ID 连续（game.h 用 SFXID_GUN + variant 轮换）
enum SfxId : uint8_t {
  SFXID_GUN = 0, SFXID_GUN2, SFXID_GUN3,      // v0.4.2：机枪三音色（基频×1/×1.25/×1.5）
  SFXID_CANNON, SFXID_BOOM, SFXID_LASER, SFXID_FROST,
  SFXID_COIN,  SFXID_HIT,     SFXID_LEAK, SFXID_CLEAR, SFXID_ROTOR,
  SFXID_HORN,  SFXID_EMP,                       // v0.4：号角/EMP
  SFXID_COUNT
};

// ── 环形事件队列：game.h（逻辑核）只投递，不碰任何音频 API ───────
#define SFX_QUEUE 24
static uint8_t  s_q[SFX_QUEUE];
static uint8_t  s_qHead = 0, s_qTail = 0;

// 每音效最小间隔 ms（"不能太吵"第一道闸：同种音高频去重）
static const uint16_t SFX_MIN_IV[SFXID_COUNT] = {
  /*GUN*/80, /*GUN2*/80, /*GUN3*/80,   // v0.4.2：三音色共用同一闸（换皮不换吵）
  /*CAN*/120, /*BOOM*/140, /*LASER*/90, /*FROST*/350,
  /*COIN*/120, /*HIT*/70, /*LEAK*/250, /*CLEAR*/0,   /*ROTOR*/380,
  /*HORN*/400, /*EMP*/500                          // v0.4：技能音低频事件，间隔防吵
};

static uint32_t s_lastMs[SFXID_COUNT];      // millis() 缓存（仿真用 tick 计数）
static uint32_t s_nowMs = 0;                // pump/主循环每帧刷新
static bool     s_muted = false;
#ifdef ARDUINO_M5STACK_TAB5
#else
static uint32_t s_simPlay[SFXID_COUNT];    // 仿真记账：每音效"起播"次数
#endif

static inline void sfxSetMuted(bool m) { s_muted = m; }
static inline bool sfxIsMuted(void)    { return s_muted; }

// 投递一个音效事件：静音/间隔未到 → 直接丢弃（零成本）
static void sfxPost(uint8_t id) {
  if (s_muted || id >= SFXID_COUNT) return;
  uint32_t iv = s_nowMs - s_lastMs[id];
  if (iv < SFX_MIN_IV[id]) return;
  s_lastMs[id] = s_nowMs;
  uint8_t next = (uint8_t)((s_qHead + 1) % SFX_QUEUE);
  if (next == s_qTail) return;              // 队列满：丢最轻的（直接丢弃新事件）
  s_q[s_qHead] = id;
  s_qHead = next;
}

// ── 派发：主循环帧间隙调用（渲染之后）。每轮最多 N 条防尖峰 ──────
// ⚠️ playRaw 在 begin() 前调用会失败但不崩；begin 由首次 pump 惰性执行。
static void sfxPump(void) {
#ifdef ARDUINO_M5STACK_TAB5
  int budget = 3;                           // 每帧最多起播3条：混音器也怕齐射尖峰
  while (budget-- > 0 && s_qTail != s_qHead) {
    uint8_t id = s_q[s_qTail];
    s_qTail = (uint8_t)((s_qTail + 1) % SFX_QUEUE);
    switch (id) {
      case SFXID_GUN:    M5.Speaker.playRaw(SFX_GUN,   SFX_GUN_LEN,   SFX_SR, false, 1, -1, false); break;
      case SFXID_GUN2:   M5.Speaker.playRaw(SFX_GUN2,  SFX_GUN2_LEN,  SFX_SR, false, 1, -1, false); break;
      case SFXID_GUN3:   M5.Speaker.playRaw(SFX_GUN3,  SFX_GUN3_LEN,  SFX_SR, false, 1, -1, false); break;
      case SFXID_CANNON: M5.Speaker.playRaw(SFX_CANNON, SFX_CANNON_LEN,SFX_SR, false, 1, -1, false); break;
      case SFXID_BOOM:   M5.Speaker.playRaw(SFX_BOOM,   SFX_BOOM_LEN,  SFX_SR, false, 1, -1, false); break;
      case SFXID_LASER:  M5.Speaker.playRaw(SFX_LASER,  SFX_LASER_LEN, SFX_SR, false, 1, -1, false); break;
      case SFXID_FROST:  M5.Speaker.playRaw(SFX_FROST,  SFX_FROST_LEN, SFX_SR, false, 1, -1, false); break;
      case SFXID_COIN:   M5.Speaker.playRaw(SFX_COIN,   SFX_COIN_LEN,  SFX_SR, false, 1, -1, false); break;
      case SFXID_HIT:    M5.Speaker.playRaw(SFX_HIT,    SFX_HIT_LEN,   SFX_SR, false, 1, -1, false); break;
      case SFXID_LEAK:   M5.Speaker.playRaw(SFX_LEAK,   SFX_LEAK_LEN,  SFX_SR, false, 1, -1, false); break;
      case SFXID_CLEAR:  M5.Speaker.playRaw(SFX_CLEAR,  SFX_CLEAR_LEN,  SFX_SR, false, 1, -1, false); break;
      case SFXID_ROTOR:  M5.Speaker.playRaw(SFX_ROTOR, SFX_ROTOR_LEN, SFX_SR, false, 1, -1, false); break;
      case SFXID_HORN:   M5.Speaker.playRaw(SFX_HORN,  SFX_HORN_LEN,  SFX_SR, false, 1, -1, false); break;
      case SFXID_EMP:    M5.Speaker.playRaw(SFX_EMP,   SFX_EMP_LEN,   SFX_SR, false, 1, -1, false); break;
    }
  }
#else
  // PC 仿真：排空队列并记账——测试断言读 s_simPlay[]（与真机同份节流/投递）
  while (s_qTail != s_qHead) { s_simPlay[s_q[s_qTail]]++;
    s_qTail = (uint8_t)((s_qTail + 1) % SFX_QUEUE); }
#endif
}

// ── 选关界面右下角静音开关几何（render 画 / actions 命中，唯一真源）─
#define MUTE_BTN_X (SCREEN_W - 92)
#define MUTE_BTN_Y (SCREEN_H - 84)
#define MUTE_BTN_W 72
#define MUTE_BTN_H 60
