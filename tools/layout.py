#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 andjiang0083
"""几何/路径/关卡的唯一真源 (single source of truth)。

v0.2.0：10 关卡系统 + 主题配色 + BOSS 参数化。产出：
  1) src/config_gen.h —— 固件网格/关卡表常量
  2) preview.png / preview-lvNN.png —— PC 端重渲染棋盘，先判构图再上真机
改布局/关卡只改这里。"""

import struct, zlib, os

# ── 屏幕与网格（Tab5 横屏 1280x720）───────────────────────────────
SCREEN_W, SCREEN_H = 1280, 720
CELL        = 40
COLS        = SCREEN_W // CELL   # 32
ROWS_TOTAL  = SCREEN_H // CELL   # 18
HUD_ROWS    = 2
MAP_TOP     = HUD_ROWS * CELL   # 80
BUILD_BAR_ROWS = 2
MAP_ROWS    = ROWS_TOTAL - HUD_ROWS - BUILD_BAR_ROWS   # 14

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
OVERLAP_MS   = 5000     # v0.3.2：刷怪完毕且残敌≥8→5s 后强开下波（压叠，防雪崩）

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
DESIGN_GAT_RANGE = 150        # 审计用尺 = GAT LV1 射程（最便宜塔=可达性地板）
LEGACY_LEVELS    = 10         # L1-L10 沿用旧图（无禁建）；L11-L20 走雕刻

def snake(rows, xa=2, xb=29):
    """蛇形路线生成器：rows = 折返行（升序、间隔≥2 才留得下行间位给塔）。
    首点右缘出生、末点左缘出口（path_cells 的硬约束）。"""
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
      routes=[snake([1,3,5,7,9,11,13], xa=1, xb=30)],  # 满屏七折，路最长、带最窄
      kinds=[0,1,2,8,7]),
 dict(id=18, name="STONE WEAVE", theme=0, waves=17, diff=2.35, elite_from=6,
      boss=("WEAVER", 12.0, 30, 64, 4, 380, 11, AB_MOLT | AB_GHOST | AB_SPLIT),
      mid=[8], band=1,
      routes=[snake([1,5,9,13])],                     # 折返间隔 4 行：路间大片岩区
      kinds=[0,2,6,8,7]),
 dict(id=19, name="GRINDER TWO", theme=2, waves=18, diff=2.40, elite_from=7,
      boss=("GRINDER PRIME", 14.0, 26, 72, 6, 400, 12, AB_EMP | AB_BROOD | AB_SPLIT | AB_GREED),
      boss_count=2, mid=[6, 12], band=1,
      routes=[snake([1,3,5,7,9,11,13], xa=1, xb=30)],
      kinds=[0,1,2,8,7]),
 dict(id=20, name="THE LAST WALL", theme=5, waves=18, diff=2.45, elite_from=7,
      boss=("THE WALL", 18.0, 22, 76, 10, 450, 14,
            AB_HEAL | AB_BROOD | AB_SPLIT | AB_EMP | AB_GHOST),
      boss_count=2, mid=[6, 15], band=1,
      routes=[snake([2,4,6], xa=1, xb=30), snake([8,10,12], xa=1, xb=30)],
      kinds=[0,2,8,4,6,7]),                           # 终盘：盾+甲+直升机+奶妈全家桶
]

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
        blocked=blocked_c)
    open("src/config_gen.h", "w").write(txt)

HEADER_TPL = """// SPDX-License-Identifier: MIT
// Copyright (c) 2026 andjiang0083
// config_gen.h -- GENERATED by tools/layout.py. DO NOT EDIT BY HAND.
// 网格 / 关卡 / 塔表 / 敌人表 / 数值 / 调色板唯一真源：tools/layout.py
#pragma once
#include <stdint.h>
#include <stddef.h>

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

#define BTN_X 1064
#define BTN_Y 8
#define BTN_W 200
#define BTN_H 64

#define BUILD_BAR_Y   (SCREEN_H - BUILD_BAR_H)
// v0.3.0：5 塔种 → 槽加宽排满屏幕，右侧留提示列
#define SLOT_W        200
#define SLOT_H        72
#define SLOT_GAP      8
#define SLOT_X0       16

#define PANEL_W       360
#define PANEL_H       248
#define PBTN_UP_X     16
#define PBTN_UP_Y     150
#define PBTN_W        164
#define PBTN_H        72

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
// 压叠窗口（v0.3.2）：本波刷完但残敌未清→ms 后强开下波
#define OVERLAP_MS      %(ovl)d

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

// ── level select screen (v0.4): 5 x 4 node grid（20 关）──
#define SEL_COLS      5
#define SEL_ROWS      4
#define SEL_NODE_W    170
#define SEL_NODE_H    110
#define SEL_GAP_X     35
#define SEL_GAP_Y     26
static inline int selNodeX(int i) { return (SCREEN_W - (SEL_COLS*SEL_NODE_W + (SEL_COLS-1)*SEL_GAP_X)) / 2 + (i %% SEL_COLS) * (SEL_NODE_W + SEL_GAP_X); }
static inline int selNodeY(int i) { return 108 + (i / SEL_COLS) * (SEL_NODE_H + SEL_GAP_Y); }
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


def main():
    os.makedirs("src", exist_ok=True)
    validate()
    gen_header()
    print("wrote src/config_gen.h (%d levels, %d kinds)" % (len(LEVELS), len(KINDS)))
    for lv in LEVELS:
        png("preview-lv%02d.png" % lv["id"], SCREEN_W, SCREEN_H,
            render_board(lv["routes"], lv["theme"]))
        print("  L%-2d %-15s routes=%d waves=%2d diff=%.2f pathcells=%d"
              % (lv["id"], lv["name"], len(lv["routes"]), lv["waves"],
                 lv["diff"], len(path_cells(lv["routes"]))))


if __name__ == "__main__":
    main()
