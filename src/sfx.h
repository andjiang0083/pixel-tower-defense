/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 andjiang0083
 */
// sfx.h — 程序化音效运行时：事件环形队列 + 帧间隙派发 + 节流防吵
// 音频数据零文件资源：tools/gen_sfx.py 合成 → sfx_data.h。
//
// ── v0.5.0（Claw4 移植）：平台后端从"单一 M5Unified"改为三选一 ─────────
// 原来只有两个分支：定义 ARDUINO_M5STACK_TAB5 走 M5.Speaker.playRaw，
// 否则走 PC 仿真空实现。Claw4 是第三种平台（ESP-IDF + esp_lcd/I2S），
// 于是把"有真机硬件"与"用哪个硬件"拆成两个宏：
//   PTD_AUDIO_HW   是否有真机音频后端（真机固件定义，仿真不定义）
//   PTD_AUDIO_API  用哪个实现（PRAGMA_ONCE 在下面各平台段里定义）
// 这样 game.h 只认 sfxPost()/sfxPump()，对新平台零感知——这正是原作者
// "游戏逻辑零感知硬件"的设计意图，此前只是平台只有一个所以没显式化。
//
// 三个后端：
//   PTD_AUDIO_M5UNIFIED  Tab5（M5Unified 的 playRaw，自带混音任务，多轨重叠 OK）
//   PTD_AUDIO_CLAW4      Claw4（main/bsp_claw4.c 的 audioPlayRaw，I2S 直写）
//   都不定义              PC 仿真：排空队列并记账，断言读 s_simPlay[]
//
// ⚠️ Tab5 那120 行 ES8388 I2C 寄存器 bulk 写入是 Tab5 专属 hack
//   （修 PLL 不锁定），**一行都不能带到 Claw4**——Claw4 的 codec 控制方式
//   完全不同（I2S + 独立 BT audio codec）。
#pragma once
#include <stdint.h>
#include "config_gen.h"     // SCREEN_W/H（静音按钮几何）

#include "sfx_data.h"

// ── 平台后端选择 ─────────────────────────────────────────────────────
// PTD_AUDIO_M5UNIFIED 与 PTD_AUDIO_CLAW4 由构建系统定义其一。
#if defined(ARDUINO_M5STACK_TAB5) && !defined(PTD_AUDIO_M5UNIFIED) && !defined(PTD_AUDIO_CLAW4)
  #define PTD_AUDIO_M5UNIFIED 1
#endif

#if defined(PTD_AUDIO_M5UNIFIED) || defined(PTD_AUDIO_CLAW4)
  #define PTD_AUDIO_HW 1          // 有真机音频后端
#else
  #define PTD_AUDIO_HW 0          // PC 仿真
#endif

#ifdef PTD_AUDIO_M5UNIFIED
#include <M5Unified.h>
#endif
#ifdef PTD_AUDIO_CLAW4
  // Claw4 的音频 API 由 BSP 提供（main/bsp_claw4.h，已用 extern "C" 包裹）。
  // 这里不再重复声明——重复声明若链接属性不一致（一个是 C、一个是 C++）
  // 会直接报 "conflicting declaration ... with 'C' linkage"。
  // sfx.h 保持不 include BSP 头，从而对 BSP 的头路径零依赖。
#endif

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
#if !PTD_AUDIO_HW
static uint32_t s_simPlay[SFXID_COUNT];    // 仿真记账：每音效"起播"次数
#endif

// v0.5.3 性能探针（默认关，PTD_PERF_PROBE 未定义 = 0 ⇒ 这行不参与编译）：
// 数"真的起播了几条 PCM"。判定"音效洪峰是不是卡顿源"需要的是**起播数**，
// 不是投递数——投递早在 sfxPost 里被节流掉一大半，只看投递会低估。
#ifndef PTD_PERF_PROBE
#define PTD_PERF_PROBE 0
#endif
#if PTD_PERF_PROBE
static uint32_t s_probePlayed = 0;
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

#ifdef PTD_AUDIO_M5UNIFIED
// Tab5：M5Unified 的 playRaw 自带混音任务，多轨重叠 OK。
// force=true 排除"流忙丢帧"（齐射时强占）。
static void sfxPlay(const int16_t* d, uint32_t len) {
  M5.Speaker.playRaw(d, len, SFX_SR, false, 1, -1, true);
}
#define SFX_PLAY(d, len) sfxPlay((d), (len))
#elif defined(PTD_AUDIO_CLAW4)
// Claw4：BSP 的 audioPlayRaw（I2S 直写，阻塞但带 200ms 超时，绝不卡死主循环）。
// sfx_data.h 是 16kHz PCM，与 Claw4 的 I2S 16kHz 天然对齐，无需重采样。
#define SFX_PLAY(d, len) audioPlayRaw((d), (len), SFX_SR)
#endif

// ── 派发：主循环帧间隙调用（渲染之后）。每轮最多 N 条防尖峰 ──────
// ⚠️ playRaw 在 begin() 前调用会失败但不崩；begin 由首次 pump 惰性执行。
static void sfxPump(void) {
#ifdef PTD_AUDIO_M5UNIFIED
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
#if PTD_PERF_PROBE
    ++s_probePlayed;
#endif
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
#elif defined(PTD_AUDIO_CLAW4)
  // Claw4：audioInit() 已在 main 里做过（含 I2S 通道启用）。这里只做音量设置，
  // 惰性执行一次——音效可能在 audioInit 之前就被投递（虽然本项目不会，
  // 但保持与 Tab5 同款的"幂等 begin"语义，避免顺序耦合）。
  static bool begun = false;
  if (!begun) { audioSetVolume(70); begun = true; }
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
