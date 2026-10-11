# SPEC v0.4.0 — Boss 技能系统 + 无尽模式 + 难度重做

## 背景（小学生实测反馈）
1. 经济发育过好 → 塔越滚越多，中后期无压力
2. 关卡内容短、一波敌人太少（MAX_ENEMIES=24 是硬顶）
3. BOSS 太老实 → 要"会耍坏"的技能，打爆还能吐小兵
4. 目标：**首通必须付出代价（一定重开）**；L10 之后进无尽模式

## 交付定义
- [D1] layout.py：新数值 + AB_* 位掩码扩到 8 个技能 + 每关 BOSS 技能分配
      + mid-boss（一关多王）+ 无尽参数 → config_gen.h（validate() 过，先看 GEN_EXIT §380）
- [D2] game.h：killEnemy() 收口三处击杀结算（子弹/激光/溅射）→ 死亡分裂挂这里；
      ability 引擎：GHOST/MOLT/HORN/GREED/EMP/SPLIT（HEAL/BROOD 保留）；
      Tower.stunMs、Enemy.boostMs/ghostMs/molted/hitCnt 新字段；
      无尽模式：g_endless + lvDiff()/levelWaves() helper 收口所有 LV().waves/.diff 引用
- [D3] render.h：技能视觉前摇/反馈——盾光、EMP 黑烟塔、幽灵半透明棋盘、蜕壳裂纹、
      号角波纹金环、抢钱红字、无尽 HUD ∞
- [D4] actions.h/main.cpp：L10 CLEAR → 点屏进无尽；选关界面 ENDLESS 节点（通关解锁）；
      无尽 GAMEOVER → NVS 记 best wave，显示 BEST WAVE xx
- [D5] gen_sfx.py：HORN（双音上行方波）/ EMP（下滑刺啦）/ SPLIT（短促爆裂），零音频文件
- [D6] sim/run.cpp：每技能至少触发一次的断言 + 无尽冒烟（≥25波不死循环）+
      bot 适配新机制；全量回归通过并把逐波报告贴给用户
- [D7] pio run 编译 + RAM/Flash 占用报告。**不刷机**

## Boss 技能表（AB_ 位掩码，uint16_t）
| bit | 名 | 效果 | 触发规则（全部有前摇/音效预告） |
|-----|----|------|----------------------------------|
| 1   | HEAL（旧）| 自愈 +6% maxHp/2s | 保留现状 |
| 2   | BROOD（旧）| 每4s吐3只SWARM | 保留现状 |
| 4   | SPLIT 💀 | **打爆后**爆出3~4只小兵（GRUNT/SHIELD 随关池），沿同 route 继续跑 | BOSS 死亡瞬间；分裂体计入 alive（末波豁免联动 §381）|
| 8   | HORN 📢 | 全场敌人 +45% 速度持续 3s | abilityMs 节拍（8s）；金色波纹环+哨音 |
| 16  | EMP 🔇 | 最近的 2 座塔哑火 4s（冒黑烟）| abilityMs 节拍（9s）；咝啦声+塔顶红叉 |
| 32  | GREED 💰 | 每受击 8 次偷 $15（封顶当前金 20%）| 受击计数；头顶红字 -$15 |
| 64  | MOLT 🛡 | HP<50% 蜕壳：armor→0、speed×1.4、体积-6px | 一次性；裂纹白闪+壳片飞 |
| 128 | GHOST 🌫 | 隐身 1.5s/每 7s：塔不可锁定，子弹已出膛仍可命中 | 棋盘半透明渲染 |

## 每关 BOSS 分配（一关多王：mid-boss = 半程波，属性 hp×0.45 size-8 gold×0.6）
| L | BOSS | ability | mid-boss（波次） |
|---|------|---------|------------------|
| 1 | BIG GRUNT | —（教学关无技能）| — |
| 2 | BLITZ | HORN | — |
| 3 | BROOD MOTHER | BROOD+SPLIT（死亡爆虫）| — |
| 4 | FORTRESS | MOLT（厚甲蜕壳狂暴）| MINI-FORTRESS @w5 |
| 5 | TWIN ×2 | GREED+HORN（双王轮流偷钱）| MINI-TWIN @w5 |
| 6 | FROST LORD | HEAL+GHOST（奶王隐身）| MINI-FROST @w5 |
| 7 | COLOSSUS | EMP+MOLT（沉默塔+蜕壳）| — |
| 8 | TWIN PRO ×2 | BROOD+SPLIT+HORN | MINI-PRO @w5 |
| 9 | TWIN ELITES ×2 | EMP+GREED+MOLT（哑火+偷钱双坏）| MINI-ELITE @w6 |
| 10| THE END | HEAL+BROOD+SPLIT+EMP（全家桶）| MINI-END @w8 + w12 |
- 无尽：每 5 波 mini-boss、每 10 波真 BOSS，技能随轮次递增（round n = 前 n+1 个技能的组合）

## 难度重做（泄压阀反过来装：这次是收紧+加压）
| 参数 | 旧 | 新 | 理由 |
|------|----|----|------|
| KILL_GOLD_PCT | 150 | **125** | v0.3.2 泄压开太大，经济滚雪球（反馈#1）|
| WAVE_BONUS | 25+6/w | **18+4/w** | 同上，波补也收 |
| START_GOLD | 240 | **210** | 开局少一座塔，前期就要做选择 |
| toSpawn/波 | 4+wave | **6+(wave*3)/2** | 一波太稀（反馈#3）|
| MAX_ENEMIES | 24 | **48** | 硬顶翻倍（DIRAM +3KB，无压力）|
| MAX_BULLETS | 36 | **56** | 怪翻倍后弹道不堵枪口 |
| MAX_FX | 16 | **24** | 溅射/技能特效变多 |
| SHIELD_FROM_WAVE | 6 | **5** | 压力提前一拍 |

## 无尽模式
- L10 CLEAR → "TAP FOR ENDLESS"；选关界面右下加 ENDLESS 节点（L10 通关解锁）
- 地图=TERMINAL；波数无上限（uint8 封顶 250）；diff = LV().diff × (1+6%×round)，
  round = wave/10；兵种池=全兵种
- GAMEOVER：记 NVS "endless"（best wave），结算画面 ENDLESS WAVE xx / BEST yy
- HUD：无尽时波数显示 "∞" 角标（棋盘画法的无穷符号，纯 fillRect）

## bot/仿真适配（§382：标尺要配得上标器）
- bot 已知缺陷修复继承（喘息窗+交通加权）；新增：
  - EMP  awareness：塔被哑火时优先在另一路补位（不做也行，先观察）
  - GHOST：不可锁定期间 bot 不涨塔判断（无需改，自然行为）
- 新断言：每关技能日志 g_abLog[]（game.h 里 ability 触发时置位，仿真读取）：
  L2 HORN / L3 SPLIT+BROOD / L4 MOLT …全 8 技能至少各触发一次
- 无尽冒烟：进无尽、跑到 ≥25 波、无卡死/断言崩溃，记录 bot 倒在哪一波
- **难度验收**：bot 在 L8+ 必须掉命（目标 ≤12 命）；若 bot 仍 20/20 全通 →
  KILL_GOLD_PCT/WAVE_BONUS 再收紧一档（迭代到 bot 见血为止，真机才"一定重开"）

## 风险
- MAX_ENEMIES×MAX_BULLETS 碰撞 O(n·m)=2688/帧：P4@360MHz 无压力，真机盯 FPS
- GHOST+HEAL 组合（L6）可能"打不死"：隐身只 1.5s/7s，且奶王自愈仅对自己——观察仿真
- SPLIT 分裂体在末波：必须计入 alive，否则 ST_CLEAR 提前/卡死（§381 联动）
- GREED 偷钱可能把 bot 偷进金币螺旋（§382）：封顶=当前金 20%，且只在受击时触发
