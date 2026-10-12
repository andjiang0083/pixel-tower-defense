/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 andjiang0083
 */
// ============================================================================
// PIXEL TOWER DEFENSE — M5Stack Tab5 (ESP32-P4)  v0.4.3
// ----------------------------------------------------------------------------
// ⚠️ 为什么不是 TFT_eSPI（重要）：
//   Tab5 的屏是 MIPI DSI（ILI9881C / ST7123），不是 SPI/QSPI。ESP32-P4 的
//   DSI 控制器只支持 Video Mode：帧缓冲常驻 PSRAM、由 DMA 连续扫描输出，
//   不存在"把矩形写进屏幕 GRAM"的通路 —— TFT_eSPI 只实现 SPI 时序，
//   因此在 Tab5 上无法工作（上游同样结论：Bodmer/TFT_eSPI issue #3719）。
//   Arduino 生态下 Tab5 唯一可用的显示层是 M5GFX（内部走 ESP-IDF esp_lcd/DSI）。
//   本项目仍然只用 fillRect / drawPixel 手画像素方块，不加载任何图片/位图，
//   连数字字母都是自带的 5x7 点阵（src/pixel_font.h，由 tools/gen_font.py 生成）。
//   ⚠️ M5GFX/M5Unified 的内置字体是位图资源，本项目刻意不使用它们。
//
// 编译配置（PlatformIO / pioarduino —— 不要用 Arduino IDE + TFT_eSPI）：
//   1) brew install platformio
//   2) board = esp32-p4-evboard + -DARDUINO_M5STACK_TAB5（见 platformio.ini），
//      让 M5GFX 自动识别 Tab5 的面板（ST7123/ILI9881C）与触控（GT911/ST7123）
//   3) 必须开 PSRAM：board_build.psram = enable / oct @200MHz —— 帧缓冲所在地，
//      不满足时 M5GFX 直接报 "M5Tab5 need PSRAM SPEED 200MHz"
//   4) pio run -e tab5 && pio run -e tab5 -t upload && pio device monitor
//   5) Tab5 无用户物理按键，全部交互走触摸屏（与本作操作方式一致）
// ----------------------------------------------------------------------------
// 模块划分：touch.h 采样 / actions.h 规则落地 / game.h 逻辑更新 / render.h 渲染
// PC 仿真（sim/）编译的是同一份 game/render/actions/touch，已跑通 25 项断言。
// ============================================================================

#include <M5Unified.h>
#include <esp_task_wdt.h>
#include <Preferences.h>       // v0.2.0：关卡解锁进度持久化到 NVS

#include "config_gen.h"     // 生成的网格/路径/数值（真源 tools/layout.py）
#include "pixel_text.h"     // 5x7 点阵文字 + v0.4.2 中文 16x16（逐位 fillRect）
#include "i18n.h"           // v0.4.2：界面文本双语表（标题界面选择语言）
#include "game.h"           // Enemy/Tower/Bullet/Fx + gameUpdate()
#include "touch.h"          // readTap()：非阻塞 tap-on-release
#include "render.h"         // drawBackground / renderFrame / drawTitle
#include "actions.h"        // handleTap / updateBuildFx / updateOverlays（内含 progress.h）

static Preferences s_prefs;

// v0.4.3：引导 overlay 的重画去重（见 loop 里的说明）
static int8_t s_lastOnboardStep = 0;

// 进度落盘：把解锁/通关/星级/语言/引导/无尽记录写进 NVS
// （v0.4.1：去重逻辑在 progress.h，由仿真断言"通关画面只写一次"）
static void saveProgress(void) {
  s_prefs.begin("td", false);
  s_prefs.putUInt("unlock", g_unlocked);
  s_prefs.putUInt("clear",  g_cleared);
  s_prefs.putUInt("stars",  packStars(0));
  s_prefs.putUInt("stars2", packStars(10));
  s_prefs.putUChar("mute", s_muted ? 1 : 0);   // v0.3.1：静音偏好
  s_prefs.putUChar("lang", (uint8_t)g_lang);   // v0.4.2：界面语言
  s_prefs.putUChar("onboarded", g_onboarded ? 1 : 0);  // v0.4.2：引导只讲一次
  s_prefs.putUShort("endbest", (uint16_t)g_endBest);   // v0.4.2：无尽最高波次
  s_prefs.end();
}

// ── 帧率控制：30fps 足够，显著降低 PSRAM 填充压力 ──────────────────
static const uint32_t FRAME_MS = 33;

// ── v0.4.3：ES8388 编解码器 / 功放 硬件层校验 + 兜底重初始化 ────────
// 背景：M5Unified 的 _speaker_enabled_cb_tab5 把 ES8388 寄存器序列和功放开关
// 写在 M5.Speaker.begin() 里，但 begin() 可能被 M5.begin() 抢先触发、那时
// In_I2C 还没稳，I2C 写静默失败（返回值被丢弃）→ 编解码器没编程 / 功放没开 →
// 完全没声音，而 playRaw 仍返回成功（软件层看不出问题）。这里在 In_I2C 稳定后
// 用同一套已对照数据手册核实过的寄存器值重跑一遍，并回读确认到底写没写进去。
// 寄存器序列与 M5Unified _speaker_enabled_cb_tab5 完全一致：
//   23=0x18 → 16-bit I2S 从机；25=0x20 → DAC 解除静音；
//   4=0x3C → DAC+四路输出上电；46..49=0x21 → 输出音量 +4.5dB（最大）。
static void audioEnsureHardware(void) {
#if defined (ARDUINO_M5STACK_TAB5)
  static constexpr uint8_t ES8388 = 0x10;   // ES8388 codec I2C 地址
  static constexpr uint8_t PI4IOE = 0x43;   // PI4IOE1 I/O 扩展器（功放使能）
  static const uint8_t regs[][2] = {
    {0,0x80},{0,0x00},{0,0x00},{0,0x0E},
    {1,0x00},{2,0x0A},{3,0xFF},{4,0x3C},
    {5,0x00},{6,0x00},{7,0x7C},{8,0x00},
    {23,0x18},{24,0x00},{25,0x20},{26,0x00},{27,0x00},
    {28,0x08},{29,0x00},{38,0x00},{39,0xB8},{42,0xB8},
    {43,0x08},{45,0x00},{46,0x21},{47,0x21},{48,0x21},{49,0x21},
  };
  int okW = 0, failW = 0;
  for (auto& r : regs) {
    if (M5.In_I2C.writeRegister8(ES8388, r[0], r[1], 100000)) ++okW; else ++failW;
  }
  bool amp = M5.In_I2C.bitOn(PI4IOE, 0x05, 0b00000010, 400000);  // 功放使能位
  // 回读：注意 readRegister8 在 NAK 时返回 0，故以"写是否 ACK"为准，回读仅作佐证
  uint8_t rd_dacpwr = M5.In_I2C.readRegister8(ES8388, 4,  100000);
  uint8_t rd_fmt    = M5.In_I2C.readRegister8(ES8388, 23, 100000);
  uint8_t rd_mute   = M5.In_I2C.readRegister8(ES8388, 25, 100000);
  uint8_t rd_vol    = M5.In_I2C.readRegister8(ES8388, 46, 100000);
  uint8_t rd_amp    = M5.In_I2C.readRegister8(PI4IOE, 5, 400000);
  Serial.printf("[AUDHW] es8388 writes ok=%d fail=%d | dacpwr=%02X fmt=%02X mute=%02X vol=%02X | amp_bitOn=%d ampReg=%02X\n",
                okW, failW, rd_dacpwr, rd_fmt, rd_mute, rd_vol, (int)amp, rd_amp);
#endif
}

// ============================================================================
// SETUP
// ============================================================================
void setup(void) {
  Serial.begin(115200);
  // USB-CDC 写阻塞会饿死 task WDT（关掉串口监视器就能复现）：超时=0 丢字节
  Serial.setTxTimeoutMs(0);

  esp_task_wdt_config_t twdt = { .timeout_ms = 8000,
                                  .idle_core_mask = (1 << 0) | (1 << 1),
                                  .trigger_panic = true };
  esp_task_wdt_init(&twdt);
  esp_task_wdt_add(NULL);

  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(3);              // Tab5 横屏 1280x720

  Serial.printf("=== PIXEL TOWER DEFENSE v0.4.3 | board=%d | %dx%d ===\n",
                (int)M5.getBoard(), SCREEN_W, SCREEN_H);

  // PSRAM 是帧缓冲所在地（DSI video mode）；没有就停下，别黑屏空转
  if (!psramFound()) {
    Serial.println("FATAL: no PSRAM - enable octal PSRAM @200MHz in platformio.ini");
    while (true) delay(1000);
  }

  // ── v0.2.0：读进度 → 标题界面（v0.4.2 起开机第一屏是标题）─────
  s_prefs.begin("td", true);
  g_unlocked = s_prefs.getUInt("unlock", 0x0001) | 0x0001;      // L1 永远解锁（v0.4：uint32）
  g_cleared  = s_prefs.getUInt("clear", 0);
  uint32_t packed = s_prefs.getUInt("stars", 0);               // v0.3.0：旧档无此键=0
  uint32_t packed2 = s_prefs.getUInt("stars2", 0);             // v0.4：L11-20
  sfxSetMuted(s_prefs.getUChar("mute", 0) != 0);              // v0.3.1
  setLang(s_prefs.getUChar("lang", LANG_EN));                 // v0.4.2：界面语言（默认英文）
  g_onboarded = s_prefs.getUChar("onboarded", 0) != 0;        // v0.4.2：引导只讲一次
  g_endBest   = (int16_t)s_prefs.getUShort("endbest", 0);     // v0.4.2：无尽最高波次
  s_prefs.end();
  for (int i = 0; i < LEVEL_COUNT && i < 10; ++i)  g_stars[i] = (uint8_t)((packed >> (i * 3)) & 7);
  for (int i = 10; i < LEVEL_COUNT && i < 20; ++i) g_stars[i] = (uint8_t)((packed2 >> ((i - 10) * 3)) & 7);
  g_saveProgress = saveProgress;

  levelLoad(0);                       // 预载 L1（选关前世界已就绪）
  g_w.state = ST_TITLE;               // v0.4.2：开机先进标题界面
  drawTitle(&M5.Display);

  Serial.printf("unlock=%04X cleared=%04X levels=%d lang=%s endbest=%d\n",
                g_unlocked, g_cleared, LEVEL_COUNT,
                langCN() ? "CN" : "EN", (int)g_endBest);
  Serial.println("tap language = switch EN/中文 | tap = start");

  // ── v0.4.3 音频诊断 + 硬件兜底初始化（定位/修复"没声音"）──────────
  // 先打印 M5Unified 给的 I2S 配置，再 begin()，再用 audioEnsureHardware()
  // 在 In_I2C 稳定后重跑 ES8388/功放初始化并回读校验（写 ACK 情况一目了然）。
  {
    auto sc = M5.Speaker.config();
    Serial.printf("[AUD] board=%d dout=%d bck=%d ws=%d mck=%d sr=%u mag=%u port=%d stereo=%d\n",
                  (int)M5.getBoard(), sc.pin_data_out, sc.pin_bck, sc.pin_ws,
                  sc.pin_mck, (unsigned)sc.sample_rate, (unsigned)sc.magnification,
                  (int)sc.i2s_port, (int)sc.stereo);
    Serial.printf("[AUD] isEnabled=%d muted=%d\n",
                  (int)M5.Speaker.isEnabled(), (int)sfxIsMuted());
    const bool b = M5.Speaker.begin();
    Serial.printf("[AUD] begin=%d isRunning=%d\n", (int)b, (int)M5.Speaker.isRunning());
    M5.Speaker.setVolume(255);                       // 诊断期间直接拉满
    audioEnsureHardware();                           // 硬件层兜底重初始化 + 回读
    // 让用户能直接听到结果：1kHz 持续 2s（若仍无声，看 [AUDHW] 回读判断卡在哪一环）
    const bool ta = M5.Speaker.tone(1000.0f, 2000, 0, true);
    Serial.printf("[AUD] toneA(1kHz,2s)=%d playing=%d\n", (int)ta, (int)M5.Speaker.isPlaying());
    delay(3000);
  }

  delay(1200);                            // 让开局界面可见
}

// ============================================================================
// LOOP — 输入 → 逻辑 → 渲染（顺序固定，每帧一次）
// ============================================================================
void loop(void) {
  static uint32_t lastFrame = 0;
  esp_task_wdt_reset();

  // ── v0.4.3 音频诊断心跳（定位完即删）────────────────────────────
  // 放在 loop 最前：标题态会提前 return，放末尾就永远打不出来（踩过一次）。
  // 四段收支分开记账：post / 静音丢 / 节流丢 / 队列满丢 / pump / 起播成功 / 失败
  {
    static uint32_t adMs = 0;
    const uint32_t adNow = millis();
    if (adNow - adMs >= 5000) {
      adMs = adNow;
      uint32_t o[7];
      sfxDiagGet(o);
#if defined (ARDUINO_M5STACK_TAB5)
      uint8_t hw_dacpwr = M5.In_I2C.readRegister8(0x10, 4, 100000);
      uint8_t hw_amp    = M5.In_I2C.readRegister8(0x43, 5, 400000);
#else
      uint8_t hw_dacpwr = 0, hw_amp = 0;
#endif
      Serial.printf("[AUD] t=%lums st=%d post=%lu muteDrop=%lu thrDrop=%lu fullDrop=%lu pump=%lu qOK=%lu qFail=%lu vol=%d playing=%d | hw dacpwr=%02X amp=%02X\n",
                    (unsigned long)adNow, (int)g_w.state,
                    (unsigned long)o[0], (unsigned long)o[1], (unsigned long)o[2],
                    (unsigned long)o[3], (unsigned long)o[4], (unsigned long)o[5],
                    (unsigned long)o[6],
                    (int)M5.Speaker.getVolume(), (int)M5.Speaker.isPlaying(),
                    (unsigned)hw_dacpwr, (unsigned)hw_amp);
    }
  }

  Tap t = readTap();                      // 非阻塞：按住屏幕不会卡住世界
  if (t.valid) handleTap(&M5.Display, t);

  // 标题/选关界面：静态画面，无世界逻辑（handleTap 已处理跳转）；
  // v0.4.1：锁关红叉反馈在选关界面推进（20 帧后自擦）
  if (g_w.state == ST_TITLE) { delay(20); return; }
  if (g_w.state == ST_SEL) { updateSelFx(&M5.Display); delay(20); return; }

  uint32_t now = millis();
  uint32_t dt = now - lastFrame;
  if (dt < FRAME_MS) { delay(1); return; }   // 未到下一帧：让出 CPU
  if (dt > 60) dt = 60;                      // 限幅：防止卡顿后敌人瞬移
  lastFrame = now;

  gameUpdate(dt);                            // ── 逻辑（不碰绘制）──
  renderFrame(&M5.Display);                  // ── 渲染（不碰逻辑）──
  // v0.4.2：首次引导 overlay（对话框区域点击推进；NVS 记住只讲一次）
  //
  // ⚠️ v0.4.3 修掉一个真机必现崩溃：原来这里**每帧**无条件调 drawOnboard。
  //    drawOnboard 的"半透明压暗"是 8px 网格逐块 fillRect，
  //    1280/8 × 720/8 = **14400 次 fillRect/帧**（30fps ≈ 43 万次/秒），
  //    每次都写 PSRAM 里的 DSI 帧缓冲，与扫描 DMA 疯狂抢 cache，
  //    最终死锁在 esp_cache_msync 的自旋锁上：
  //      MEPC 0x4ff0a57a → vPortExitCriticalMultiCore (port.c:569)
  //      RA   0x4ff07312 → esp_cache_msync (esp_cache_msync.c:135)
  //    loopTask 卡死 > TWDT 8s → task_wdt panic → 蓝屏重启。
  //    玩家看到的就是"一点『点击继续』就死"。
  //    引导期间世界静止（ST_READY），没有东西会盖掉 overlay，
  //    因此按 step 变化重画一次即可，无需每帧重刷。
  if (g_onboardStep > 0 &&
      (g_w.state == ST_READY || g_w.state == ST_WAVE)) {
    if (g_onboardStep != s_lastOnboardStep) {
      drawOnboard(&M5.Display, g_onboardStep);
      s_lastOnboardStep = g_onboardStep;
    }
  } else {
    s_lastOnboardStep = 0;
  }
  updateBuildFx(&M5.Display);              // ── 建造反馈动画 ──
  updateOverlays(&M5.Display);             // ── Game Over 覆盖层（只画一次）──

  // v0.3.1：音效派发放帧间隙（渲染之后，上次实测音频 ~4ms/帧塞得进）
  s_nowMs = now;
  sfxPump();

  // 通关瞬间落盘一次（不等点击，防断电丢进度；v0.4.1：去重逻辑与
  // 整型宽度在 progress.h，仿真有断言——停留 N 帧也只写一次）
  progressTickOnClear();
}
