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

#ifdef ARDUINO_M5STACK_TAB5
// [DIAG] 统一发射口：force=true 排除"流忙丢帧"，并打日志
static void sfxPlay(const int16_t* d, uint32_t len) {
  M5.Speaker.playRaw(d, len, SFX_SR, false, 1, -1, true);   // force=true：齐射时强占
}
#define SFX_PLAY(d, len) sfxPlay((d), (len))
#endif

// ── 派发：主循环帧间隙调用（渲染之后）。每轮最多 N 条防尖峰 ──────
// ⚠️ playRaw 在 begin() 前调用会失败但不崩；begin 由首次 pump 惰性执行。
static void sfxPump(void) {
#ifdef ARDUINO_M5STACK_TAB5
  static bool begun = false;
  if (!begun) { M5.Speaker.begin(); M5.Speaker.setVolume(200); begun = true;
    // 根因修复（v0.4.4）：ES8388 时钟寄存器必须在 I2S MCLK 已运行后写入，
    // PLL 才能锁定；M5Unified begin() 回调写表时 MCLK 未起 → "寄存器全绿
    // 但完全静音"（v0.4.x + M5Burner v0.4.0 全哑的根因）。begin() 后立即
    // 重跑一次寄存器表 → PLL 锁定，tone/playRaw 出声（真机 A/B 实测）。
    // ⚠️ 仅限这一次：流停止后重刷会令 PLL 失锁（探针 C 段实测哑）。
    // 官方 esp8388_codec init 序列（DAC-only, I2S slave, ratio=256）——
    // 真机参考：retro-go-tab5 / M5Tab5-UserDemo / tab5-backrooms 全部走
    // esp_codec_dev+此序列，实测有声。关键：0x27=0x90/0x2A=0x90 把 DAC
    // 路由到 mixer——M5Unified Tab5 回调表没写这两条，冷启动必静音
    // （此前"有时能响"是别的固件残留寄存器没断电的假象）。
    static const uint8_t es8388_bulk[][2] = {
      {0x19, 0x04},   // DAC mute + soft ramp
      {0x01, 0x50},   // CONTROL2
      {0x02, 0x00},   // CHIPPOWER: power up all
      {0x35, 0xA0},   // DLL（低采样率稳定性）
      {0x37, 0xD0},
      {0x39, 0xD0},
      {0x08, 0x00},   // MASTERMODE=0 → codec I2S SLAVE
      {0x04, 0xC0},   // DACPOWER: 全关先
      {0x00, 0x12},   // CONTROL1: play mode
      {0x17, 0x18},   // DACCONTROL1: I2S 16bit
      {0x18, 0x02},   // DACCONTROL2: single speed, MCLK ratio=256
      {0x26, 0x00},   // DACCONTROL16: LIN1/RIN1
      {0x27, 0x90},   // ★ DACCONTROL17: L DAC→mixer 0dB
      {0x2A, 0x90},   // ★ DACCONTROL20: R DAC→mixer 0dB
      {0x2B, 0x80},   // DACCONTROL21: internal LRCK from ADC side
      {0x2D, 0x00},   // DACCONTROL23: vroi=0
      {0x1A, 0x00},   // DAC digital vol L=0dB
      {0x1B, 0x00},   // DAC digital vol R=0dB
      {0x2E, 0x1E},   // Lout1 vol 0dB
      {0x2F, 0x1E},   // Rout1 vol 0dB
      {0x30, 0x00},   // Lout2 off
      {0x31, 0x00},   // Rout2 off
      {0x04, 0x3C},   // ★ DACPOWER: enable DAC + Lout/Rout
      {0x03, 0xFF},   // ADC power down（不用麦克风，省电）
      {0x19, 0x00},   // unmute + ramp → 出声
    };
    for (auto& r : es8388_bulk) M5.In_I2C.writeRegister8(0x10, r[0], r[1], 400000);
    M5.In_I2C.writeRegister8(0x43, 0x05, M5.In_I2C.readRegister8(0x43, 0x05, 400000) | 0x02, 400000);   // SPK_EN(bit1)=高

    Serial.printf("[SFX] begin+rerun done | dacpwr=%02X clk1=%02X out=%02X volOK\n",
                  M5.In_I2C.readRegister8(0x10, 0x04, 400000),
                  M5.In_I2C.readRegister8(0x10, 0x01, 400000),
                  M5.In_I2C.readRegister8(0x43, 0x05, 400000));
  }
  int budget = 3;                           // 每帧最多起播3条：混音器也怕齐射尖峰
  while (budget-- > 0 && s_qTail != s_qHead) {
    uint8_t id = s_q[s_qTail];
    s_qTail = (uint8_t)((s_qTail + 1) % SFX_QUEUE);
    switch (id) {
      case SFXID_GUN:    SFX_PLAY(SFX_GUN, SFX_GUN_LEN); break;
      case SFXID_GUN2:   SFX_PLAY(SFX_GUN2, SFX_GUN2_LEN); break;
      case SFXID_GUN3:   SFX_PLAY(SFX_GUN3, SFX_GUN3_LEN); break;
      case SFXID_CANNON: SFX_PLAY(SFX_CANNON, SFX_CANNON_LEN); break;
      case SFXID_BOOM:   SFX_PLAY(SFX_BOOM, SFX_BOOM_LEN); break;
      case SFXID_LASER:  SFX_PLAY(SFX_LASER, SFX_LASER_LEN); break;
      case SFXID_FROST:  SFX_PLAY(SFX_FROST, SFX_FROST_LEN); break;
      case SFXID_COIN:   SFX_PLAY(SFX_COIN, SFX_COIN_LEN); break;
      case SFXID_HIT:    SFX_PLAY(SFX_HIT, SFX_HIT_LEN); break;
      case SFXID_LEAK:   SFX_PLAY(SFX_LEAK, SFX_LEAK_LEN); break;
      case SFXID_CLEAR:  SFX_PLAY(SFX_CLEAR, SFX_CLEAR_LEN); break;
      case SFXID_ROTOR:  SFX_PLAY(SFX_ROTOR, SFX_ROTOR_LEN); break;
      case SFXID_HORN:   SFX_PLAY(SFX_HORN, SFX_HORN_LEN); break;
      case SFXID_EMP:    SFX_PLAY(SFX_EMP, SFX_EMP_LEN); break;
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
