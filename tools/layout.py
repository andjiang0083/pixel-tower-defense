#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 andjiang0083
"""几何/路径/关卡的唯一真源 (single source of truth)。

v0.2.0：10 关卡系统 + 主题配色 + BOSS 参数化。产出：
  1) src/config_gen.h —— 固件网格/关卡表常量
  2) preview.png / preview-lvNN.png —— PC 端重渲染棋盘，先判构图再上真机
改布局/关卡只改这里。

v0.5.0（Claw4 移植）：屏幕几何改为 **profile 制**。
  原实现把 SCREEN_W/H 写死成 1280x720（Tab5）。Metalio Claw4 是 720x720
  方形屏，同一份生成器要能出两套几何，否则两个仓库的 layout.py 会分叉。
  用法：
      python3 tools/layout.py                    # 默认 profile(tab5) → src/config_gen.h
      python3 tools/layout.py --profile claw4    # → src/config_gen_claw4.h
      python3 tools/layout.py --profile list     # 列出所有 profile
  新增 profile 只改 PROFILES 一处。默认 profile 的输出路径保持 config_gen.h
  不变，CI 的漂移检查（.github/workflows/sim.yml）因此无需改动。
"""

import struct, zlib, os, sys, argparse

# ── PROFILES：屏幕几何的唯一真源 ───────────────────────────────────
# 每项：
#   sw, sh        逻辑分辨率（帧缓冲 = 逻辑坐标，无旋转无偏移）
#   cell          格边长（px）
#   hud_rows      顶部 HUD 占几格
#   build_bar_rows底部建造栏占几格
#   ui            UI 尺寸缩放系数（作用于 BTN_*/SLOT_*/PANEL_* 等硬编码几何）
#   design_range  map_design.py 审计尺 = 最便宜塔射程（px）
#
# ⚠️ Claw4 的 ui 缩放说明：slot/button 几何在 HEADER_TPL 里是写死的
#    200x64 / 200x72 / 360x248。720 宽的屏放不下 5 个 200px 槽（1000px），
#    所以 claw4 的 ui=0.62 → 槽宽 124px × 5 + 间隙 = 满铺 720。
#    同时 render.h 里 HUD 各元素的 X 坐标（16..900）也是按 1280 宽写死的，
#    见 docs/CLAW4-PORT.md 的"待重排"清单。
PROFILES = {
    "tab5": dict(
        sw=1280, sh=720, cell=40,
        hud_rows=2, build_bar_rows=2,
        ui=1.00,
        design_range=150,
        diff_scale=1.00,      # 难度旋钮（见 lvDiff）：tab5 是设计基准，恒 1.0
        # ── 横向 UI 绝对尺寸（px）。为什么是绝对值而不是比例：──
        #   这些容器里装的是**字**，字宽只由 scale（1/2/3...）决定，
        #   不随屏宽连续变化。容器宽度必须 ≥ 最长文案的字宽 + 内边距，
        #   所以它是"内容驱动"的下界，不是"屏宽驱动"的比例。
        #   720 宽的屏因此不能把它们等比缩小——缩了字就溢出容器。
        btn_w=200,          # "NEXT WAVE"@scale2 = 108px；+内边距 20 → 128 起
        panel_w=360,        # 塔面板：标题行(14+108+20+54+14=210) 与属性行(210+60+14=284)
        # 选关节点网格（20 关）
        sel_cols=5, sel_rows=4, sel_node_w=170, sel_node_h=110,
        sel_gap_x=35, sel_gap_y=26, sel_y0=108,
        # 浮层面板（居中；会自动夹到屏宽内，见 OVER_W 的 min）
        over_w=760,         # GAME OVER / LEVEL CLEAR 面板宽
        onboard_w=820,      # 首次引导对话框宽
        # ── profile 能力（v0.5.3）：见 config_gen.h 里的 PTD_HAS_CJK ──
        #   1280 宽放得下 4 汉字（17px/字 = 68px）；tab5 是双语设计基准。
        cjk=1,
        bg_tile=1,            # 背景格子缓存块拷贝（见 config_gen.h 的 PTD_BG_TILE_BLIT）
        desc="M5Stack Tab5 横屏 1280x720（原生目标）",
    ),
    "claw4": dict(
        sw=720, sh=720, cell=40,
        # 720/40 = 18 列 × 18 行。HUD 2 行 + 地图 14 行 + 建造栏 2 行 = 18 行，
        # 与 tab5 的 MAP_ROWS 保持一致（14 行），减少关卡数据迁移量。
        hud_rows=2, build_bar_rows=2,
        ui=0.62,
        # 审计尺同比缩放：地图宽度 32→18 格（-44%），射程像素值不变会让塔
        # 相对变强 1.78 倍、难度崩塌。缩放系数 sqrt(18/32)≈0.75，
        # 具体最终值要靠 map_design.py 审计 + 模拟器自动通关反推。
        design_range=112,
        # ── 关卡难度配平旋钮（见 game.h 的 lvDiff）──────────────────
        # 为什么 claw4 需要它，而 design_range 解决不了：
        #   design_range 缩的是**塔的射程**（150→112px），补偿的是"屏幕窄了，
        #   塔的绝对射程会让塔相对变强"。但它补偿不了另一件事：
        #   **地图从 32 列缩到 18 列，路径长度也等比缩了**（L4 从 ~120 格
        #   缩到 62 格），而敌人的 hp / 数量 / 波数**一个都没动**。
        #   于是同一只怪在 claw4 上从入口走到出口的时间只有 tab5 的一半，
        #   塔能打它的时间也就只有一半 —— 难度凭空翻倍。
        # 这是"地图窄"带来的**第二阶效应**，design_range 管不到。
        # 所以单独给一个旋钮：敌人生血统一乘它（spawnKind 的 lvDiff()）。
        # 值由模拟器反推：20 关全部 bot 可通的最低档。
        diff_scale=0.82,
        # ── 横向 UI 绝对尺寸 ──
        # 按钮：仍按内容定宽（"NEXT WAVE" 108 + 内边距 20 = 128），
        #   取 132 留一点余量。它占 720 的 18.3%（tab5 只占 15.6%），
        #   因为 720 上没有更多空间可以再让了——HUD 左段（12..326）+
        #   BEST(56) + WAVE(96) + 按钮(132) + 边距 = 恰好塞满 720。
        btn_w=132,
        # 塔面板：360 是内容下界（属性行要 284），720 的 41.7% 装得下。
        #   不能按 tab5 的 28.125% 缩到 202 —— 那样属性行必然溢出面板。
        panel_w=300,
        # 选关：tab5 的 5×170+4×35 = 990 在 720 上左右各出屏 135。
        #   改 4 列 × 5 行 + 收窄节点：4×148+3×22 = 658 < 720（左右各留 31）。
        #   纵向 5×100+4×18 = 572，y0=80 → 80..652，底部提示在 666，屏高 720 ✓。
        sel_cols=4, sel_rows=5, sel_node_w=148, sel_node_h=100,
        sel_gap_x=22, sel_gap_y=18, sel_y0=80,
        # 浮层：会被 min(..., SCREEN_W-40) 夹到 680（720 上）。
        over_w=760, onboard_w=820,
        # ── profile 能力（v0.5.3）：claw4 单语 ──
        #   中文 16x16 字形固定 17px/字且**不随 scale 缩**（CN_FONT_H=16），
        #   4 汉字 = 68px 起，装不进 116px 建造栏槽 / 202px 塔面板；720 宽也
        #   没有余量加宽。将来要开：改这一格为 1 + 缩短文案或加宽槽（另起一轮）。
        #   ⚠️ 这条曾是 v0.5.0 在**共享层删中文**的理由 —— 现在降级成这里的一个数字。
        cjk=0,
        bg_tile=0,            # Claw4 暂不开（它的渲染路径不同，要单独量）
        desc="nologotech Metalio Claw4 方形 720x720",
    ),
}

# 当前生效的 profile（由 main() 设置；模块级默认值保证 import 时也能工作）
PROFILE = "tab5"
_P = PROFILES[PROFILE]


def use_profile(name):
    """切换 profile 并重算派生网格常量。必须先于任何 LEVELS/snake() 使用。"""
    global PROFILE, _P, DESIGN_GAT_RANGE, DIFF_SCALE
    if name not in PROFILES:
        raise SystemExit("unknown profile %r (have: %s)"
                         % (name, ", ".join(sorted(PROFILES))))
    PROFILE, _P = name, PROFILES[name]
    apply_profile()                      # ← 关键：重算 COLS/MAP_ROWS/...
    # 审计尺也是 profile 相关的（Gfx 抽象之外的第二处几何依赖）
    DESIGN_GAT_RANGE = _P["design_range"]
    DIFF_SCALE       = _P["diff_scale"]
    SNAKE_XB = COLS - 3
    globals()["SNAKE_XB"] = SNAKE_XB    # snake() 默认折返横坐标随列数变
    return _P


def apply_profile():
    """把 profile 的几何写入模块级网格常量。由 use_profile() 调用。"""
    global SCREEN_W, SCREEN_H, CELL, COLS, ROWS_TOTAL
    global HUD_ROWS, MAP_TOP, BUILD_BAR_ROWS, MAP_ROWS, UI_SCALE
    # v0.5.1：DIFF_SCALE 必须进 global，否则它只是 apply_profile 的局部变量，
    #   模块级那个（模板取值用的）永远不会更新 —— 症状就是"claw4 与 tab5
    #   生成出来的值一模一样"，且不报错。
    global DIFF_SCALE, DESIGN_GAT_RANGE
    global BTN_W, PANEL_W
    global SEL_COLS, SEL_ROWS, SEL_NODE_W, SEL_NODE_H, SEL_GAP_X, SEL_GAP_Y, SEL_Y0
    global OVER_W, ONBOARD_W
    SCREEN_W, SCREEN_H = _P["sw"], _P["sh"]
    CELL        = _P["cell"]
    COLS        = SCREEN_W // CELL
    ROWS_TOTAL  = SCREEN_H // CELL
    HUD_ROWS    = _P["hud_rows"]
    MAP_TOP     = HUD_ROWS * CELL
    BUILD_BAR_ROWS = _P["build_bar_rows"]
    MAP_ROWS    = ROWS_TOTAL - HUD_ROWS - BUILD_BAR_ROWS
    UI_SCALE    = _P["ui"]
    # 内容驱动的横向尺寸（见 PROFILES 注释：字宽不随屏宽连续变化）
    BTN_W       = _P["btn_w"]
    PANEL_W     = _P["panel_w"]
    SEL_COLS, SEL_ROWS = _P["sel_cols"], _P["sel_rows"]
    SEL_NODE_W, SEL_NODE_H = _P["sel_node_w"], _P["sel_node_h"]
    SEL_GAP_X, SEL_GAP_Y, SEL_Y0 = _P["sel_gap_x"], _P["sel_gap_y"], _P["sel_y0"]
    OVER_W, ONBOARD_W = _P["over_w"], _P["onboard_w"]


SCREEN_W, SCREEN_H = 1280, 720
CELL        = 40
COLS        = SCREEN_W // CELL   # 32
ROWS_TOTAL  = SCREEN_H // CELL   # 18
HUD_ROWS    = 2
MAP_TOP     = HUD_ROWS * CELL   # 80
BUILD_BAR_ROWS = 2
MAP_ROWS    = ROWS_TOTAL - HUD_ROWS - BUILD_BAR_ROWS   # 14
UI_SCALE    = 1.00              # UI 几何缩放（见 PROFILES[*].ui）
BTN_W       = 200               # 内容驱动（见 PROFILES[*].btn_w）
PANEL_W     = 360               # 内容驱动（见 PROFILES[*].panel_w）
SEL_COLS, SEL_ROWS, SEL_NODE_W, SEL_NODE_H = 5, 4, 170, 110
SEL_GAP_X, SEL_GAP_Y, SEL_Y0 = 35, 26, 108
OVER_W, ONBOARD_W = 760, 820
apply_profile()

# ── 经济（v0.4：小学生实测"发育过好"→全面收紧+波次加厚）──────────
START_GOLD, START_LIVES = 210, 20        # v0.4：开局少一座塔，前期就要做选择
MAX_ENEMIES, MAX_TOWERS, MAX_BULLETS, MAX_FX = 48, 20, 56, 24   # v0.4：一波太稀→硬顶翻倍
WAVE_BONUS_BASE, WAVE_BONUS_STEP = 18, 4 # v0.4：25+6→18+4，波补收紧
HP_GROWTH = 1.10   # v0.2.1：基数×2.1 后成长率同步收敛（1.16→1.10，否则后期总血量爆表）
# v0.2.1：真机反馈"一颗子弹就死"——血量基数 40→85，前几波也要挨 3~7 发
HP_BASE = 85
# v0.2.1：血量×2.1 后赏金同步 +30%（否则塔升级永远追不上血线，L4+ 必死局）
KILL_GOLD_PCT = 150   # v0.4：保持 150。实测教训：波量+双王机制下再砍赏金
                      # = 反向金币螺旋（L8 bot w6-7 12塔饿死，gold=51 连 GAT 都
                      # 买不起）。压力改由"波量加厚+技能+多王"提供，经济只维持
                      # 紧平衡——这正对小学生反馈：难在"使坏"，不难在"抠门"。
# v0.2.1：NEXT WAVE 变倒计时——波间 10s 自动开下一波（首波仍手动，留布塔时间）
AUTO_NEXT_MS = 7000     # v0.3.2：10s→7s，波间喘息缩短
# 压叠窗口：本波刷怪完毕、且本波已**基本清完**→ 5s 后强开下波。
#   v0.3.2 的判据是"场上总敌数 ≥ 8"，不区分这 8 只是哪一波的 —— L10 实测
#   （弱防真人 playtest）：第 2 波 9 只刚刷完（一只没死，正好 ≥8）就满足条件，
#   5s 后强开第 3 波；此后每一波都踩着前几波的残留再次满足 → 波波叠到
#   MAX_ENEMIES(48) 硬顶，fillRect/帧 424→4192（≈10×），真机帧预算崩掉。
#   v0.5.2 起只看**本波出生的幸存者**，且必须落到"清理收尾"的量级。
OVERLAP_MS   = 5000     # 本波清完后 5s 强开下波
OVERLAP_LEFTOVER_PCT = 50   # 本波幸存者 ≤ 本波规模的一半才算清完（v0.5.2）
OVERLAP_LEFTOVER_MIN = 2    # 兜底：小波（9 只）也要留 2 只的余量，别卡死收尾
# ⛔ 已删除（v0.5.3）：曾经的"场上总敌数红线 OVERLAP_FIELD_MAX=16"。
#   它是拿 PC 仿真的 fillRect 次数当负载代理量拍出来的**出兵上限**，等于用
#   "少放敌人"绕开性能问题。真机负载态实测（Tab5，48 只同屏 + 20 座 3 级塔）：
#   修掉擦格碎块税后 draw=18~30ms/33ms、30fps 稳 ⇒ 不需要限制出兵。
#   详见 docs/FIXLOG-v0.5.3.md（含"每格 25 次 fillRect → 1 次 pushImage"的账）。

TOWER_TYPES = [
    # behavior: 0=单体弹 1=溅射炮弹(仅地面) 2=冰霜光环 3=能量产出(不攻击) 4=激光瞬击(穿地面+空中)
    # damage 列语义随 behavior：0/1=伤害，3=每脉冲产金 $
    ("GAT",   "M.GUN",   60, 150, 420,  12, 560,  0, 0),
    ("CAN",   "CANNON", 100, 170, 1500, 34, 320, 64, 1),
    ("FRO",   "FROST",   80, 130, 1200,  0,   0,  0, 2),
    ("EMIT",  "ENERGY",  80,   0, 4000, 12,   0,  0, 3),   # 每4s产$12（投资换后期）
    ("LASER", "LASER",  140, 260,  900, 45,   0,  0, 4),   # 超远瞬击，对空可用
]
TOWER_MAX_LEVEL = 3
# v0.3.2：LV3 费用上调——"三座满级机枪平推"的 $600 门槛抬到 ~$830
UPGRADE_COST = {"GAT": (50, 120), "CAN": (90, 170), "FRO": (70, 130),
                "EMIT": (60, 110), "LASER": (120, 210)}
SELL_REFUND_PCT = 70
FROST_SLOW_PCT, FROST_SLOW_MS = 45, 1500

# ── 敌人种类（v0.3.0：+CHOPPER 空中单位）─────────────────────────
# (name, hp_scale, speed_pps, size_px, gold, leak_dmg, armor_flat, flying, shield)
# v0.3.2：+shield 列——1=弹盾兵，子弹类(机枪/炮弹直击)伤害×0.5；
#         激光全伤、溅射无视盾（与对空矩阵同思路：逼混编，废纯机枪阵）
KINDS = [
    ("GRUNT",   1.0, 70,  24, 8,   1, 0, 0, 0),
    ("RUNNER",  0.6, 132, 20, 10,  1, 0, 0, 0),
    ("TANK",    3.4, 46,  34, 25,  3, 0, 0, 0),
    ("SWARM",   0.35,120, 16, 3,   1, 0, 0, 0),
    ("ARMOR",   1.8, 52,  26, 14,  3, 6, 0, 0),
    ("BOSS",    9.0, 34,  48, 90,  10, 2, 0, 0),
    ("HEALER",  0.5, 80,  20, 12,  1, 0, 0, 0),
    ("CHOPPER", 1.3, 95, 26, 14,   2, 0, 1, 0),   # v0.3.0：直线飞行，加农/冰霜拿它没办法
    ("SHIELD",  1.1, 48, 30, 20,   3, 0, 0, 1),   # v0.3.2：弹盾兵——子弹伤害×0.5，激光/溅射全伤
]
K_SWARM, K_ARMOR, K_BOSS, K_HEAL = 3, 4, 5, 6
K_CHOPPER = 7                                        # v0.3.0：空中单位
K_SHIELD  = 8                                        # v0.3.2：弹盾兵
SWARM_BURST, ARMOR_FROM_WAVE = 5, 4
AIR_FROM_WAVE = 3                                  # v0.3.0：空中单位第3波起登场（先给防空准备期）
SHIELD_FROM_WAVE = 5                                 # v0.4：6→5——压力提前一拍

# ── v0.2.0 新机制数值 ─────────────────────────────────────────────
HEAL_PCT, HEAL_MS            = 12, 1600    # HEALER：r=90 内友军 +maxHp%
BOSS_HEAL_PCT, BOSS_HEAL_MS  = 6, 2000    # BOSS 自愈：v0.3.2 8→6——
                                          # 8% 在 diff≥1.6 的终波=3 王互 heal 393 DPS 死锁（bot 全灭）
# v0.4.1：BOSS_HEAL_R（420，光环半径）已删——治疗改自愈后它是死宏，
#          曾误导"光环还在"的错觉。历史：r=420 光环奶全场 → L6 互续航打不完。
BOSS_BROOD_MS, BOSS_BROOD_SIZE = 4000, 3   # BOSS 产卵：吐 SWARM
ELITE_HP_MULT, ELITE_GOLD_ADD = 1.75, 8    # ELITE 金皮旗标（×2.5→×1.75：仿真 L7 wave8 火力墙死局）

# ── v0.4 BOSS 技能位掩码（"会耍坏的王"，小学生点名需求）──────────
AB_HEAL,  AB_BROOD = 1, 2          # 旧：自愈光环 / 产卵
AB_SPLIT, AB_HORN  = 4, 8          # 💀死亡分裂：打爆爆出小兵 / 📢集结号角：全场加速
AB_EMP,   AB_GREED = 16, 32       # 🔇塔静默：附近2座塔哑火 / 💰抢钱：受击偷金
AB_MOLT,  AB_GHOST = 64, 128       # 🛡蜕壳：半血狂暴 / 🌫隐身：周期性不可锁定
AB_SIEGE             = 256         # 🚧 v0.4.2 封锁建造区：王周围一圈禁建塔数秒
SPLIT_SIZE          = 3             # 💀 分裂体数量（+wave/8 渐增）
HORN_MS, HORN_BOOST_PCT, HORN_DUR_MS = 8000, 45, 3000   # 📢 +45%速 持续3s
EMP_MS, EMP_TOWERS, EMP_DUR_MS = 9000, 2, 4000           # 🔇 哑火最近2塔 4s
GREED_HITS, GREED_AMT, GREED_CAP_PCT = 8, 15, 20         # 💰 每受击8次偷$15，封顶金20%
MOLT_PCT, MOLT_SPD_PCT, MOLT_SIZE_DROP = 50, 40, 8        # 🛡 HP<50%蜕壳：+40%速 -8px 甲归零
GHOST_MS, GHOST_DUR_MS = 7000, 1500                        # 🌫 每7s隐身1.5s（塔丢目标）
# 🚧 v0.4.2 SIEGE：r=180px（约4.5格）封锁——王贴脸前最后一拍干扰，
#    逼玩家提前布阵而不是临场补塔。半径再大就覆盖半张图（烦扰 > 挑战）。
# ⚠️ 持续时间与节拍必须分开：曾把两者都设 4000 → 王每 4s 重放、封锁
#    几乎永久（仿真实测 siegeMs 归零前 1 帧就被下一拍重置）。
SIEGE_MS, SIEGE_R, SIEGE_IV = 3500, 180, 10000
# mid-boss：一关多王——半程波出缩小版王（hp×0.45 size-8 gold×0.6）
MID_HP_PCT, MID_GOLD_PCT, MID_SIZE_DROP = 45, 60, 8
# ── v0.4 无尽模式（L10 CLEAR → TERMINAL 图无限刷）─────────────────
ENDLESS_WAVES      = 250          # 无尽总波数上限（v0.4.1 立为常量：game.h lvWaves 曾硬编码 250）
ENDLESS_DIFF_PCT   = 6             # 每轮(10波) diff ×(1+6%)
ENDLESS_ROUND_WAVES = 10           # 一轮=10波；每轮末波出真BOSS
ENDLESS_MINI_EVERY = 5             # 每5波一个mini-boss

# ── v0.3.0：能量塔 / 激光塔 / 空中单位 / 星级 ─────────────────────
# 对空规则（参考 Tower Defense Template 的克制矩阵，取其"强制混编"内核）：
#   GAT ✔对空  CAN ✘(火箭只打地)  FRO ✘(冰雾够不着天)  EMIT —  LASER ✔对空
TOWER_AIR_OK = [1, 0, 0, 0, 1]
LASER_BEAM_MS = 90                          # 光束残留显示时长（纯视觉）
ENERGY_GOLD_SCALE = 1.0                     # 能量塔每级产金 ×1.5（走 TOWER_DMG_SCALE，复用）
# 星级：通关按剩余生命比例评星（选关界面收集目标）
STAR3_PCT, STAR2_PCT = 90, 55             # ≥90%→★★★  ≥55%→★★  否则★

# ── v0.4 地图设计器：可建区雕刻 + 覆盖预算审计 ─────────────────────
# 设计哲学（讨论定稿，见 SPEC-v0.4.md）：
#   · 路是绳子，塔是环。难度 = 削减"好位置"，不是堆数值。
#   · 关卡是【预算题】不是【拼图】：必须多条活路、失败可渐进挽救（防"围棋化"）。
#   · 长路径不该白送时间：可建区压成"贴路窄带 + 行间位"，长度变周长负担。
# 关键杠杆：行间位（蛇形两排平行路之间那一行）一格同时罩两排路，
#           leverage 是贴路边位的两倍——绞肉机图的好位置天生在"路缝里"。
# v0.5.0：审计尺改为 profile 提供（见 PROFILES[*].design_range）。
#   tab5 = 150（= GAT LV1 射程，最便宜塔 = 可达性地板）
#   claw4 = 112（地图窄 44% 后同比缩放，防止"塔相对变强"导致难度崩塌）
DIFF_SCALE       = _P["diff_scale"]
DESIGN_GAT_RANGE = _P["design_range"]
LEGACY_LEVELS    = 10         # L1-L10 沿用旧图（无禁建）；L11-L20 走雕刻

# v0.5.0：蛇形折返的默认横坐标。旧版硬编码 xa=2/xb=29（= 32 列的两端内缩），
#   在 claw4 的 18 列上会直接越界触发 path_cells 的 assert。改为按 COLS 推导：
#   距左端 2 格、距右端 2 格 → 任何列数都成立。
SNAKE_XA = 2
SNAKE_XB = COLS - 3          # 32列→29（与旧版一致）；18列→15

def snake(rows, xa=None, xb=None, tight=False):
    """蛇形路线生成器：rows = 折返行（升序、间隔≥2 才留得下行间位给塔）。
    首点右缘出生、末点左缘出口（path_cells 的硬约束）。

    v0.5.0：xa/xb 默认值从硬编码改为 None→SNAKE_XA/SNAKE_XB，使同一份
    关卡定义在任何 profile 下都能生成合法路径（0 < col < COLS）。
    tight=True 表示"贴边折返"（距两端各 1 格，L17/L19/L20 用），
    替代旧版硬编码的 xa=1, xb=30 —— 后者在 18 列的 claw4 上必然越界。"""
    if tight:
        xa = 1 if xa is None else xa
        xb = COLS - 2 if xb is None else xb
    else:
        if xa is None: xa = SNAKE_XA
        if xb is None: xb = SNAKE_XB
    assert 0 < xa < COLS, "snake xa=%d out of range (COLS=%d)" % (xa, COLS)
    assert 0 < xb < COLS, "snake xb=%d out of range (COLS=%d)" % (xb, COLS)
    wps = [(COLS - 1, rows[0])]
    go_left = True
    for i, r in enumerate(rows):
        far = 0 if i == len(rows) - 1 else (xa if go_left else xb)
        wps.append((far, r))
        if i < len(rows) - 1: wps.append((far, rows[i + 1]))
        go_left = not go_left
    return wps

def band_keep(routes, D):
    """可建白名单 = 距路 Chebyshev ≤D 的格（贴路窄带）。D 越小好位越稀缺。"""
    pathc = path_cells(routes)
    keep = set()
    for (c, r) in pathc:
        for dc in range(-D, D + 1):
            for dr in range(-D, D + 1):
                cc, rr = c + dc, r + dr
                if 0 <= cc < COLS and 0 <= rr < MAP_ROWS and (cc, rr) not in pathc:
                    keep.add((cc, rr))
    return keep

def blocked_cells(lv):
    """该关禁建格 = 全网格 - 路 - 白名单。旧关（无 band/keep）→ 全开放=空集。"""
    if "band" not in lv and not lv.get("keep"): return set()
    pathc = path_cells(lv["routes"])
    keep = band_keep(lv["routes"], lv["band"]) if "band" in lv else set()
    for (c, r, w, h) in lv.get("keep", []):
        for cc in range(c, c + w):
            for rr in range(r, r + h):
                if 0 <= cc < COLS and 0 <= rr < MAP_ROWS and (cc, rr) not in pathc:
                    keep.add((cc, rr))
    return {(c, r) for c in range(COLS) for r in range(MAP_ROWS)} - pathc - keep

def audit_level(lv):
    """覆盖预算审计（纯位运算）：最少几座 GAT LV1 才罩住全路 + 死角数。
    ⚠️ 贪心的起点必须是【全部路格】——若用"被覆盖格的并集"当起点，
       打不到的格子会被静默忽略，"不可玩地图"伪装成 PASS（踩过的坑）。"""
    pathc = path_cells(lv["routes"])
    blocked = blocked_cells(lv)
    idx = {p: i for i, p in enumerate(sorted(pathc))}
    R2 = DESIGN_GAT_RANGE * DESIGN_GAT_RANGE
    masks = {}
    for c in range(COLS):
        for r in range(MAP_ROWS):
            if (c, r) in pathc or (c, r) in blocked: continue
            m = 0
            for p, i in idx.items():
                dx = (c - p[0]) * CELL; dy = (r - p[1]) * CELL
                if dx * dx + dy * dy <= R2: m |= 1 << i
            if m: masks[(c, r)] = m
    todo, picks = (1 << len(idx)) - 1, 0
    while todo:
        best, bn = 0, 0
        for m in masks.values():
            n = bin(m & todo).count("1")
            if n > bn: best, bn = m, n
        if bn == 0: break
        todo &= ~best; picks += 1
    return dict(pathc=pathc, blocked=blocked, buildable=set(masks),
                towers=picks, gap=bin(todo).count("1"), masks=masks, idx=idx)

# ── 关卡表（v0.4：+mid-boss 一关多王 + BOSS 全技能分配）──────────
# routes: 每条 [(col,row)...]；首点必须右缘(col==31)，末点必须左缘(0)
# kinds : 常规兵种池（<=6）；SWARM 按 burst 规则注入，BOSS=末波压轴
# boss  : (name, hp_mult, speed_pps, size_px, armor, gold, leak_dmg, ability)
# mid   : [wave,...] 半程波出 mini-boss（hp×MID_HP_PCT%，至多2次，绝不与末波重合）
# diff  : 关卡间血量系数（关内再叠 HP_GROWTH^波）——"越来越难 :p"
LEVELS = [
 dict(id=1, name="FIRST BLOOD", theme=0, waves=5, diff=1.00, elite_from=0,
      boss=("BIG GRUNT", 6.0, 24, 52, 0, 100, 8, 0),        # 教学关：王不带技能
      routes=[[(31,7),(0,7)]],
      kinds=[0]),
 dict(id=2, name="SERPENTINE", theme=1, waves=7, diff=1.05, elite_from=0,
      boss=("BLITZ", 4.5, 68, 40, 0, 110, 8, AB_HORN),       # 📢 首现号角：兵潮加速
      routes=[[(31,2),(6,2),(6,7),(25,7),(25,11),(0,11)]],
      kinds=[0,1]),
 dict(id=3, name="BUG VALLEY", theme=2, waves=8, diff=1.12, elite_from=0,
      boss=("BROOD MOTHER", 7.0, 30, 52, 2, 140, 8, AB_BROOD | AB_SPLIT),  # 💀打爆爆虫
      routes=[[(31,1),(2,1),(2,4),(29,4),(29,7),(3,7),(3,10),(28,10),
               (28,12),(0,12)]],
      kinds=[0,1,3]),
 dict(id=4, name="IRON CORRIDOR", theme=3, waves=9, diff=1.20, elite_from=0,
      boss=("FORTRESS", 9.0, 26, 56, 12, 170, 10, AB_MOLT),  # 🛡厚甲蜕壳狂暴
      mid=[5],                                                  # MINI-FORTRESS 半程偷家
      routes=[[(31,2),(4,2),(4,6),(27,6),(27,10),(8,10),
               (8,4),(12,4),(12,8),(0,8)]],
      kinds=[0,4,7]),   # v0.3.0：CHOPPER 首现——加农阵地必须配机枪/激光防空
 dict(id=5, name="CROSSROADS", theme=0, waves=9, diff=1.20, elite_from=0,
      boss=("TWIN", 5.0, 40, 48, 2, 180, 8, AB_GREED | AB_HORN), boss_count=2,  # 💰双王轮流偷钱
      mid=[5],
      routes=[[(31,2),(4,2),(4,7),(0,7)],
              [(31,12),(27,12),(27,7),(0,7)]],
      kinds=[0,1,2,8]),   # v0.3.2：+SHIELD 弹盾兵
 dict(id=6, name="FROZEN PASS", theme=4, waves=10, diff=1.45, elite_from=0,
      boss=("FROST LORD", 8.0, 30, 56, 2, 200, 10, AB_HEAL | AB_GHOST),   # 🌫奶王隐身
      mid=[5],
      routes=[[(31,1),(6,1),(6,4),(28,4),(28,7),(5,7),
               (5,10),(26,10),(26,13),(0,13)]],
      kinds=[0,1,6,7]),  # v0.3.0：奶妈保地 + 直升机偷家
 dict(id=7, name="SPIRAL ABYSS", theme=5, waves=10, diff=1.60, elite_from=8,
      boss=("COLOSSUS", 12.0, 24, 64, 4, 230, 10, AB_EMP | AB_MOLT),      # 🔇哑火塔+蜕壳
      routes=[[(31,1),(2,1),(2,12),(29,12),(29,4),(6,4),
               (6,9),(25,9),(25,6),(10,6),(10,8),(0,8)]],
      kinds=[0,2,8]),    # v0.3.2：+SHIELD
 dict(id=8, name="FORKED CANYON", theme=2, waves=10, diff=1.65, elite_from=7,
      boss=("TWIN PRO", 6.0, 45, 48, 2, 250, 8, AB_BROOD | AB_SPLIT | AB_HORN), boss_count=2,
      mid=[5],
      routes=[[(31,2),(14,2),(14,7),(0,7)],
              [(31,12),(18,12),(18,7),(0,7)]],
      kinds=[0,1,4,7]),  # v0.3.0：双路+空中，三面来敌
 dict(id=9, name="MEAT GRINDER", theme=3, waves=12, diff=1.75, elite_from=6,
      boss=("TWIN ELITES", 7.0, 50, 48, 2, 280, 10, AB_EMP | AB_GREED | AB_MOLT), boss_count=2,
      mid=[6],
      routes=[[(31,1),(2,1),(2,3),(29,3),(29,5),(2,5),
               (2,7),(29,7),(29,9),(2,9),(2,11),(28,11),(28,13),(0,13)]],
      kinds=[0,1,2,8,7]),  # v0.3.2：SHIELD 顶替 RUNNER——盾兵带节奏
 dict(id=10, name="TERMINAL", theme=5, waves=15, diff=1.95, elite_from=4,
      boss=("THE END", 16.0, 22, 72, 8, 350, 12, AB_HEAL | AB_BROOD | AB_SPLIT | AB_EMP),
      mid=[8, 12],        # 双 mini-END 夹击，真王全家桶压轴
      routes=[[(31,1),(1,1),(1,13),(29,13),(29,3),(4,3),
               (4,11),(27,11),(27,5),(6,5),(6,9),(24,9),(24,7),(0,7)]],
      kinds=[0,2,8,4,6,7]),  # v0.3.2：GRUNT→SHIELD（THE END 带盾亲卫）
 # ═══ v0.4 追加：L11-L20 按"覆盖预算"新哲学设计（禁建石区 + 贴路窄带）═══
 # band D = 可建带宽度（Chebyshev 距路）：D 小 → 好位稀缺 → 要靠行间位和取舍。
 # 难度不是数值档位，是"最少要几座塔才罩得住全路"逼近 MAX_TOWERS=20。
 # 老地图的病灶：可建格两百多个、塔上限 20 → 空间免费、路越长越是福利。
 # 新地图把这笔账扳回来：D=3 起步（还有余地）→ D=1（只剩路缝）。
 # ⚠️ 排序规矩：**双路图是难度乘数**（两个独立前线，单点集群防守失效），
 #    必须排在单路图之后——L13 首版放双路，bot 波7 就被打穿（43 只过线）。
 dict(id=11, name="GLASS FIELDS", theme=0, waves=15, diff=2.00, elite_from=4,
      boss=("GLASS TITAN", 9.0, 28, 60, 4, 260, 10, AB_SPLIT | AB_HORN | AB_SIEGE),  # 🚧 v0.4.2 首现封锁
      mid=[7], band=3,
      routes=[snake([2,4,6,8,10,12])],
      kinds=[0,1,2,8]),
 dict(id=12, name="HAIRPIN RIDGE", theme=3, waves=15, diff=2.05, elite_from=4,
      boss=("RIDGE WARDEN", 10.0, 26, 60, 6, 270, 10, AB_EMP | AB_MOLT),
      mid=[6], band=2,
      routes=[snake([1,3,5,7,9,11,13])],
      kinds=[0,2,4,8]),
 dict(id=13, name="SAND COIL", theme=2, waves=16, diff=2.10, elite_from=5,
      boss=("SAND COIL", 11.0, 26, 64, 4, 320, 11, AB_GHOST | AB_BROOD),
      mid=[7], band=2,
      routes=[[(31,1),(1,1),(1,13),(29,13),(29,4),(5,4),(5,10),(25,10),(25,7),(0,7)]],
      kinds=[0,2,6,8]),
 dict(id=14, name="CROSSING FIRE", theme=2, waves=16, diff=2.15, elite_from=5,
      boss=("CROSSFIRE", 7.0, 42, 52, 2, 300, 9, AB_GREED | AB_HORN | AB_SPLIT),
      boss_count=2, mid=[6, 11], band=2,
      routes=[snake([2,4,6]), snake([8,10,12])],      # 双路各三段折返：两条独立前线
      kinds=[0,1,2,7,8]),
 dict(id=15, name="COMB LINE", theme=1, waves=16, diff=2.20, elite_from=5,
      boss=("COMB LORD", 11.5, 28, 64, 4, 330, 11, AB_HEAL | AB_EMP | AB_MOLT),
      mid=[6, 11], band=1,                            # D=1 起：好位只在路缝里
      routes=[snake([1,3,5,7,9,11,13])],
      kinds=[0,1,2,7,8]),
 dict(id=16, name="TWIN RAVINE", theme=4, waves=17, diff=2.25, elite_from=6,
      boss=("RAVINE TWIN", 8.0, 44, 56, 2, 350, 10, AB_BROOD | AB_SPLIT | AB_GHOST),
      boss_count=2, mid=[7, 12], band=1,
      routes=[snake([1,3,5]), snake([9,11,13])],      # 上下两条峡谷，各自要人守
      kinds=[0,2,4,7,8]),
 dict(id=17, name="LONG SERPENT", theme=3, waves=17, diff=2.30, elite_from=6,
      boss=("SERPENT KING", 13.0, 24, 68, 6, 370, 12, AB_EMP | AB_HORN | AB_GREED),
      mid=[6, 12], band=1,
      routes=[snake([1,3,5,7,9,11,13], tight=True)],  # 满屏七折，路最长、带最窄
      kinds=[0,1,2,8,7]),
 dict(id=18, name="STONE WEAVE", theme=0, waves=17, diff=2.35, elite_from=6,
      boss=("WEAVER", 12.0, 30, 64, 4, 380, 11, AB_MOLT | AB_GHOST | AB_SPLIT),
      mid=[8], band=1,
      routes=[snake([1,5,9,13])],                     # 折返间隔 4 行：路间大片岩区
      kinds=[0,2,6,8,7]),
 dict(id=19, name="GRINDER TWO", theme=2, waves=18, diff=2.40, elite_from=7,
      boss=("GRINDER PRIME", 14.0, 26, 72, 6, 400, 12, AB_EMP | AB_BROOD | AB_SPLIT | AB_GREED),
      boss_count=2, mid=[6, 12], band=1,
      routes=[snake([1,3,5,7,9,11,13], tight=True)],
      kinds=[0,1,2,8,7]),
 dict(id=20, name="THE LAST WALL", theme=5, waves=18, diff=2.45, elite_from=7,
      boss=("THE WALL", 18.0, 22, 76, 10, 450, 14,
            AB_HEAL | AB_BROOD | AB_SPLIT | AB_EMP | AB_GHOST),
      boss_count=2, mid=[6, 15], band=1,
      routes=[snake([2,4,6], tight=True), snake([8,10,12], tight=True)],
      kinds=[0,2,8,4,6,7]),                           # 终盘：盾+甲+直升机+奶妈全家桶
]

# ═══ v0.5.0 profile 覆盖层：按屏幕几何替换关卡的 routes ═════════════
# LEVELS 里的 routes 是 **tab5 基准（32 列）** 的定义。目标 profile 列数不同时，
# 这里按 PROFILE 应用对应覆盖。设计上刻意做成"表驱动"而不是把多套坐标塞进
# LEVELS —— 后者会让关卡表变成没人敢动的意大利面。
#
# ⚠️ 关键结构说明（v0.5.0 的一个真实踩坑）：
#   snake() 在 LEVELS 定义处被**立即求值**，它捕获的是当时模块级的 COLS。
#   若主流程先跑完 import（COLS=32）、再调 use_profile('claw4')（COLS=18），
#   snake() 的结果早已固化成 32 列坐标 —— use_profile 改 COLS 已经太晚。
#   症状：apply_profile_routes() 明明报告"11 关已覆盖"，validate() 仍然报
#   `spawn at right edge: (31, 2)`。
#   修法：生成器关卡（snake）在 LEVELS 里登记为 snake_spec（延迟），由
#   rebuild_generated_routes() 在 use_profile() 之后统一求值。L1-L10/L13
#   那种字面量手写坐标则由 PROFILE_ROUTES 覆盖层负责。
#
# 覆盖来源，三种：
#   "remap"   → 用 remap_cols() 等比映射。仅适用于拓扑足够简单的关卡
#               （路径是一条横线、折返数 ≤2）。等比压缩会塌掉折返间距，
#               复杂折线必须人工重画，不要偷懒用 remap。
#   "routes"  → 直接给目标列数下的绝对坐标（人工重画的结果）
#   snake_spec → 折返行列表 + 紧贴标志，由 snake() 在当前 COLS 下现算
#
# ⚠️ 未覆盖的关卡会在 validate() 阶段被 path_cells 的 assert 拦下并指名道姓，
#    不会静默产出坏地图——这是原作者"单一真源 + 机器可判"纪律的延续。
PROFILE_ROUTES = {
    "claw4": {
        # ── L1 FIRST BLOOD：教学关，2 折回折（40 步 → 41 格）──────────
        # 为什么不再用 remap：
        #   tab5 的 L1 是"一条横线"（32 格），remap 到 18 列只剩 18 格。
        #   18 格 = 敌人从右边进、直穿到左边出，**一次转向都没有** ——
        #   玩家没有"拐角位"可守（拐角是塔防里价值最高的塔位：能同时
        #   覆盖两条边），第一关就把最核心的玩法教没了。
        #   而且 18 格连"路径长度 ≥30"这条门禁都过不了。
        # 改成 2 折回折：右上进 → 走到左上 → 下折到中段右行 → 再下折
        #   → 左行出界。41 格，比 L2（43）略短，符合"第一关最简单"。
        # 拓扑意图（对照 tab5）：tab5 的 L1 是直横线，claw4 这里主动
        #   加了两折 —— 因为 18 列的屏宽只有 56%，直横线的绝对长度
        #   不足以撑起一关的时长与塔位密度。这是**按屏幕比例重设计**，
        #   不是等比压缩。
        1: dict(routes=[[(17,2),(1,2),(1,7),(8,7),(8,11),(0,11)]]),

        # ── L2-L13 人工重画（18 列）─────────────────────────────────
        # 设计约束（与 tab5 版本的差异，逐条说明）：
        #   · 横向自由度 32→18 列（56%）。折返的横坐标间距必须 ≥3 格才留得下
        #     "行间位"（两排平行路之间的塔位），否则好位置消失、难度变成堆数值。
        #     tab5 的 31→27（4 格）压成 17→14（3 格）。
        #   · 纵向 MAP_ROWS 不变（14 行），所以行数与折返层数全部保留。
        #   · 每关的**拓扑意图**（几折、双路/单路、绞肉机/走廊/十字）严格保留，
        #     只把横坐标按 18 列重排。难度靠 design_range=112 缩回来配平。
        # L2 SERPENTINE：3 折蛇形（tab5: 31→6→25，1 层）
        2: dict(routes=[[(17,2),(5,2),(5,7),(13,7),(13,11),(0,11)]]),

        # L3 BUG VALLEY：5 折长蛇（tab5: 31→2→29→3→28），6 层
        3: dict(routes=[[(17,1),(2,1),(2,4),(15,4),(15,7),(3,7),
                          (3,10),(14,10),(14,12),(0,12)]]),

        # L4 IRON CORRIDOR：内卷走廊（tab5: 31→4→27→8→12→0），5 层
        4: dict(routes=[[(17,2),(4,2),(4,6),(14,6),(14,10),(8,10),
                         (8,4),(11,4),(11,8),(0,8)]]),

        # L5 CROSSROADS：双路汇流到左中（tab5: 31→4→0 / 31→27→0）
        5: dict(routes=[[(17,2),(4,2),(4,7),(0,7)],
                         [(17,12),(14,12),(14,7),(0,7)]]),

        # L6 FROZEN PASS：错位双层通道（tab5: 31→6→28→5→26→0）
        6: dict(routes=[[(17,1),(5,1),(5,4),(15,4),(15,7),(4,7),
                          (4,10),(13,10),(13,13),(0,13)]]),

        # L7 SPIRAL ABYSS：内螺旋（tab5: 31→2→29→6→25→10→0），6 层
        7: dict(routes=[[(17,1),(2,1),(2,12),(15,12),(15,4),(6,4),
                          (6,9),(13,9),(13,6),(9,6),(9,8),(0,8)]]),

        # L8 FORKED CANYON：双路（tab5: 31→14→0 / 31→18→0），上下分进
        8: dict(routes=[[(17,2),(8,2),(8,7),(0,7)],
                         [(17,12),(10,12),(10,7),(0,7)]]),

        # L9 MEAT GRINDER：8 折绞肉机（tab5 8 层），逐层左右互咬
        9: dict(routes=[[(17,1),(2,1),(2,3),(15,3),(15,5),(2,5),
                          (2,7),(15,7),(15,9),(2,9),(2,11),(14,11),
                          (14,13),(0,13)]]),

        # L10 TERMINAL：8 折终局迷宫
        10: dict(routes=[[(17,1),(1,1),(1,13),(15,13),(15,3),(4,3),
                           (4,11),(13,11),(13,5),(6,5),(6,9),(12,9),
                           (12,7),(0,7)]]),

        # L13 SAND COIL：手写内卷（tab5: 31→1→29→5→25→0）
        13: dict(routes=[[(17,1),(1,1),(1,13),(15,13),(15,4),(5,4),
                           (5,10),(13,10),(13,7),(0,7)]]),
    },
}

# ═══ v0.5.0 延迟求值的生成器关卡 ═══════════════════════════════════
# L11/L12/L14-L20 用 snake() 生成。它们在 tab5 下是立即求值的，坐标已固化为
# 32 列。跨 profile 时不能靠 PROFILE_ROUTES 逐关覆盖（九张图覆盖表 = 冗余
# 且易失同步），而是登记成延迟规格，由 rebuild_generated_routes() 在
# use_profile() 之后按当前 COLS 现算。
#
# 每项：level_id -> list of (折返行列表, tight 是否贴边)
#   · 单路关 = [(rows, tight)]
#   · 双路关 = [(上半 rows, tight), (下半 rows, tight)]  ← 两条都显式列出
#     ⚠️ 双路不要用"上半 + 自动下移"推导：L14 的下半是 snake([8,10,12])，
#        与上半 [2,4,6] 的间隔关系并不是简单的行平移（推导出的 [9,11,13]
#        会让 config_gen.h 产生漂移）。显式列出唯一真源。
#   · rows 取自 tab5 版本原值；纵向 MAP_ROWS 两个 profile 都是 14，层数不变。
SNAKE_SPECS = {
    11: [([2, 4, 6, 8, 10, 12], False)],
    12: [([1, 3, 5, 7, 9, 11, 13], False)],
    14: [([2, 4, 6], False), ([8, 10, 12], False)],      # CROSSING FIRE 双路
    15: [([1, 3, 5, 7, 9, 11, 13], False)],
    16: [([1, 3, 5], False), ([9, 11, 13], False)],      # TWIN RAVINE 双路
    17: [([1, 3, 5, 7, 9, 11, 13], True)],
    18: [([1, 5, 9, 13], False)],
    19: [([1, 3, 5, 7, 9, 11, 13], True)],
    20: [([2, 4, 6], True), ([8, 10, 12], True)],        # THE LAST WALL 双路
}


def rebuild_generated_routes():
    """在当前 PROFILE 的 COLS 下重算所有 snake() 关卡。必须在 use_profile() 后。"""
    n = 0
    for lv in LEVELS:
        spec = SNAKE_SPECS.get(lv["id"])
        if not spec:
            continue
        lv["routes"] = [snake(rows, tight=tight) for (rows, tight) in spec]
        n += 1
    return n


def apply_profile_routes():
    """按当前 PROFILE 重写 LEVELS[*]["routes"]。必须在 use_profile() 后调用。"""
    n = rebuild_generated_routes()      # 生成器关卡先在当前 COLS 下重算
    over = PROFILE_ROUTES.get(PROFILE, {})
    for lv in LEVELS:
        spec = over.get(lv["id"])
        if not spec:
            continue
        if spec.get("mode") == "remap":
            lv["routes"] = [remap_cols(w) for w in lv["routes"]]
        elif "routes" in spec:
            lv["routes"] = spec["routes"]          # 人工重画结果直接生效
        else:
            raise SystemExit("L%d: bad route override %r" % (lv["id"], spec))
        n += 1
    return n


# ── 主题配色 (grassA, grassB, pathA, pathB) ──────────────────────
THEMES = {
    0: ((24,58,30),(20,48,26),(96,74,38),(82,63,32)),      # grass
    1: ((24,58,30),(20,48,26),(104,78,40),(90,66,34)),      # grass+
    2: ((18,50,34),(16,42,28),(118,92,50),(102,78,42)),     # mud
    3: ((36,40,48),(30,34,42),(94,96,104),(80,82,90)),      # stone
    4: ((36,54,72),(30,46,64),(128,148,170),(112,132,154)), # ice
    5: ((14,26,20),(12,22,18),(72,54,30),(60,44,24)),        # dark
}

PAL = [
    ("C_BG",         (8,14,10),     "背景纯黑底"),
    ("C_GRASS_A",    (24,58,30),    "草地主色"),
    ("C_GRASS_B",    (20,48,26),    "草地暗格"),
    ("C_GRASS_EDGE", (14,34,20),    "网格线"),
    ("C_PATH_A",     (96,74,38),    "路径主色"),
    ("C_PATH_B",     (82,63,32),    "路径暗格"),
    ("C_PATH_EDGE",  (58,44,22),    "路径描边"),
    ("C_ROCK",       (52,54,60),    "禁建岩层"),           # v0.4
    ("C_ROCK_HI",    (96,100,112),  "岩层高光"),
    ("C_ROCK_LO",    (30,32,36),    "岩层阴影"),
    ("C_TOWER_BASE", (70,74,86),    "塔基座"),
    ("C_TOWER_BODY", (120,128,146), "塔身"),
    ("C_TOWER_TOP",  (178,190,208), "塔顶高光"),
    ("C_TOWER_MUZZLE",(255,196,64), "枪口焰"),
    ("C_BULLET",     (255,240,176), "子弹"),
    ("C_TOWER_GAT",  (150,172,208), "机枪塔"),
    ("C_TOWER_CAN",  (176,98,54),   "加农塔"),
    ("C_TOWER_FRO",  (96,178,208),  "冰霜塔"),
    ("C_FROST_RING", (120,214,238), "冰霜/减速"),
    ("C_SPLASH",     (255,138,40),  "溅射爆炸"),
    ("C_ENEMY_A",    (208,56,48),   "GRUNT"),
    ("C_ENEMY_B",    (232,150,40),  "RUNNER"),
    ("C_ENEMY_C",    (128,64,176),  "TANK"),
    ("C_ENEMY_SWARM",(204,196,70),  "SWARM"),
    ("C_ENEMY_ARMOR",(150,164,178), "ARMOR"),
    ("C_ENEMY_BOSS", (196,30,84),   "BOSS"),
    ("C_ENEMY_HEAL", (90,224,130),  "HEALER"),
    ("C_ENEMY_CHOPPER",(70,190,235),"CHOPPER 空中"),   # v0.3.0
    ("C_ENEMY_SHIELD", (176,150,90), "SHIELD 弹盾"),      # v0.3.2
    ("C_TOWER_ENERGY",(64,208,120),  "能量塔"),          # v0.3.0
    ("C_TOWER_LASER", (240,80,60),   "激光塔"),          # v0.3.0
    ("C_LASER_BEAM",  (255,140,90),  "激光光束"),        # v0.3.0
    ("C_STAR",        (255,214,80),  "星级"),            # v0.3.0
    ("C_STAR_EMPTY",  (74,86,98),    "空星位"),          # v0.3.0
    ("C_SHADOW",      (6,10,8),      "空中单位投影"),     # v0.3.0
    ("C_ELITE",      (255,214,80),  "ELITE金边"),
    ("C_SLOT_BG",    (16,26,34),    "槽底"),
    ("C_SLOT_SEL",   (255,206,74),  "槽选中"),
    ("C_HP_BACK",    (58,12,12),    "血条底"),
    ("C_HP_FULL",    (56,208,74),   "血条满"),
    ("C_HP_LOW",     (232,64,40),   "血条低"),
    ("C_TEXT",       (236,240,232), "主文字"),
    ("C_TEXT_DIM",   (120,134,122), "次要文字"),
    ("C_GOLD",       (255,206,74),  "金币"),
    ("C_UI_BG",      (12,20,16),    "UI条底"),
    ("C_BTN",        (34,86,52),    "按钮底"),
    ("C_BTN_HI",     (56,140,78),   "按钮高亮"),
    ("C_BTN_TEXT",   (214,255,220), "按钮文字"),
    ("C_BAD",        (196,48,40),   "无效/生命"),
    ("C_FX_WHITE",   (255,255,255), "闪光"),
    ("C_LOCK",       (70,84,96),    "未解锁灰"),
    ("C_WARN",       (255,80,40),   "BOSS警告"),
]

# ── v0.5.0 路径重映射：把 32 列基准的关卡映射到任意列数 ─────────────
# 背景：L1-L10 是**手写路径**，折点列坐标是 32 列下的绝对值（2/6/14/25/
#   27/28/29/30/31…）。换到 claw4 的 18 列后全部越界。
#
# 为什么不做「自动等比缩放」而是留人工覆盖：
#   塔防地图的价值在于**每格的位置**。31→17 的等比压缩会让折返间距从
#   2-4 格塌成 1-2 格，路径变成锯齿，难度曲线失真；而且 18 列的横向自由度
#   只有原来的 56%，根本塞不下 14 行的复杂折线拓扑。
#   所以：L11-L20 用 snake() 生成器（已参数化，天然跨 profile 合法），
#   L1-L10 逐张**人工重画**（阶段 4 的工作）。remap_cols() 只作为
#   「拓扑足够简单、缩放后仍成立」关卡的现成实现（L1 的一条直路），
#   同时是人工重画时的参考算法。
REF_COLS = 32                # L1-L10 手写路径的基准列数（tab5）

def remap_cols(wps, ref_cols=REF_COLS):
    """把一条轴对齐折线的列坐标从 ref_cols 基准映射到当前 COLS。
    出生点(col=ref_cols-1)与出口(col=0)强制钉在两端——这是 path_cells 的
    硬约束，不能靠缩放碰运气。"""
    out = []
    for (c, r) in wps:
        if c == ref_cols - 1:      nc = COLS - 1
        elif c == 0:               nc = 0
        else:                      nc = int(round(c * (COLS - 1) / (ref_cols - 1)))
        out.append((max(0, min(COLS - 1, nc)), r))
    return out


# ── 校验 + 路径展开 ───────────────────────────────────────────────
def path_cells(routes):
    cells = set()
    for wps in routes:
        assert len(wps) >= 2, "route too short: %r" % (wps,)
        assert wps[0][0] == COLS - 1, "spawn at right edge: %r" % (wps[0],)
        assert wps[-1][0] == 0, "exit at left edge: %r" % (wps[-1],)
        for (c0, r0), (c1, r1) in zip(wps, wps[1:]):
            assert c0 == c1 or r0 == r1, "axis-aligned only: %r->%r" % ((c0,r0),(c1,r1))
            assert 0 <= c0 < COLS and 0 <= r0 < MAP_ROWS, "off grid: %r" % ((c0,r0),)
            assert 0 <= c1 < COLS and 0 <= r1 < MAP_ROWS, "off grid: %r" % ((c1,r1),)
            if r0 == r1:
                for c in range(min(c0,c1), max(c0,c1)+1): cells.add((c,r0))
            else:
                for r in range(min(r0,r1), max(r0,r1)+1): cells.add((c0,r))
    return cells

def validate():
    for lv in LEVELS:
        assert 1 <= lv.get("boss_count", 1) <= 2, "L%d boss_count" % lv["id"]
        assert len(lv["kinds"]) <= 6, "L%d pool > 6" % lv["id"]
        assert len(lv["routes"]) <= 2, "L%d routes > 2" % lv["id"]
        assert len(path_cells(lv["routes"])) >= 12, "L%d path too short" % lv["id"]
        # v0.4 mid-boss：至多2次、必须严格早于末波（末波=真王压轴，§381 联动）
        mids = lv.get("mid", [])
        assert len(mids) <= 2, "L%d mid > 2" % lv["id"]
        for m in mids:
            assert 1 <= m < lv["waves"], "L%d mid wave %d out of range" % (lv["id"], m)
        assert len(set(mids)) == len(mids), "L%d mid waves dup" % lv["id"]
        # v0.4 ability 位掩码合法域（8技能，uint16_t）
        assert 0 <= lv["boss"][7] < 512, "L%d ability out of range" % lv["id"]
    assert len(LEVELS) == 20, "need exactly 20 levels"
    assert [l["id"] for l in LEVELS] == list(range(1, 21))
    ds = [l["diff"] for l in LEVELS]
    assert ds == sorted(ds), "difficulty must be monotonic: %r" % (ds,)
    # v0.4 覆盖预算审计（机器可判，不信手感）：
    #   gap>0  → 有路格任何塔都打不到 = 不可玩（手画石区踩过的坑）
    #   towers>MAX_TOWERS → 塔位不够罩全路 = 预算题不可通（伪难度）
    for lv in LEVELS:
        a = audit_level(lv)
        assert a["gap"] == 0, "L%d 有 %d 个路格打不到（不可玩）" % (lv["id"], a["gap"])
        assert a["towers"] <= MAX_TOWERS, (
            "L%d 最少需 %d 座塔 > MAX_TOWERS=%d（这条路罩不住）"
            % (lv["id"], a["towers"], MAX_TOWERS))

# ── 生成 C 头文件（多路径打包：每条 route 以 (len,0) 哨兵开头）──
def gen_header():
    all_wp, refs = [], []
    for lv in LEVELS:
        start = len(all_wp)
        route_offs = []
        for route in lv["routes"]:
            route_offs.append(len(all_wp))
            all_wp.append((len(route), 0))          # 哨兵：本条 route 点数
            all_wp.extend(route)
        refs.append((start, len(all_wp) - start, route_offs))

    wp_c = ", ".join("{%d,%d}" % wc for wc in all_wp)

    # v0.4：禁建格位掩码（bit c = 该格不可建；每关 MAP_ROWS 个 uint32）
    blk_rows = []
    for lv in LEVELS:
        blocked = blocked_cells(lv)
        rows_c = ", ".join(
            "0x%08Xu" % sum(1 << c for c in range(COLS) if (c, r) in blocked)
            for r in range(MAP_ROWS))
        blk_rows.append("  { %s },  // L%d %s" % (rows_c, lv["id"], lv["name"]))
    blocked_c = "\n".join(blk_rows)

    lv_c = []
    for lv, (st, n, offs) in zip(LEVELS, refs):
        pool = list(lv["kinds"]) + [-1] * (6 - len(lv["kinds"]))
        bn, bhp, bspd, bsize, barrt, bgold, bleak, bab = lv["boss"]
        assert 0 <= st < 32768 and len(offs) == lv.get("routes_n", len(lv["routes"]))
        mid = list(lv.get("mid", [])) + [0] * (2 - len(lv.get("mid", [])))
        lv_c.append(
            '  { "%s", %d, %d, %.2ff, %d, %d, %d, { %s }, "%s", %.2ff, %d, %d, %d, %d, %d, %d, %d, { %d, %d } },  // L%d'
            % (lv["name"], st, len(lv["routes"]), lv["diff"], lv["waves"],
               lv["theme"], lv["elite_from"], ", ".join(str(k) for k in pool),
               bn, bhp, int(bspd), bsize, barrt, bgold, bleak, bab,
               lv.get("boss_count", 1), mid[0], mid[1], lv["id"]))

    kinds_c = "\n".join('  { "%s", %.2ff, %d, %d, %d, %d, %d, %d, %d },' % k for k in KINDS)
    towers_c = "\n".join('  { "%s", "%s", %d, %d, %d, %d, %d, %d, %d },' % t
                         for t in TOWER_TYPES)
    air_c = ", ".join(str(a) for a in TOWER_AIR_OK)
    ups_c = "\n".join('  {0, %d, %d},' % UPGRADE_COST[k] for (k, *_r) in TOWER_TYPES)
    pal = "\n".join("#define %-14s 0x%04X  // %s"
                    % (n, ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3), note)
                    for (n, (r, g, b), note) in PAL)

    themes_c = "\n".join(
        '  { 0x%04X, 0x%04X, 0x%04X, 0x%04X },  // theme %d'
        % (rgb565(gA), rgb565(gB), rgb565(pA), rgb565(pB), tid)
        for tid, (gA, gB, pA, pB) in sorted(THEMES.items()))

    txt = HEADER_TPL % dict(
        sw=SCREEN_W, sh=SCREEN_H, cell=CELL, cols=COLS, rowsT=ROWS_TOTAL,
        hudR=HUD_ROWS, mapTop=MAP_TOP, bbh=BUILD_BAR_ROWS * CELL,
        mapR=MAP_ROWS, wp=wp_c, me=MAX_ENEMIES, mt=MAX_TOWERS,
        mb=MAX_BULLETS, mf=MAX_FX, gold=START_GOLD, lives=START_LIVES,
        sell=SELL_REFUND_PCT, wb=WAVE_BONUS_BASE, ws=WAVE_BONUS_STEP,
        hpg=HP_GROWTH, towers=towers_c, ups=ups_c, tml=TOWER_MAX_LEVEL,
        hpb=HP_BASE, kgp=KILL_GOLD_PCT, anm=AUTO_NEXT_MS, ovl=OVERLAP_MS,
        olp=OVERLAP_LEFTOVER_PCT, olm=OVERLAP_LEFTOVER_MIN,
        fsp=FROST_SLOW_PCT, fsm=FROST_SLOW_MS, kinds=kinds_c,
        ksw=K_SWARM, kar=K_ARMOR, kbo=K_BOSS, khe=K_HEAL,
        sb=SWARM_BURST, afw=ARMOR_FROM_WAVE,
        hpct=HEAL_PCT, hms=HEAL_MS, bhp=BOSS_HEAL_PCT, bhms=BOSS_HEAL_MS,
        bbm=BOSS_BROOD_MS, bbs=BOSS_BROOD_SIZE,
        ehm=ELITE_HP_MULT, ega=ELITE_GOLD_ADD, abh=AB_HEAL, abb=AB_BROOD,
        abs_=AB_SPLIT, abh2=AB_HORN, abe=AB_EMP, abg=AB_GREED,
        abm=AB_MOLT, abgh=AB_GHOST, sps=SPLIT_SIZE,
        hms2=HORN_MS, hbpp=HORN_BOOST_PCT, hdms=HORN_DUR_MS,
        ems=EMP_MS, etw=EMP_TOWERS, edms=EMP_DUR_MS,
        ghi=GREED_HITS, gga=GREED_AMT, gccp=GREED_CAP_PCT,
        mpc=MOLT_PCT, msp=MOLT_SPD_PCT, msd=MOLT_SIZE_DROP,
        gms=GHOST_MS, gdms=GHOST_DUR_MS,
        absg=AB_SIEGE, sms=SIEGE_MS, sr=SIEGE_R, siv=SIEGE_IV,   # v0.4.2 SIEGE
        midh=MID_HP_PCT, midg=MID_GOLD_PCT, mids=MID_SIZE_DROP,
        edp=ENDLESS_DIFF_PCT, erw=ENDLESS_ROUND_WAVES, eme=ENDLESS_MINI_EVERY,
        enw=ENDLESS_WAVES,
        air=air_c, lbm=LASER_BEAM_MS, s3=STAR3_PCT, s2=STAR2_PCT,
        kch=K_CHOPPER, afwv=AIR_FROM_WAVE,
        ksh=K_SHIELD, shw=SHIELD_FROM_WAVE,
        levels="\n".join(lv_c), nlv=len(LEVELS), pal=pal, themes=themes_c,
        blocked=blocked_c,
        # v0.5.1 内容驱动的横向 UI 尺寸（见 PROFILES 注释）
        btn_w=BTN_W, panel_w=PANEL_W,
        sel_cols=SEL_COLS, sel_rows=SEL_ROWS,
        sel_node_w=SEL_NODE_W, sel_node_h=SEL_NODE_H,
        sel_gap_x=SEL_GAP_X, sel_gap_y=SEL_GAP_Y, sel_y0=SEL_Y0,
        over_w=OVER_W, onboard_w=ONBOARD_W, ds=DIFF_SCALE,
        cjk=_P["cjk"], bgt=_P["bg_tile"])
    open(out_header(), "w").write(txt)

HEADER_TPL = """// SPDX-License-Identifier: MIT
// Copyright (c) 2026 andjiang0083
// config_gen.h -- GENERATED by tools/layout.py. DO NOT EDIT BY HAND.
// 网格 / 关卡 / 塔表 / 敌人表 / 数值 / 调色板唯一真源：tools/layout.py
#pragma once
#include <stdint.h>
#include <stddef.h>

// ── 关卡难度缩放（profile 级配平旋钮）──────────────────────────────
// 敌人生血统一乘这个系数（见 game.h 的 lvDiff()）。
// tab5 = 1.00（设计基准，改了就是改难度）；claw4 < 1，补偿"18 列地图
// 路径短 → 塔的输出窗口小"这个 design_range 管不到的二阶效应。
// 值由模拟器反推（20 关全部 bot 可通的最低档），不要手拍。
#define PTD_DIFF_SCALE %(ds).2ff

// ── profile 能力宏（v0.5.3）─────────────────────────────────────────
// 共享层（i18n.h / pixel_text.h / render.h）只认能力，不认板名：
//   1 = 这个 profile 放得下 16x16 中文字形（双语界面 + 标题界面语言按钮）
//   0 = 单语（英文），中文那整条链路（CN 表/字库/按钮/NVS "lang"）不参与编译
// 为什么不是按板名开关：board-specific `#if` 写在共享层 = 板级差异泄漏到共享层，
//   就是 v0.5.0「为 Claw4 删掉 Tab5 的中文」那一轮的形状。
// 依据：中文字形 17px/字（不随 scale 缩），claw4 的 116px 槽 / 202px 面板放不下。
#define PTD_HAS_CJK %(cjk)d

// ── 另一个 profile 能力（v0.5.3）：背景格子缓存块拷贝 ────────────────
//   1 = 擦背景格子走"一格一次 pushImage"（后台先缓存每类格子的 40x40 像素）
//   0 = 逐矩形重画（Claw4 现状；它的帧缓冲写法不同，等它自己量完再开）
// 为什么是能力宏而不是板名：同 PTD_HAS_CJK，板级差异不许写进共享层。
#define PTD_BG_TILE_BLIT %(bgt)d

#define SCREEN_W      %(sw)d
#define SCREEN_H      %(sh)d
#define CELL          %(cell)d
#define COLS          %(cols)d
#define ROWS_TOTAL    %(rowsT)d
#define HUD_ROWS      %(hudR)d
#define MAP_TOP       %(mapTop)d
#define BUILD_BAR_H   %(bbh)d
#define MAP_ROWS      %(mapR)d

struct Waypoint { int8_t col; int8_t row; };
// 多路径打包：每条 route = (count,0) 哨兵 + count 个点。
// LevelDef.wp_start 指向该关第一条 route；route_wp_off[] 给每条的偏移。
static const Waypoint ALL_WP[] = { %(wp)s };

#define MAX_ENEMIES   %(me)d
#define MAX_TOWERS    %(mt)d
#define MAX_BULLETS   %(mb)d
#define MAX_FX        %(mf)d

// ── v0.5.0：UI 几何按 SCREEN_W 自适应（不再写死 1280 基准的绝对值）──
//⚠️ 病根：这段 UI 几何原本是 8 个裸数字（BTN_X=1064 / SLOT_W=200 ...），
//   隐含前提是"屏宽 1280"。换屏就崩——claw4 的 720 宽下
//   5×SLOT_W=1000+间隙 1048 > 720，建造栏提示全部跑到屏外（模拟器报
//   "[EN] buildbar: TAP_SLOT hint 94px offscreen"）；
//   BTN_X=1064 > 720，NEXT WAVE 按钮整个在屏外。
//
//   修法：所有横向几何锚在右缘（SCREEN_W - w - margin）或按比例（pct/1000），
//   纵向按 MAP_TOP / BUILD_BAR_H 居中。这样换 profile 时不需要再改这里，
//   也不会出现"漏改了某个硬编码"的沉默回归——布局的每一处都可见地依赖 SCREEN_W。

// NEXT WAVE 按钮：锚右缘。
// ⚠️ v0.5.1：宽度从"屏宽 × 15.625%%"改回**profile 给的绝对值**。
//   比例法的病根：容器里装的是字，字宽只由 scale 定、不随屏宽连续变化。
//   720 × 15.625%% = 112，而 "NEXT WAVE"@scale2 = 108，加 20 内边距要 128
//   ——模拟器直接报 "[EN] btn: NEXT_WAVE 106px overflows"。
//   容器宽度是**内容驱动的下界**，不是屏宽的百分比。
#define BTN_W        %(btn_w)d
#define BTN_X        (SCREEN_W - BTN_W - 12)
#define BTN_H        (MAP_TOP - 16)
#define BTN_Y        8

#define BUILD_BAR_Y   (SCREEN_H - BUILD_BAR_H)
// v0.5.0：槽区 = 屏宽 5/6，右侧 1/6 留给操作提示（见 render.h 的 BAR_HINT_X）。
//   ⚠️ v0.4.x 的 SLOT_W=200×5 + 间隙 32 + X0 16 = 1048px，在 1280 屏上
//   "排满屏幕、右侧留提示列"（原作者注释原话），但作者没在断言里锁这个账——
//   换到 720 宽就溢出（模拟器报 "TAP_SLOT hint 94px offscreen"）。
//   现在按屏宽分区，任何宽度都不溢出。
//
//⚠️ 槽位几何的真源在这里而不是 render.h：渲染层（render.h 的 drawBuildBar）
//   与命中判定层（touch.h 的 hitSlot）都要用，而 touch.h 不能 include
//   render.h（render.h → game.h → touch.h 会形成循环依赖）。
//   v0.4.x 的槽位几何因此被复制成两份（config_gen.h 一份、render.h 又按
//   slotX() 自己算一份），改一处必漏另一处——正是 v0.4.3 注释里记的那类 bug。
//   现在单一真源在 config_gen.h，两侧都从这里取。
#define SLOT_X0       8
#define SLOT_GAP      4
#define SLOT_H        (BUILD_BAR_H - 8)
#define BAR_SLOT_W    ((SCREEN_W * 5) / 6)          // 槽区总宽
#define SLOT_W        ((BAR_SLOT_W - 4 * SLOT_GAP) / 5)   // 单槽宽
#define BAR_HINT_X    (SLOT_X0 + BAR_SLOT_W)        // 提示列左缘
#define BAR_HINT_W    (SCREEN_W - BAR_HINT_X - 6)
#define BAR_HINT_SCALE (BAR_HINT_W >= 100 ? 2 : 1)  // 提示字号（窄列用 1）
#define SLOT_NSCALE   (SLOT_W >= 180 ? 2 : 1)       // 塔名字号（窄槽用 1）
#define SLOT_TX       40                            // 槽内文字起始 x（图标右侧）

// 塔面板：宽 = profile 绝对值（tab5 360 / claw4 300），高 = 地图区高 × 36.6%%
// ⚠️ v0.5.1：同 BTN_W，宽度改回内容驱动的绝对值。
//   720 × 28.125%% = 202，而面板内容（属性行 210 起 + 60 宽 + 14 边距）
//   需要 284 —— 属性文字直接画到面板框外（模拟器 3 项 overflow）。
//   360 在 720 屏上占 50%%、300 占 41.7%%，都是"必须的代价"：
//   面板是模态的（打开时世界暂停、且会自动翻到塔的另一侧），可以宽。
#define PANEL_W       %(panel_w)d
#define PANEL_H       ((MAP_TOP + MAP_ROWS*CELL) * 366) / 1000
// 面板内两列属性标签/数值的 x（相对面板左缘）。
//   tab5(360)： 14 /  69 / 150 / 209   ← 与 v0.4.x 的硬编码逐像素一致
//   claw4(300)：14 /  58 / 125 / 174
// 比例而非固定值：面板变窄时列跟着收，不会把数值列推出右边缘。
#define PANEL_PAD     14
#define PA_LBL1_X     (PANEL_PAD)
#define PA_VAL1_X     ((PANEL_W * 194) / 1000)
#define PA_LBL2_X     ((PANEL_W * 417) / 1000)
#define PA_VAL2_X     ((PANEL_W * 583) / 1000)
#define PBTN_UP_X     12
#define PBTN_UP_Y     (PANEL_H / 2 - 36)
#define PBTN_W        ((PANEL_W - 32) / 2)
#define PBTN_H        72

// 居中浮层（GAME OVER / LEVEL CLEAR / 引导）：宽度夹到屏宽内，留 40 边距。
//   tab5：min(760, 1240) = 760 / min(820, 1240) = 820（原值不变）
//   claw4：min(760, 680) = 680 / min(820, 680) = 680
// ⚠️ v0.5.1：原实现写死 760/820，720 屏上 (720-760)/2 = -20 —— 面板左右
//   各出屏 20px，且引导框最外侧的点击不在命中区里（教程推不动）。
//   这类"居中但忘了夹宽度"的 bug 没有断言能抓（见 sim/run.cpp 新增的
//   checkOverlayFit）。
#define OVER_W        (%(over_w)d < (SCREEN_W - 40) ? %(over_w)d : (SCREEN_W - 40))
#define ONBOARD_W     (%(onboard_w)d < (SCREEN_W - 40) ? %(onboard_w)d : (SCREEN_W - 40))
#define ONBOARD_H     130

#define START_GOLD      %(gold)d
#define START_LIVES     %(lives)d
#define SELL_REFUND_PCT %(sell)d
#define WAVE_BONUS_BASE %(wb)d
#define WAVE_BONUS_STEP %(ws)d
// 关内血量复利；关卡间另乘 LevelDef.diff（见 LEVELS）
#define HP_GROWTH       %(hpg).2ff
// 血量基数（v0.2.1：40→85，拒绝"一枪秒"）
#define HP_BASE         %(hpb)d
// 击杀赏金系数（v0.2.1：血量上调后同步 +30%%，经济跟着血线走）
#define KILL_GOLD_PCT   %(kgp)d
// 波间自动开波倒计时（v0.3.2：10s→6s）
#define AUTO_NEXT_MS    %(anm)d
// 压叠窗口（v0.3.2，v0.5.2 修判据）：本波刷完且"本波已清完" → ms 后强开下波
#define OVERLAP_MS      %(ovl)d
#define OVERLAP_LEFTOVER_PCT %(olp)d   // 本波幸存者 ≤ 本波规模的百分之几（v0.5.2）
#define OVERLAP_LEFTOVER_MIN %(olm)d   // 至少留这么多只（小波兜底，v0.5.2）
// ⛔ 已删除（v0.5.3）：曾有的 OVERLAP_FIELD_MAX（场上总数红线）——
//   那是拿"少放敌人"绕开帧预算问题；真机实测修掉擦格碎块税后 48 只同屏 30fps 稳，
//   判据已删（见 docs/FIXLOG-v0.5.3.md）。

struct TowerType { const char* key; const char* disp; int cost, range_px, fire_ms,
                   damage, proj_speed, splash_r; uint8_t behavior; };
static const TowerType TOWER_TYPES[] = {
%(towers)s
};
#define TT_COUNT ((int)(sizeof(TOWER_TYPES)/sizeof(TOWER_TYPES[0])))

static const int TT_UP_COST[TT_COUNT][3] = {
%(ups)s
};

#define TOWER_MAX_LEVEL   %(tml)d
// v0.3.2：1.5→1.3 / 0.25→0.12 —— "三座满级机枪平推"根因：
// LV3 复合 3.1x DPS + 射程(=覆盖时间)×1.5 ≈ 4.7x 有效输出，全关溢出。
#define TOWER_DMG_SCALE   1.3f
#define TOWER_RANGE_SCALE 0.12f

#define FROST_SLOW_PCT  %(fsp)d
#define FROST_SLOW_MS   %(fsm)d

struct EnemyKind { const char* name; float hp_scale; int speed_pps; int size_px;
                   int gold; uint8_t leak_dmg; uint8_t armor_flat; uint8_t flying;
                   uint8_t shield; };
static const EnemyKind ENEMY_KINDS[] = {
%(kinds)s
};
#define KIND_COUNT ((int)(sizeof(ENEMY_KINDS)/sizeof(ENEMY_KINDS[0])))

#define K_SWARM %(ksw)d
#define K_ARMOR %(kar)d
#define K_BOSS  %(kbo)d
#define K_HEAL  %(khe)d
#define K_CHOPPER %(kch)d          // v0.3.0: air unit
#define SWARM_BURST     %(sb)d
#define ARMOR_FROM_WAVE %(afw)d
#define AIR_FROM_WAVE   %(afw)d   // v0.3.0: choppers join from this wave
#define K_SHIELD        %(ksh)d           // v0.3.2: bullet-damage halved unit
#define SHIELD_FROM_WAVE %(shw)d

// ── v0.3.0 anti-air matrix: 1 = tower can target flying enemies ──
static const uint8_t TOWER_AIR_OK[TT_COUNT] = { %(air)s };
#define LASER_BEAM_MS   %(lbm)d    // beam stays this long (visual only)
#define STAR3_PCT       %(s3)d      // clear with >= this %% lives -> 3 stars
#define STAR2_PCT       %(s2)d

// healer / boss abilities (v0.2.0)
#define HEAL_PCT        %(hpct)d     // healer pulse: +%%maxHp to allies r<90
#define HEAL_MS         %(hms)d
#define BOSS_HEAL_PCT   %(bhp)d      // boss heal aura +%%%%maxHp
#define BOSS_HEAL_MS    %(bhms)d
#define BOSS_BROOD_MS   %(bbm)d      // boss spawns SWARM every ms
#define BOSS_BROOD_SIZE %(bbs)d
#define ELITE_HP_MULT   %(ehm).1ff
#define ELITE_GOLD_ADD  %(ega)d

// ── v0.4 BOSS ability bitmask（"会耍坏的王"）──
#define AB_HEAL   %(abh)d            // heal aura (self)
#define AB_BROOD  %(abb)d            // brood spawn
#define AB_SPLIT  %(abs_)d          // 💀 death split: burst minions on death
#define AB_HORN   %(abh2)d          // 📢 horn: all enemies +speed
#define AB_EMP    %(abe)d           // 🔇 silence nearest towers
#define AB_GREED  %(abg)d           // 💰 steal gold on hit
#define AB_MOLT   %(abm)d           // 🛡 shed armor at half hp, go feral
#define AB_GHOST  %(abgh)d          // 🌫 periodic untargetable ghost
#define AB_SIEGE  %(absg)d          // 🚧 v0.4.2: lock build zone near boss
#define SIEGE_MS  %(sms)d           // 🚧 build-zone lock duration
#define SIEGE_IV  %(siv)d           // 🚧 lock cadence (must be > SIEGE_MS)
#define SIEGE_R   %(sr)d            // 🚧 lock radius px (4.5 cells)
#define SPLIT_SIZE        %(sps)d   // split minions base count (+wave/8)
#define HORN_MS         %(hms2)d    // horn cadence
#define HORN_BOOST_PCT  %(hbpp)d    // +%% speed while boosted
#define HORN_DUR_MS     %(hdms)d    // boost lasts
#define EMP_MS          %(ems)d     // emp cadence
#define EMP_TOWERS      %(etw)d     // towers silenced per blast
#define EMP_DUR_MS      %(edms)d    // silence lasts (tower.stunMs)
#define GREED_HITS      %(ghi)d     // hits per steal
#define GREED_AMT       %(gga)d     // $ stolen per proc
#define GREED_CAP_PCT   %(gccp)d    // steal <= this %% of current gold
#define MOLT_PCT        %(mpc)d     // trigger below this hp %%
#define MOLT_SPD_PCT    %(msp)d     // speed bonus after molt
#define MOLT_SIZE_DROP  %(msd)d     // body shrinks px
#define GHOST_MS        %(gms)d     // ghost cadence
#define GHOST_DUR_MS    %(gdms)d    // ghost lasts
// mid-boss（一关多王：半程波出缩小版王）
#define MID_HP_PCT      %(midh)d    // hp ×%% of real boss
#define MID_GOLD_PCT    %(midg)d    // gold ×%%
#define MID_SIZE_DROP   %(mids)d    // size -px
// endless（L10 CLEAR 后 TERMINAL 无限刷）
#define ENDLESS_WAVES      %(enw)d  // total waves cap in endless (lvWaves)
#define ENDLESS_DIFF_PCT   %(edp)d  // diff ×(1+%%) per round
#define ENDLESS_ROUND_WAVES %(erw)d // one round = N waves; boss on last
#define ENDLESS_MINI_EVERY  %(eme)d // mini-boss cadence

// ── levels (v0.2.0)：10 关，diff 单调递增 ──
struct LevelDef {
  const char* name; int16_t wp_start; uint8_t routes_n; float diff;
  uint8_t waves; uint8_t theme; int8_t elite_from; int8_t kinds[6];
  const char* boss_name; float boss_hp_mult; int16_t boss_speed;
  uint8_t boss_size, boss_armor; int16_t boss_gold; uint8_t boss_leak;
  uint16_t ability;   // v0.4.2: 技能位掩码升 16 位（AB_SIEGE=256 是第 9 位）
  uint8_t boss_count;                  // twin bosses spawn together (last wave)
  uint8_t mid[2];                      // v0.4: mid-boss waves (0 = none)
};
static const LevelDef LEVELS[] = {
%(levels)s
};
#define LEVEL_COUNT ((int)(sizeof(LEVELS)/sizeof(LEVELS[0])))

// ── v0.4 禁建岩层位掩码（bit c = 该格不可建；路格本就不可建，不在此表）──
// 设计哲学：难度靠削减"好位置"（覆盖率），不靠堆数值曲线。
// 旧关 L1-L10 全 0（全开放）；新关 L11-L20 用"贴路窄带"逼出行间位取舍。
static const uint32_t BLOCKED[LEVEL_COUNT][MAP_ROWS] = {
%(blocked)s
};

// ── RGB565 palette ──
%(pal)s

// ── themes (v0.2.0): grassA/grassB/pathA/pathB per level theme ──
struct Theme { uint16_t grassA, grassB, pathA, pathB; };
static const Theme THEMES[] = {
%(themes)s
};
#define THEME_COUNT ((int)(sizeof(THEMES)/sizeof(THEMES[0])))

// ── level select screen: node grid（20 关）──
// v0.5.1：列数/节点尺寸改为 profile 提供。
//   tab5  5 列 × 4 行，170x110 间距 35/26 → 网格 990x518（左右各留 145）
//   claw4 4 列 × 5 行，148x100 间距 22/18 → 网格 658x572（左右各留 31）
// 原实现把 5×170 写死，720 屏上网格 990 宽 → 居中后左右各出屏 135px，
// L1/L5/L11/L16 四个节点完全看不见。**这个没有任何断言在守**
// （v0.5.0 的断言只看文字宽度，不看容器）——所以只能靠真机截图发现。
#define SEL_COLS      %(sel_cols)d
#define SEL_ROWS      %(sel_rows)d
#define SEL_NODE_W    %(sel_node_w)d
#define SEL_NODE_H    %(sel_node_h)d
#define SEL_GAP_X     %(sel_gap_x)d
#define SEL_GAP_Y     %(sel_gap_y)d
#define SEL_GRID_W    (SEL_COLS*SEL_NODE_W + (SEL_COLS-1)*SEL_GAP_X)
#define SEL_GRID_H    (SEL_ROWS*SEL_NODE_H + (SEL_ROWS-1)*SEL_GAP_Y)
static inline int selNodeX(int i) { return (SCREEN_W - SEL_GRID_W) / 2 + (i %% SEL_COLS) * (SEL_NODE_W + SEL_GAP_X); }
static inline int selNodeY(int i) { return %(sel_y0)d + (i / SEL_COLS) * (SEL_NODE_H + SEL_GAP_Y); }
// 节点内部三行（编号 / 关卡名 / 星级）的 y 偏移，按节点高度比例
#define SEL_NUM_DY    ((SEL_NODE_H * 127) / 1000)
#define SEL_NAME_DY   ((SEL_NODE_H * 564) / 1000)
#define SEL_STAR_DY   ((SEL_NODE_H * 745) / 1000)
"""


def png(path, w, h, px):
    raw = b"".join(b"\x00" + bytes(px[y*w*3:(y+1)*w*3]) for y in range(h))
    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
                           + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def rgb565(rgb):
    r, g, b = rgb
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def unmap(n16):
    return (((n16 >> 11) & 0x1F) << 3, ((n16 >> 5) & 0x3F) << 2, (n16 & 0x1F) << 3)


def render_board(routes, theme):
    buf = bytearray(SCREEN_W * SCREEN_H * 3)

    def rect(x, y, w, h, rgb):
        x0, x1 = max(0, int(x)), min(SCREEN_W, int(x + w))
        y0, y1 = max(0, int(y)), min(SCREEN_H, int(y + h))
        blob = bytes(rgb)
        for yy in range(y0, y1):
            o = yy * SCREEN_W * 3 + x0 * 3
            for xx in range(x0, x1):
                buf[o:o+3] = blob; o += 3

    gA, gB, pA, pB = THEMES[theme]
    PATH = path_cells(routes)
    rect(0, 0, SCREEN_W, MAP_TOP, unmap(rgb565((12, 20, 16))))
    rect(0, SCREEN_H - BUILD_BAR_ROWS * CELL, SCREEN_W, MAP_TOP,
         unmap(rgb565((12, 20, 16))))
    for r in range(MAP_ROWS):
        for c in range(COLS):
            x, y = c * CELL, MAP_TOP + r * CELL
            if (c, r) in PATH:
                rect(x, y, CELL, CELL, unmap(rgb565(pA)))
                for i in range(0, CELL, 8):
                    for j in range((i // 8) % 2 * 4, CELL, 8):
                        rect(x + i, y + j, 4, 4, unmap(rgb565(pB)))
            else:
                rect(x, y, CELL, CELL, unmap(rgb565(gA if (c + r) % 2 == 0 else gB)))
                rect(x, y, CELL, 1, unmap(rgb565((14, 34, 20))))
                rect(x, y, 1, CELL, unmap(rgb565((14, 34, 20))))
    # 出生/出口标记：右缘红块、左缘金块
    for wps in routes:
        c, r = wps[0]
        rect(c*CELL+8, MAP_TOP+r*CELL+12, 24, 16, unmap(rgb565((196, 48, 40))))
        c, r = wps[-1]
        rect(c*CELL+8, MAP_TOP+r*CELL+12, 24, 16, unmap(rgb565((255, 206, 74))))
    return buf


# ── 输出路径 ─────────────────────────────────────────────────────────
# v0.5.0：生成物从 src/ 移到 build/<profile>/。
#
# 为什么必须挪走（一次踩过的坑）：
#   同一份 src/*.h 要能同时编译成 tab5 版和 claw4 版，两者只差 config_gen.h。
#   如果 config_gen.h 留在 src/，那么无论用哪个 -I 顺序，src/ 都会命中——
#   -Ibuild/claw4 在前时，sim/run.cpp 先 include "gfx_fake.h"→gfx_fb.h→
#   gfx.h→config_gen.h 命中 claw4 版；但 render.h 之后再 include "config_gen.h"
#   会被 #pragma once 判为"已包含"而跳过（pragma once 按路径去重，两份路径不同
#   其实各自展开 → struct Waypoint / ALL_WP / TowerType 全部 redefinition）。
#   改成 include guard 带 profile 名也不行：两份宏名不同 → 两份都展开 → 同样炸。
#
#   把生成物**完全移出 src/** 后，src/ 里没有任何 config_gen.h，选哪个 profile
#   就是选哪个 -I，冲突从根上消失。这也贴合上游"生成物不入源码目录"的纪律。
#
# CI 兼容：.github/workflows/sim.yml 里 `git diff --exit-code src/config_gen.h`
# 改为校验 build/tab5/config_gen.h（见该文件 v0.5.0 改动）。
BUILD_DIR = "build"
def out_dir(profile=None):
    return os.path.join(BUILD_DIR, profile or PROFILE)

def out_header(profile=None):
    return os.path.join(out_dir(profile), "config_gen.h")

def out_preview_prefix(profile=None):
    return os.path.join(out_dir(profile), "preview-lv")

DEFAULT_PROFILE = "tab5"


def main():
    ap = argparse.ArgumentParser(description="生成 config_gen.h（网格/关卡/数值真源）")
    ap.add_argument("--profile", default=None,
                    help="屏幕几何 profile（默认 %s）；'list' 列出全部" % DEFAULT_PROFILE)
    ap.add_argument("--no-preview", action="store_true",
                    help="只生成头文件，不渲染 preview PNG（CI/快速校验用）")
    args = ap.parse_args()

    if args.profile == "list":
        for k in sorted(PROFILES):
            p = PROFILES[k]
            mark = " (default)" if k == DEFAULT_PROFILE else ""
            print("  %-8s %4dx%-4d cell=%-3d %2dx%-2d grid  %s%s"
                  % (k, p["sw"], p["sh"], p["cell"],
                     p["sw"] // p["cell"], p["sh"] // p["cell"],
                     p["desc"], mark))
        return

    use_profile(args.profile or DEFAULT_PROFILE)
    os.makedirs(out_dir(), exist_ok=True)
    n_over = apply_profile_routes()
    if n_over and PROFILE != DEFAULT_PROFILE:
        print("profile routes: %d level(s) overridden for %s" % (n_over, PROFILE))
    validate()
    gen_header()
    hdr = out_header()
    print("profile=%s  %dx%d  cell=%d  COLS=%d MAP_ROWS=%d  design_range=%d"
          % (PROFILE, SCREEN_W, SCREEN_H, CELL, COLS, MAP_ROWS, DESIGN_GAT_RANGE))
    print("wrote %s (%d levels, %d kinds)" % (hdr, len(LEVELS), len(KINDS)))
    if args.no_preview:
        return
    pfx = out_preview_prefix()
    for lv in LEVELS:
        png("%s%02d.png" % (pfx, lv["id"]), SCREEN_W, SCREEN_H,
            render_board(lv["routes"], lv["theme"]))
        print("  L%-2d %-15s routes=%d waves=%2d diff=%.2f pathcells=%d"
              % (lv["id"], lv["name"], len(lv["routes"]), lv["waves"],
                 lv["diff"], len(path_cells(lv["routes"]))))


if __name__ == "__main__":
    main()
