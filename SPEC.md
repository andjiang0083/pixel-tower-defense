# SPEC v0.2.0 — 10 关卡系统（今晚目标：完成 + 打包，不刷机）

## 交付定义（验收标准）
1. `tools/layout.py`：LEVELS[10] 真源（地图/波数/兵种池/BOSS/diff/theme），生成 config_gen.h + 每关 preview PNG
2. 引擎：多路径（route idx per enemy）、HEALER、ELITE 旗标、BOSS 技能（治疗光环/产卵）、关卡状态机（选关→进关→过关解锁）
3. 进度持久化：Preferences NVS（unlockedMask, stars?→仅通关位图），掉电保留
4. 渲染：选关界面（10 节点+锁态）、每关主题配色、关卡名横幅、BOSS 警告闪
5. PC 仿真：逐关断言（10/10 可通关性 by bot + 关键机制）
6. `pio run -e tab5` SUCCESS；打包 `dist/tab5-td-v0.2.0/`（factory.bin + README）
7. **不刷机**——等用户点头

## 关卡表（数值进 layout.py，此为人类可读版）
| # | name | theme | waves | diff | routes | pool | elite波 | BOSS(名 hp× spd size armor gold leak ability) |
|---|------|-------|-------|------|--------|------|--------|-----|
|1|FIRST BLOOD|grass|5|1.00|直路 r7|(G)|-|BIG GRUNT 6×24 52/0/100/8/-|
|2|SERPENTINE|grass|7|1.05|S三段|(G,R)|-|BLITZ 4.5×68 40/0/110/8/-|
|3|BUG VALLEY|mud|8|1.12|6连弯|(G,R)|-|BROOD MOTHER 7×30 52/2/140/8/BROOD|
|4|IRON CORRIDOR|stone|9|1.20|回字|(G,A)|-|FORTRESS 9×26 56/12(机枪免疫)/170/10/-|
|5|CROSSROADS|grass|9|1.30|十字双路|(G,R,T)|-|TWIN 5×40 ×2只/180/8/-|
|6|FROZEN PASS|ice|10|1.45|直+发夹|(G,R,H)|-|FROST LORD 8×30 56/2/200/10/HEAL|
|7|SPIRAL ABYSS|dark|10|1.60|外旋内|(G,T,A)|8|COLOSSUS 12×24 64/4/230/10/-|
|8|FORKED CANYON|mud|10|1.80|双出生汇合|(G,R,A,S)|7|TWIN PRO 6×45 ×2/250/8/BROOD(各一)|
|9|MEAT GRINDER|stone|12|2.05|8折绞肉|(G,R,T,A,S)|6|TWIN ELITES 7×50 ×2/280/10/-|
|10|TERMINAL|dark|15|2.30|双螺旋最长|(全)|4|THE END 16×22 72/8/350/12|HEAL+BROOD|

- 血量 = kind.hp ×40 × diff × HP_GROWTH^(wave-1)；末波=BOSS 波（清场前不出 READY）
- HEALER：每 HEAL_MS=1.6s 给半径 90px 内友军 +maxHp×12%；ELITE：hp×2.5 gold+8 金边
- BOSS HEAL=半径110 +maxHp×8%/2s；BROOD=每4s吐3只SWARM
- 赏金：gold×(1+0.02·level)；漏怪扣血按 kind.leak_dmg
- 双路径：enemy.routeIdx，route[]各自航点表；出口共用左缘

## 任务拆分（顺序执行，每步验证后进下一步）
- [T1] layout.py 重写：LEVELS+KINDS(+HEALER)+主题PAL → config_gen.h + preview-lv1..10.png（校验：python 跑通+assert 轴对齐/格数）
- [T2] game.h：多路径数组、routeIdx、HEALER/ELITE/BOSS技能 tick、关卡状态（lvSelect/play/clear）、startLevel()
- [T3] render.h：主题换肤（grass/mud/stone/ice/dark）、选关界面、BOSS警告、关卡横幅
- [T4] actions.h+touch：选关节点命中→进关；clear 界面点击→回选关
- [T5] main.cpp：Preferences 存档(unlockedMask)、setup/loop 状态分派
- [T6] sim/run.cpp：逐关 bot（复用 v0.1.2 贪心消费），断言 L1-L4 必过、L5+ 允许失败但记录；HEALER/ELITE/BROOD/双路机制断言
- [T7] pio 编译 + dist 打包（factory.bin、checksum、CHANGELOG）+ shots/*.png
- [T8] README 更新 + git commit（本地），经验沉淀 §

## 风险
- flash/RAM：多路径表 ×10 ≈ +2KB rodata，无压力
- 仿真时长：10关×~9波 bot ≈ 数分钟，可接受
- NVS 首次初始化失败→内存兜底不崩
