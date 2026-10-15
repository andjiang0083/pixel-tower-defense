/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 andjiang0083
 */
// i18n.h — 界面文本表（v0.5.0：英文单一语言）
// ----------------------------------------------------------------------------
// ⚠️ v0.5.0（Claw4 移植）决定：**移除中文支持，界面只保留英文**。
//
// 为什么删而不是"窄屏降级"：
//   v0.4.2 起的中文走 cn_font.h 的 **16x16 固定字形**（px_text.h: CN_ADV =
//   17px/字），且**不随 scale 缩小**（CN_FONT_H 固定 16）。于是 4 个汉字
//   = 68px 起，在 Claw4 的 116px 建造栏槽 / 202px 塔面板里必然溢出；
//   而 720x720 的屏又没有余量把槽和面板加宽到能放下。
//   v0.5.0 曾加过 cnRenderable() 窄屏自动降级，但那只是把中文**藏起来**——
//   表、字库（17.7KB Flash）、生成器、CI 检查全都还在，是一份永远走不到的
//   死代码。既然 Claw4 才是当前的真机目标，就把它整条删掉。
//
// 删掉了什么（一并从仓库移除，不留孤儿）：
//   · STR_CN[] 文案表 与 LANG_CN
//   · src/cn_font.h + tools/gen_cnfont.py / check_cnfont.py / cn_glyph_rule.py
//   · px_text.h 的 UTF-8 高位字节分支（px_cnChar / CN_ADV / s_cnMissing）
//   · 标题界面的语言切换按钮（EN/中文 两个都要点，去掉后整组移除）
//   · NVS "lang" 键的存读
//   · CI 的 cn_font 语义覆盖检查
//
// TR() 保留：它是界面文本的**唯一入口**，40+ 处调用点不动。
//  将来若恢复双语，只需补回 STR_<lang> 表与 px_cnChar，TR() 内部切换即可。
//
// 关卡名 / BOSS 名 / 塔名是 ASCII 专名，不翻译（保持像素字统一）。
// ----------------------------------------------------------------------------
#pragma once
#include <stdint.h>

enum StrId : uint8_t {
  STR_TITLE_HINT = 0,   // 标题界面：点击开始
  STR_HP,               // HUD 生命标签
  STR_WAVE,             // HUD 波次标签
  STR_FIGHTING,         // 按钮：战斗中
  STR_NEXT_WAVE,        // 按钮：下一波
  STR_TAP_SLOT,         // 建造栏提示
  STR_THEN_MAP,
  STR_TAP_TOWER,
  STR_NEED,             // 建造栏：金币不足
  STR_DMG,              // 面板属性
  STR_RNG,
  STR_SLOW,
  STR_PULSE,
  STR_SPLASH,
  STR_SINGLE,
  STR_NO_ARMOR,
  STR_NO_ATTACK,
  STR_UPGRADE,          // 面板按钮
  STR_SELL,
  STR_MAX,
  STR_LEVEL,
  STR_NEED_GOLD,
  STR_SELECT,           // 选关标题
  STR_UNLOCK_HINT,      // 选关底部提示
  STR_GAME_OVER,        // 结算
  STR_LEVEL_CLEAR,
  STR_KILLS,
  STR_GOLD_WORD,
  STR_WAVE_WORD,
  STR_TAP_RESTART,
  STR_TAP_NEXT,
  STR_ALL_CLEAR,
  STR_BOSS_DOWN,
  STR_ENDLESS,          // 无尽模式按钮
  STR_ENDLESS_DESC,
  STR_BEST,             // HUD 无尽最高波次
  STR_ONBOARD_1,        // 首次引导三步
  STR_ONBOARD_2,
  STR_ONBOARD_3,
  STR_ONBOARD_TAP,
  STR_SIEGE_WARN,       // BOSS 封锁建造区提示
  STR_COUNT
};

static const char* const STR_EN[STR_COUNT] = {
  "TAP TO START",
  "HP", "WAVE", "FIGHTING", "NEXT WAVE",
  "TAP SLOT", "THEN MAP", "TAP TOWER=UP", "NEED",
  "DMG", "RNG", "SLOW", "PULSE", "SPLASH", "SINGLE", "NO-ARMOR",
  "NO ATTACK - EARNS GOLD",
  "UPGRADE", "SELL", "MAX", "LEVEL", "NEED GOLD",
  "SELECT LEVEL", "CLEAR A LEVEL TO UNLOCK THE NEXT",
  "GAME OVER", "LEVEL CLEAR", "KILLS ", "GOLD ", "WAVE ",
  "TAP TO RESTART", "TAP FOR NEXT LEVEL", "ALL CLEAR - TAP TO REPLAY",
  "BOSS DOWN: ",
  "ENDLESS MODE", "SURVIVE 250 WAVES", "BEST",
  "STEP 1/3 - TAP GRASS TO BUILD A TOWER",
  "STEP 2/3 - TAP NEXT WAVE TO FIGHT",
  "STEP 3/3 - STOP EVERY ENEMY!",
  "TAP TO CONTINUE",
  "BUILD ZONE LOCKED",
};

// 取界面文本（v0.5.0：英文单一语言，TR 是唯一入口）
static inline const char* TR(StrId id) { return STR_EN[id]; }
