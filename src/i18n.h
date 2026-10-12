/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 andjiang0083
 */
// i18n.h — 界面文本双语表（v0.4.2）
// ----------------------------------------------------------------------------
// 语言在标题界面选择，NVS 记忆（main.cpp 键 "lang"），默认英文（开源默认）。
// 中文渲染不走 ASCII 点阵：pixel_text.h 遇到 UTF-8 高位字节自动切
// cn_font.h（16x16，由 tools/gen_cnfont.py 从**本文件 CN 列**自动收字生成）。
// ⚠️ 新增界面文本时必须：① 在两张表同序补条目；② 重跑 gen_cnfont.py
//    （CI 漂移检查会抓没重跑的情况）。
// 关卡名 / BOSS 名 / 塔名是 ASCII 专名，不翻译（保持像素字统一）。
// ----------------------------------------------------------------------------
#pragma once
#include <stdint.h>

enum LangId : uint8_t { LANG_EN = 0, LANG_CN = 1, LANG_COUNT = 2 };

enum StrId : uint8_t {
  STR_TITLE_HINT = 0,   // 标题界面：点击开始
  STR_VERSION,          // "版本"
  STR_LANG,             // "语言"
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
  STR_READY_HINT,       // 开局提示条
  STR_SIEGE_WARN,       // BOSS 封锁建造区提示
  STR_COUNT
};

// 两张表必须同序同量（gen_cnfont.py 与编译期都会间接校验）
static const char* const STR_EN[STR_COUNT] = {
  "TAP TO START", "VERSION", "LANGUAGE",
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
  "TAP GRASS TO BUILD TOWER",
  "BUILD ZONE LOCKED",
};

static const char* const STR_CN[STR_COUNT] = {
  "点击开始", "版本", "语言",
  "生命", "波次", "战斗中", "下一波",
  "点塔位", "再点草地", "点塔=升级", "金币不足",
  "伤害", "射程", "减速", "脉冲", "溅射", "单体", "无视护甲",
  "不攻击 - 产金币",
  "升级", "卖出", "满", "级", "金币不足",
  "选择关卡", "通关后解锁下一关",
  "游戏结束", "关卡通关", "击杀 ", "金币 ", "波次 ",
  "点击重开", "点击下一关", "全部通关 - 点击重玩",
  "已击败：",
  "无尽模式", "挑战二百五十波", "最高",
  "第1步/3 - 点草地建塔",
  "第2步/3 - 点下一波开战",
  "第3步/3 - 拦住所有敌人！",
  "点击继续",
  "点草地建塔",
  "建造区被封锁",
};

// ── 运行时选择 ─────────────────────────────────────────────────────
static uint8_t g_lang = LANG_EN;
static inline void setLang(uint8_t l) { g_lang = (l < LANG_COUNT) ? l : LANG_EN; }
static inline bool langCN(void) { return g_lang == LANG_CN; }
// 取当前语言文本（界面唯一入口）
static inline const char* TR(StrId id) {
  return (g_lang == LANG_CN) ? STR_CN[id] : STR_EN[id];
}
