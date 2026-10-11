/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 andjiang0083
 */
// ============================================================================
// PIXEL TOWER DEFENSE — M5Stack Tab5 (ESP32-P4)  v0.4.0
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
#include "pixel_text.h"     // 5x7 点阵文字（逐位 fillRect）
#include "game.h"           // Enemy/Tower/Bullet/Fx + gameUpdate()
#include "touch.h"          // readTap()：非阻塞 tap-on-release
#include "render.h"         // drawBackground / renderFrame
#include "actions.h"        // handleTap / updateBuildFx / updateOverlays

static Preferences s_prefs;

// 进度落盘：把解锁/通关位图写进 NVS（actions.h 在通关时回调）
// v0.4：3bit × 20 关 = 60bit > 32bit → 拆两个键（stars=L1-10，stars2=L11-20）
static uint32_t packStars(int base) {
  uint32_t p = 0;
  for (int i = base; i < LEVEL_COUNT && i < base + 10; ++i)
    p |= (uint32_t)(g_stars[i] & 7) << ((i - base) * 3);
  return p;
}

static void saveProgress(void) {
  s_prefs.begin("td", false);
  s_prefs.putUInt("unlock", g_unlocked);
  s_prefs.putUInt("clear",  g_cleared);
  s_prefs.putUInt("stars",  packStars(0));
  s_prefs.putUInt("stars2", packStars(10));
  s_prefs.putUChar("mute", s_muted ? 1 : 0);   // v0.3.1：静音偏好
  s_prefs.end();
}

// ── 帧率控制：30fps 足够，显著降低 PSRAM 填充压力 ──────────────────
static const uint32_t FRAME_MS = 33;

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

  Serial.printf("=== PIXEL TOWER DEFENSE v0.4.0 | board=%d | %dx%d ===\n",
                (int)M5.getBoard(), SCREEN_W, SCREEN_H);

  // PSRAM 是帧缓冲所在地（DSI video mode）；没有就停下，别黑屏空转
  if (!psramFound()) {
    Serial.println("FATAL: no PSRAM - enable octal PSRAM @200MHz in platformio.ini");
    while (true) delay(1000);
  }

  // ── v0.2.0：读进度 → 进选关界面（唯一开局入口）───────────────
  s_prefs.begin("td", true);
  g_unlocked = s_prefs.getUInt("unlock", 0x0001) | 0x0001;      // L1 永远解锁（v0.4：uint32）
  g_cleared  = s_prefs.getUInt("clear", 0);
  uint32_t packed = s_prefs.getUInt("stars", 0);               // v0.3.0：旧档无此键=0
  uint32_t packed2 = s_prefs.getUInt("stars2", 0);             // v0.4：L11-20
  sfxSetMuted(s_prefs.getUChar("mute", 0) != 0);              // v0.3.1
  s_prefs.end();
  for (int i = 0; i < LEVEL_COUNT && i < 10; ++i)  g_stars[i] = (uint8_t)((packed >> (i * 3)) & 7);
  for (int i = 10; i < LEVEL_COUNT && i < 20; ++i) g_stars[i] = (uint8_t)((packed2 >> ((i - 10) * 3)) & 7);
  g_saveProgress = saveProgress;

  levelLoad(0);                       // 预载 L1（选关前世界已就绪）
  g_w.state = ST_SEL;               // 开局先进选关界面
  drawLevelSelect(&M5.Display);

  Serial.printf("unlock=%04X cleared=%04X levels=%d\n",
                g_unlocked, g_cleared, LEVEL_COUNT);
  Serial.println("tap node = enter level | tap SLOT then MAP = build");
  Serial.println("tap TOWER = upgrade panel (pauses) | NEXT WAVE = fight");

  delay(1200);                            // 让开局界面可见
}

// ============================================================================
// LOOP — 输入 → 逻辑 → 渲染（顺序固定，每帧一次）
// ============================================================================
void loop(void) {
  static uint32_t lastFrame = 0;
  esp_task_wdt_reset();

  Tap t = readTap();                      // 非阻塞：按住屏幕不会卡住世界
  if (t.valid) handleTap(&M5.Display, t);

  // 选关界面：静态画面，无世界逻辑（handleTap 已处理跳转）
  if (g_w.state == ST_SEL) { delay(20); return; }

  uint32_t now = millis();
  uint32_t dt = now - lastFrame;
  if (dt < FRAME_MS) { delay(1); return; }   // 未到下一帧：让出 CPU
  if (dt > 60) dt = 60;                      // 限幅：防止卡顿后敌人瞬移
  lastFrame = now;

  gameUpdate(dt);                            // ── 逻辑（不碰绘制）──
  renderFrame(&M5.Display);                  // ── 渲染（不碰逻辑）──
  updateBuildFx(&M5.Display);              // ── 建造反馈动画 ──
  updateOverlays(&M5.Display);             // ── Game Over 覆盖层（只画一次）──

  // v0.3.1：音效派发放帧间隙（渲染之后，上次实测音频 ~4ms/帧塞得进）
  s_nowMs = now;
  sfxPump();

  // 通关瞬间落盘一次（不等点击，防断电丢进度；位图去重避免反复写 NVS）
  static uint16_t s_savedUnlock = 0xFFFF, s_savedClear = 0xFFFF;
  if (g_w.state == ST_CLEAR && (s_savedUnlock != g_unlocked || s_savedClear != g_cleared)) {
    s_savedUnlock = g_unlocked; s_savedClear = g_cleared;
    saveProgress();
  }
}
