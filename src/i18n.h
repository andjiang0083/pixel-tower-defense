/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 andjiang0083
 */
// i18n.h — 界面文本表（v0.5.3：双语回归，但由 **profile 能力宏**决定开关）
// ----------------------------------------------------------------------------
// 历史与本轮修正（v0.5.3）：
//   v0.4.2 起双语（EN/中文，标题界面切换，NVS 记忆）。
//   v0.5.0（Claw4 移植）为了 720 窄屏放不下固定 17px/字的中文，**在共享层
//   把中文整条链路删掉了** —— 于是 1280x720 的 Tab5（放得下、也是对外卖点）
//   跟着一起失去双语。这是隔离形状错了：把"某块板放不下"写成了"大家都不要"。
//
//   v0.5.3 起改用**能力宏**：`PTD_HAS_CJK` 由 profile 提供
//   （tools/layout.py 的 PROFILES[*].cjk → config_gen.h）：
//     · tab5  = 1（1280x720，双语）
//     · claw4 = 0（720x720：116px 建造栏槽 / 202px 塔面板装不下 4 汉字=68px 起）
//   共享层里因此**不出现板名**，只有 `#if PTD_HAS_CJK`；将来 claw4 要开中文，
//   改 layout.py 那一格 + 重排它自己的布局即可，不必再动这里。
//
// ⚠️ 能力宏 = 0 时的形态必须与 v0.5.0~v0.5.2 的英文单语形态**逐字节等价**
//   （claw4 固件零位移门禁）：所以下面 5 条"只在双语下存在"的枚举项与表行
//   也一起被守卫，而不是留在表里当死数据。
//
// 字库真源唯一：STR_CN[] 是本文件的 CN 列 → tools/gen_cnfont.py 扫它生成
//   src/cn_font.h（16x16 位图，入库）。能力宏只决定**要不要 include** 它，
//   不改字库、不造第二份表。
//
// 关卡名 / BOSS 名 / 塔名是 ASCII 专名，不翻译（保持像素字统一）。
// ----------------------------------------------------------------------------
#pragma once
#include <stdint.h>
#include "config_gen.h"     // PTD_HAS_CJK（profile 能力宏）

enum LangId : uint8_t { LANG_EN = 0, LANG_CN = 1, LANG_COUNT = 2 };

// 界面语言数量：出图批处理 / 文本适配循环按它走。
//   ⚠️ 单语形态（claw4）自然是 1 —— 这样"遍历语言"只有一份代码，不会退化成
//      "只出英文图"（v0.5.0 削中文时就是这么静默退化的：图还在，中文那路没了）。
#if PTD_HAS_CJK
#define PTD_LANG_COUNT LANG_COUNT
#else
#define PTD_LANG_COUNT 1
#endif

enum StrId : uint8_t {
  STR_TITLE_HINT = 0,   // 标题界面：点击开始
#if PTD_HAS_CJK
  STR_TITLE_CN,         // v0.4.3：标题界面的中文标题（英文模式用 drawRainbow 画彩色 PIXEL TOWER DEFENSE，不用此条）
  STR_LANG_EN,          // v0.4.3：语言按钮标签——显示"目标语言"名（惯例）：EN 模式显示 ENGLISH/中文，CN 模式显示 英文/中文
  STR_LANG_CN,          // v0.4.3：同上（中文按钮）。两者都走表，否则"中文"二字不进字库（v0.4.2 英文模式下中文按钮是空框）
  STR_VERSION,          // 标题界面："版本"
  STR_LANG,             // 标题界面："语言"（按钮组上方的标签）
#endif
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
#if PTD_HAS_CJK
  "PIXEL TOWER DEFENSE", "ENGLISH", "\xE4\xB8\xAD\xE6\x96\x87", "VERSION", "LANGUAGE",
#endif
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

#if PTD_HAS_CJK
// ── 中文列（字库生成器的输入，见 tools/gen_cnfont.py）──────────────────
static const char* const STR_CN[STR_COUNT] = {
  "点击开始",
  "像素塔防", "英文", "中文", "版本", "语言",
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
  "建造区被封锁",
};

static uint8_t g_lang = LANG_EN;                      // 界面语言（main.cpp 从 NVS "lang" 读）
static inline void setLang(uint8_t l) { g_lang = (l < LANG_COUNT) ? l : LANG_EN; }
static inline bool langCN(void) { return g_lang == LANG_CN; }

// 取界面文本：TR() 是**唯一入口**（渲染层 42 处调用点，不关心语言）
static inline const char* TR(StrId id) {
  return (g_lang == LANG_CN) ? STR_CN[id] : STR_EN[id];
}
#else
// 单语形态（claw4）：没有语言状态、没有 CN 表 —— 与 v0.5.0~v0.5.2 逐字节等价
static inline const char* TR(StrId id) { return STR_EN[id]; }
#endif
