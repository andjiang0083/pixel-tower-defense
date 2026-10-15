/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 andjiang0083
 */
// game.h — 游戏数据与逻辑更新（不碰任何绘制 API，模块分离）
// v0.2.0：10 关卡系统——多路径（双出生点）、主题由 layout.py 下发、
//         HEALER/ELITE 新敌、BOSS 技能（治疗光环/产卵）、关卡解锁进度。
//         全部固定数组，零 malloc。数值真源：tools/layout.py → config_gen.h
// ⚠️ 单编译单元约定（v0.4.1 立字为据）：本仓库所有游戏状态都是头文件里的
//    static 对象（g_w / 音效队列 / render 缓存 / g_buildFx…），platformio.ini
//    的 build_src_filter 目前只编 main.cpp 一个 TU。**新增 src/*.cpp 前必须
//    读这条**：否则新 TU 会各带一份静默副本，状态分裂且无链接报错。
//    要拆多文件，先把这些 static 搬进唯一的 game.cpp 再谈。
#pragma once
// millis()：唯一需要的 Arduino 设施。
// v0.5.0（Claw4 移植）：改为条件包含——Arduino 侧走 Arduino.h，
// 其他平台（ESP-IDF / PC 仿真）由各自的 shim 提供同一个 millis()。
//   · ESP-IDF 侧：main/bsp_claw4.h 声明 millis()，实现是 esp_timer_get_time()
//   · PC 仿真侧：sim/compat/Arduino.h 里的 g_simMs 递增版
// 逻辑层对"时间从哪来"零感知，这是原作者"游戏逻辑不碰硬件"的直接延伸。
#ifdef ARDUINO
  #include <Arduino.h>
#else
  // 非 Arduino 平台由 BSP 提供 millis()。声明放在这里会让每个 TU 都看到它，
  // 好处是 game.h 自包含；代价是 BSP 的定义必须匹配签名(uint32_t(void))。
  // ESP-IDF 侧 bsp_claw4.cpp 的 millis() 定义为 extern "C" uint32_t millis(void)，
  // C++ 链接约定下与本声明一致，故不冲突。
  uint32_t millis(void);
#endif
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "config_gen.h"
#include "sfx.h"   // v0.3.1：音效事件投递（只 post，不碰音频 API）
// ── 游戏状态机 ─────────────────────────────────────────────────────
enum GameState : uint8_t {
  ST_TITLE = 0,  // v0.4.2：标题界面（开机第一屏：标题/版本/语言选择）
  ST_SEL,        // 关卡选择界面
  ST_READY,      // 波次间隙：等玩家点 NEXT WAVE
  ST_WAVE,       // 波次进行中（还在刷怪或场上仍有敌人）
  ST_CLEAR,      // 本关通关：点屏幕回选关
  ST_GAMEOVER,   // 生命归零：点屏幕任意处重开本关
};

// ── 实体结构体 ─────────────────────────────────────────────────────
struct Enemy {
  bool    active;
  uint8_t kind;        // ENEMY_KINDS[] 下标
  bool    elite;       // ELITE：血量×ELITE_HP_MULT，金边渲染
  bool    flying;      // v0.3.0：空中单位——直线飞向终点，无视路径格
  bool    shield;      // v0.3.2：弹盾——子弹直击×0.5；激光/溅射全伤
  uint8_t route;       // 行进路线索引（双路径关卡 0/1）
  uint8_t size;        // 渲染尺寸 px（BOSS 逐关不同，不查表）
  uint8_t armor;       // 单体伤害减免（BOSS 逐关不同）
  int16_t gold;        // 击杀赏金（BOSS/ELITE 逐关不同）
  float   x, y;        // 像素坐标（中心；飞行时为空中视觉位）
  float   hp, maxHp;
  int16_t wp;          // 当前航点索引（目标 = routeWP[wp+1]）
  uint16_t speed;      // 基础速度 px/秒（不含减速）
  int8_t   flash;      // 受击闪白帧数（>0 画白色）
  uint16_t slowMs;     // 冰霜减速剩余毫秒（>0 时按 FROST_SLOW_PCT%）
  uint32_t abilityMs;  // HEALER/BOSS 技能节拍计时器（BOSS=最早到期技能位，见 abNextMs）
  // v0.4.1：每个 BOSS 主动技能位独立倒计时（0=HEAL 1=BROOD 2=SPLIT 3=HORN
  //   4=EMP 5=GREED 6=MOLT 7=GHOST 8=SIEGE；未设置位=0xFFFFFFFF 永不到期）。
  //   旧实现只有一个 abilityMs：到期后所有技能同帧齐放、节拍被 if 链最后
  //   一个常量劫持（L6 治疗变 7s/次、L10 五技能同 tick）——见 FIXLOG-v0.4.1。
  uint32_t abNextMs[9];   // v0.4.2：扩到 9 位（新增 bit8 = SIEGE 封锁建造区）
  uint8_t  leakDmg;   // 漏怪扣血（BOSS/ELITE 逐关覆盖，不查兵种表）
  // ── v0.4 BOSS 技能状态（memset 清零 = 全默认关，安全）──
  uint16_t ghostMs;   // 🌫 >0 = 隐身中（塔不可锁定）
  uint16_t boostMs;   // 📢 >0 = 号角加速中（speed×(1+HORN_BOOST_PCT%)）
  uint8_t  hitCnt;    // 💰 GREED：受击计数（每 GREED_HITS 次偷金）
  uint8_t  bossMini;  // 1 = mid-boss（缩小版王，渲染小旗）
  uint8_t  molted;    // 🛡 1 = 已蜕壳（一次性）
};

// ── 炮塔瞄准：8 方向步进（像素美术惯例，避免斜线锯齿）────────────
// 真源唯一：game.h 开火时用它把子弹放在枪口，render.h 画枪管/火光
static const int8_t TURRET_DIR[8][2] = {
  {5,0},{4,-4},{0,-5},{-4,-4},{-5,0},{-4,4},{0,5},{4,4}
};

// 屏幕坐标 y 向下；octant 0=E,2=N,4=W,6=S
static inline int turretOctant(float dx, float dy) {
  const float ax = fabsf(dx), ay = fabsf(dy);
  if (ax > ay * 2.4f) return dx > 0 ? 0 : 4;
  if (ay > ax * 2.4f) return dy < 0 ? 2 : 6;
  if (dx >= 0 && dy <= 0) return 1;
  if (dx <  0 && dy <= 0) return 3;
  if (dx >= 0 && dy >  0) return 7;
  return 5;
}

struct Tower {
  bool    active;
  int8_t  col, row;    // 所在格子（一格一塔）
  uint8_t type;        // TOWER_TYPES[] 下标（0=机枪 1=加农 2=冰霜 3=能量 4=激光）
  uint8_t level;       // 1..TOWER_MAX_LEVEL
  int32_t invested;    // 累计投入金币（卖出回收基数）
  uint16_t cooldownMs;
  int8_t   recoil;     // 开火后坐动画帧数
  float    adx, ady;   // 瞄准单位向量（360°）
  int8_t   aoct;       // 瞄准八分位（渲染枪管方向）
  float    bx, by;     // v0.3.0：激光光束终点（beamMs>0 期间渲染）
  uint8_t  beamMs;     // v0.3.0：激光光束残留毫秒（纯视觉）
  uint16_t stunMs;     // v0.4 🔇 EMP：>0 = 哑火中（不索敌不开火，冒黑烟）
};

struct Bullet {
  bool    active;
  float   x, y;
  // ⚠️ 必须 float：速度最高 560 px/s，整型会溢出/截断（真机 bug #1）
  float   vx, vy;      // 方向 * proj_speed（px/秒）
  float   spawnDist2;  // 开火瞬间目标平方距离（仿真断言用）
  uint16_t damage;     // 本发伤害（含塔等级加成，发射时结算好）
  uint8_t splashR;     // >0 = 炮弹：落点方形溅射半径（无视护甲）
};

struct Fx {            // 极简命中特效：扩散空心方框，无粒子系统
  bool    active;
  float   x, y;
  uint8_t lifeFrames;
  uint8_t kind;        // 0=白(命中) 1=橙(爆炸/溅射)
};

// ── 世界状态（全局单例）──────────────────────────────────────────
struct World {
  Enemy  enemies[MAX_ENEMIES];
  Tower  towers[MAX_TOWERS];
  Bullet bullets[MAX_BULLETS];
  Fx     fx[MAX_FX];

  bool    path[COLS][MAP_ROWS];       // true = 路径格，禁止建塔
  bool    occupied[COLS][MAP_ROWS];   // true = 已有塔
  bool    blocked[COLS][MAP_ROWS];    // v0.4：禁建岩层（BLOCKED 掩码展开）

  int32_t gold;
  int32_t lives;
  int16_t wave;
  GameState state;

  // 本波刷怪进度
  int16_t toSpawn, spawnedOfWave;
  uint32_t nextSpawnMs;
  int8_t   swarmLeft;    // 本波剩余虫群连发数（SWARM_BURST）
  bool     bossPending;  // BOSS 波：压轴还没出
  bool     miniPending, miniSpawned;   // v0.4 mid-boss：本波有mini王/已出

  int32_t selectedTower; // -1=无；否则 towers[] 下标（升级/卖出面板）
  int32_t buildSel;      // 建造栏选中塔种，-1=未选（默认机枪）

  bool    hudDirty;                    // HUD 数字脏标记
  bool    dirty[COLS][MAP_ROWS];     // 实体覆盖过的格子，下帧擦回底色

  int32_t score;                     // 击杀数
  uint8_t leakCount;               // v0.3.0：本关漏怪数（结算面板展示）

  uint32_t readyLeftMs;              // v0.2.1：波间倒计时（ST_READY 且 wave>0 时递减）

  // ── v0.4 无尽模式状态（levelLoad memset 清零 = 正常关）──
  uint8_t  endless;                  // 1 = 无尽（TERMINAL 图无限刷）

  // ── v0.4.2 SIEGE：BOSS 封锁建造区（全局一个即可，同帧只可能一个王在放）──
  uint16_t siegeMs;                  // >0 = 封锁生效中
  float    siegeX, siegeY;           // 封锁中心（BOSS 位置）
  int16_t  siegeR;                   // 封锁半径 px
};

static World g_w;

// ── 关卡进度（解锁位图；main.cpp 负责 NVS 持久化）───────────────
static uint32_t g_unlocked = 0x0001;   // bit i = 第 i+1 关可选（v0.4：20 关→uint32）
static uint32_t g_cleared  = 0;        // bit i = 第 i+1 关已通关
static uint8_t  g_stars[LEVEL_COUNT];  // v0.3.0：每关历史最高星级 0..3
static int8_t   g_lv = 0;              // 当前关卡下标 0..LEVEL_COUNT-1
// v0.4.2：无尽模式历史最高波次（NVS "endbest"；HUD 显示，死亡时结算）
static int16_t  g_endBest = 0;

static inline const LevelDef& LV(void) { return LEVELS[g_lv]; }
// ── v0.4：无尽模式下 waves/diff 不再是常量——一切难度读数走这两个 helper ──
static inline int lvWaves(void) { return g_w.endless ? ENDLESS_WAVES : LV().waves; }
static inline float lvDiff(void) {
  if (!g_w.endless) return LV().diff;
  const int round = g_w.wave / ENDLESS_ROUND_WAVES;      // 每10波一轮
  return LV().diff * (1.0f + ENDLESS_DIFF_PCT / 100.0f * round);
}
// v0.4：本波该出什么王——mid-boss（半程）/真 BOSS（末波）。无尽：每5波mini、每轮末真王
struct BossPlan { uint8_t real; uint8_t mini; };           // real=真王只数, mini=mini王只数
static inline BossPlan bossForWave(void) {
  BossPlan p{0, 0};
  if (g_w.endless) {
    const int wr = g_w.wave % ENDLESS_ROUND_WAVES;         // 0=整轮末
    if (wr == 0)                p.real = 1;                  // 轮末真王
    else if (g_w.wave % ENDLESS_MINI_EVERY == 0) p.mini = 1;
    return p;
  }
  if (g_w.wave >= LV().waves)   p.real = LV().boss_count;
  else if (LV().mid[0] == g_w.wave || LV().mid[1] == g_w.wave) p.mini = 1;
  return p;
}
// v0.4：技能触发日志（每关重开清零；仿真断言"每个技能至少响一次"）
static uint16_t g_abLog = 0;
// v0.4.2：机枪出膛音色轮换位（0/1/2 → GUN/GUN2/GUN3，见开火处）
static uint8_t s_gunVariant = 0;
static inline bool levelUnlocked(int i) { return (g_unlocked >> i) & 1; }
static inline bool levelCleared(int i) { return (g_cleared >> i) & 1; }

// v0.3.0：按剩余生命比例评星（通关瞬间结算一次）
static inline uint8_t starsFor(int32_t lives) {
  if (lives * 100 >= START_LIVES * STAR3_PCT) return 3;
  if (lives * 100 >= START_LIVES * STAR2_PCT) return 2;
  return 1;
}

// v0.3.0：空中单位的"飞行甲板"——视觉抬高 18px（贴地阴影分开画）
#define FLY_LIFT 18

// ── route 访问（ALL_WP 打包：哨兵 (count,0) + count 个点）────────
static inline int routeStart(int r) {
  // 线性扫过本关 route——数量<=2，成本可忽略
  int off = LV().wp_start;
  for (int k = 0; k < r; ++k) off += 1 + ALL_WP[off].col;
  return off;
}
static inline int routeCount(int r) { return ALL_WP[routeStart(r)].col; }
static inline const Waypoint& routeWP(int r, int i) { return ALL_WP[routeStart(r) + 1 + i]; }
static inline void routeWPPx(int r, int i, float* x, float* y) {
  const Waypoint& w = ALL_WP[routeStart(r) + 1 + i];
  *x = w.col * CELL + CELL / 2.0f;
  *y = MAP_TOP + w.row * CELL + CELL / 2.0f;
}

// ── 塔属性换算（等级缩放的唯一入口）──────────────────────────────
static inline float towerRange(const Tower& t) {
  return TOWER_TYPES[t.type].range_px * (1.0f + TOWER_RANGE_SCALE * (t.level - 1));
}
static inline int towerDamage(const Tower& t) {
  float d = TOWER_TYPES[t.type].damage;
  for (uint8_t l = 1; l < t.level; ++l) d *= TOWER_DMG_SCALE;
  return (int)(d + 0.5f);
}
static inline uint16_t towerFireMs(const Tower& t) {
  // 每升一级射速快一点：x0.85（整数毫秒）
  int ms = TOWER_TYPES[t.type].fire_ms;
  for (uint8_t l = 1; l < t.level; ++l) ms = (ms * 85 + 50) / 100;
  return (uint16_t)(ms < 90 ? 90 : ms);
}

// ── 路径展开（与 tools/layout.py 同源规则；多 route 全部标记）────
static void buildPath(void) {
  memset(g_w.path, 0, sizeof(g_w.path));
  const int nr = LV().routes_n;
  for (int r = 0; r < nr; ++r) {
    const int cnt = routeCount(r);
    for (int i = 0; i < cnt - 1; ++i) {
      const Waypoint& a = ALL_WP[routeStart(r) + 1 + i];
      const Waypoint& b = ALL_WP[routeStart(r) + 2 + i];
      if (a.col != b.col && a.row != b.row) continue;   // 只支持轴对齐段
      if (a.row == b.row) {
        int lo = a.col < b.col ? a.col : b.col, hi = a.col < b.col ? b.col : a.col;
        for (int c = lo; c <= hi; ++c)
          if ((unsigned)c < COLS && (unsigned)a.row < MAP_ROWS) g_w.path[c][a.row] = true;
      } else {
        int lo = a.row < b.row ? a.row : b.row, hi = a.row < b.row ? b.row : a.row;
        for (int rr = lo; rr <= hi; ++rr)
          if ((unsigned)a.col < COLS && (unsigned)rr < MAP_ROWS) g_w.path[a.col][rr] = true;
      }
    }
  }
}

static inline void markDirtyPx(float x, float y, int half) {
  int c0 = ((int)(x - half)) / CELL, c1 = ((int)(x + half)) / CELL;
  int r0 = ((int)(y - half - MAP_TOP)) / CELL, r1 = ((int)(y + half - MAP_TOP)) / CELL;
  if (c0 < 0) c0 = 0; if (c1 > COLS - 1) c1 = COLS - 1;
  if (r0 < 0) r0 = 0; if (r1 > MAP_ROWS - 1) r1 = MAP_ROWS - 1;
  for (int c = c0; c <= c1; ++c) for (int r = r0; r <= r1; ++r) g_w.dirty[c][r] = true;
}

// v0.4：前向声明——BOSS 技能函数（定义在建造/卖出段之后，共用 addFx/splash）
static void addFx(float x, float y, uint8_t kind);
static inline uint16_t bossAbilityOf(const Enemy& e);   // v0.4.1：spawnKind 错峰起手要用（v0.4.2 升 16 位：第 9 技能位 SIEGE）

// ── 装载一关（选关/重开共用）：清场 + 铺路，回到 ST_READY ────────
static void levelLoad(int idx) {
  if (idx < 0 || idx >= LEVEL_COUNT) return;
  g_lv = (int8_t)idx;
  memset(&g_w, 0, sizeof(g_w));
  g_abLog = 0;                        // v0.4：技能触发日志随关重置
  g_w.gold = START_GOLD;
  g_w.lives = START_LIVES;
  g_w.state = ST_READY;
  g_w.selectedTower = -1;
  g_w.buildSel = -1;              // 未选 = 默认机枪（老玩家肌肉记忆）
  g_w.hudDirty = true;
  buildPath();
  // v0.4：禁建岩层掩码展开（单一真源 BLOCKED[关卡][行]，bit c = 不可建）
  for (int r = 0; r < MAP_ROWS; ++r)
    for (int c = 0; c < COLS; ++c)
      g_w.blocked[c][r] = (BLOCKED[g_lv][r] >> c) & 1;
}

// ── 开启下一波：数量增长 + 编成规则（真源数值见 config_gen.h）───
// v0.4.1：波量公式抽成唯一 helper——旧实现在 startNextWave 与 spawnEnemy
// （mini 王出场阈值）各写一遍，改一处忘另一处会静默漂移。
static inline int waveSize(int w) { return 6 + (w * 3) / 2; }

static void startNextWave(void) {
  if (g_w.state != ST_READY || g_lv < 0) return;   // ⚠️ ST_CLEAR/游戏结束都不响应
  ++g_w.wave;
  // v0.4：一波太稀（小学生实测）——数量 4+wave → 6+(wave*3)/2，压到 MAX_ENEMIES-1
  g_w.toSpawn = waveSize(g_w.wave);
  if (g_w.toSpawn > MAX_ENEMIES - 1) g_w.toSpawn = MAX_ENEMIES - 1; // BOSS/虫群留槽位
  g_w.spawnedOfWave = 0;
  g_w.nextSpawnMs   = millis();
  // 虫群：兵种池里有 SWARM 且 wave>=2 → 本波含一组连发
  bool hasSwarm = false;
  for (int k = 0; k < 6 && LV().kinds[k] >= 0; ++k)
    if (LV().kinds[k] == K_SWARM) hasSwarm = true;
  g_w.swarmLeft = (hasSwarm && g_w.wave >= 2) ? SWARM_BURST : 0;
  if (g_w.swarmLeft > g_w.toSpawn) g_w.swarmLeft = g_w.toSpawn;
  // v0.4：王波计划——真 BOSS（末波/无尽轮末）+ mid-boss（半程，一关多王）
  const BossPlan bp = bossForWave();
  g_w.bossPending   = (bp.real > 0);       // 真王压轴（先出场领跑）
  g_w.miniPending   = (bp.mini > 0);       // mini王：混在兵潮中段出
  g_w.miniSpawned   = false;
  g_w.state         = ST_WAVE;
  g_w.hudDirty      = true;
}

// ── BOSS/普通怪入场填充（kind 已定）──────────────────────────────
// v0.4：mini=true → mid-boss（缩小版王：hp×MID_HP_PCT%、size-8、gold×0.6，带技能）
static void spawnKind(int i, int kind, uint8_t route, bool elite, bool mini = false) {
  const EnemyKind& k = ENEMY_KINDS[kind];
  // 血量复利增长：HP_GROWTH^(wave-1) × 关卡难度 diff。连乘而非 powf（热路径避免 libm）。
  float hpScale = k.hp_scale * lvDiff();
  for (int w = 1; w < g_w.wave; ++w) hpScale *= HP_GROWTH;

  Enemy& e = g_w.enemies[i];
  memset(&e, 0, sizeof(e));
  e.active = true;
  e.kind   = (uint8_t)kind;
  e.elite  = elite && kind != K_BOSS;       // BOSS 本身不再叠 ELITE
  e.flying = k.flying != 0;                  // v0.3.0：直升机走直线
  e.shield = k.shield != 0;                  // v0.3.2：弹盾兵
  e.route  = route;
  e.wp     = 0;
  routeWPPx(route, 0, &e.x, &e.y);           // 该 route 第 0 航点（右边缘）
  e.x += CELL / 2.0f;                         // 屏幕外走进来
  if (e.flying) e.y -= FLY_LIFT;            // v0.3.0：空中单位视觉抬高

  if (kind == K_BOSS) {                      // 逐关 BOSS：属性覆盖兵种表
    const LevelDef& L = LV();
    float bs = L.boss_hp_mult * lvDiff();
    for (int w = 1; w < g_w.wave; ++w) bs *= HP_GROWTH * 0.5f + 0.5f; // BOSS 增速放缓
    if (mini) bs *= MID_HP_PCT / 100.0f;   // v0.4 mid-boss：半程小王
    e.maxHp   = (float)HP_BASE * bs;
    e.hp      = e.maxHp;                    // ⚠️ 必须显式赋值（memset 清零后 v0.2.0 曾漏掉→出生 0 血一碰就死）
    e.speed   = (uint16_t)L.boss_speed;
    e.size    = mini ? (uint8_t)(L.boss_size - MID_SIZE_DROP) : L.boss_size;
    e.armor   = L.boss_armor;
    e.gold    = mini ? (int16_t)(L.boss_gold * MID_GOLD_PCT / 100) : L.boss_gold;
    e.leakDmg = mini ? (uint8_t)(L.boss_leak / 2 + 1) : L.boss_leak;  // v0.4 mini王漏怪减半
    e.bossMini = mini ? 1 : 0;              // v0.4：渲染小旗区分真王/小王
    if (mini) e.speed = (uint16_t)(L.boss_speed * 1.2f);   // 小王跑快点，别当木桩
    // v0.4.1：各主动技能位错峰起手（+1000ms 起，每位再 +350ms）——
    // 旧实现全部技能共享一个起手时间，配合"到期齐放"=开场技能风暴。
    for (int i = 0; i < 9; ++i) e.abNextMs[i] = 0xFFFFFFFFu;   // 未设置位永不到期
    {
      const uint16_t ab0 = bossAbilityOf(e);   // mini 王只带第一个（预告演出）
                                              // ⚠️ 必须 16 位：L11 ability=268，
                                              // uint8_t 会把 SIEGE(bit8) 截掉 → 王永不放封锁
      for (int i = 0; i < 9; ++i)
        if (ab0 & (1u << i)) e.abNextMs[i] = millis() + 1000 + (uint32_t)i * 350;
    }
    e.abilityMs = millis() + 1000;          // 门控初值（随后=最早到期技能位）
  } else {
    if (e.elite) hpScale *= ELITE_HP_MULT;
    e.maxHp = (float)HP_BASE * hpScale;
    e.hp    = e.maxHp;                      // ⚠️ 同上：memset 清零后必须显式满血（真机 bug #4）
    e.speed = k.speed_pps;
    e.size  = (uint8_t)k.size_px;
    e.armor = k.armor_flat;
    e.gold  = (int16_t)(k.gold + (e.elite ? ELITE_GOLD_ADD : 0));
    e.leakDmg = k.leak_dmg;
  }
  markDirtyPx(e.x, e.y, e.size);
}

// ── BOSS 产卵：原地吐 SWARM（沿同 route 继续前进）────────────────
static void broodSpawn(Enemy& boss) {
  for (int n = 0; n < BOSS_BROOD_SIZE; ++n) {
    for (int i = 0; i < MAX_ENEMIES; ++i) {
      if (g_w.enemies[i].active) continue;
      Enemy& e = g_w.enemies[i];
      memset(&e, 0, sizeof(e));
      const EnemyKind& k = ENEMY_KINDS[K_SWARM];
      e.active = true; e.kind = K_SWARM;
      e.route = boss.route; e.wp = boss.wp;
      e.x = boss.x + (n - 1) * 14.0f; e.y = boss.y;
      float hpScale = k.hp_scale * lvDiff();
      for (int w = 1; w < g_w.wave; ++w) hpScale *= HP_GROWTH;
      e.maxHp = (float)HP_BASE * hpScale; e.hp = e.maxHp;
      e.speed = k.speed_pps; e.size = (uint8_t)k.size_px;
      e.armor = k.armor_flat; e.gold = (int16_t)k.gold;
      e.leakDmg = k.leak_dmg;
      markDirtyPx(e.x, e.y, e.size);
      break;
    }
  }
}

// ── v0.4 💀 SPLIT：王死瞬间爆出小兵（沿同 route 继续跑，计入 alive）──
// ⚠️ §381：分裂体必须走正常敌人通道（active+route）——末波"全灭才 CLEAR"
//    会把它们算进去；绝不能绕过 alive 统计，否则提前通关/卡死。
static void splitDeath(Enemy& boss) {
  g_abLog |= AB_SPLIT;
  sfxPost(SFXID_BOOM);                     // 爆裂声（队列节流）
  addFx(boss.x, boss.y, 1);                // 橙色爆框
  int n = SPLIT_SIZE + g_w.wave / 8;      // 波次越高爆得越多
  if (n > 5) n = 5;
  // 兵种随关池：优先弹盾兵（恶心加倍），否则 GRUNT
  int kind = K_SHIELD;
  bool poolHasShield = false;
  for (int k = 0; k < 6 && LV().kinds[k] >= 0; ++k)
    if (LV().kinds[k] == K_SHIELD) poolHasShield = true;
  if (!poolHasShield || g_w.wave < SHIELD_FROM_WAVE) kind = 0;
  for (int m = 0; m < n; ++m) {
    for (int i = 0; i < MAX_ENEMIES; ++i) {
      if (g_w.enemies[i].active) continue;
      Enemy& e = g_w.enemies[i];
      memset(&e, 0, sizeof(e));
      const EnemyKind& k = ENEMY_KINDS[kind];
      e.active = true; e.kind = (uint8_t)kind;
      e.route = boss.route; e.wp = boss.wp;   // 从尸体位置继续往前跑
      routeWPPx(boss.route, boss.wp + 1, &e.x, &e.y);
      float t = (m - n / 2) * 0.34f; if (t > 1) t = 1; if (t < -1) t = -1;
      e.x += t * 26.0f; e.y += t * 8.0f;
      float hpScale = k.hp_scale * lvDiff() * 0.75f;   // 分裂体×0.75：数量换质量
      for (int w = 1; w < g_w.wave; ++w) hpScale *= HP_GROWTH;
      e.maxHp = (float)HP_BASE * hpScale; e.hp = e.maxHp;
      e.speed  = (uint16_t)(k.speed_pps * 1.15f);       // 复仇小跑
      e.size   = (uint8_t)k.size_px;
      e.armor  = k.armor_flat; e.shield = k.shield != 0;
      e.gold   = (int16_t)k.gold;
      e.leakDmg = k.leak_dmg;
      markDirtyPx(e.x, e.y, e.size);
      break;
    }
  }
}

// ── v0.4 📢 HORN：全场敌人加速（金色波纹环+哨音）────────────────
static void hornBlast(Enemy& boss) {
  g_abLog |= AB_HORN;
  sfxPost(SFXID_HORN);
  addFx(boss.x, boss.y, 4);               // kind=4 = 号角金环（render）
  for (int i = 0; i < MAX_ENEMIES; ++i) {
    Enemy& e = g_w.enemies[i];
    if (!e.active) continue;
    e.boostMs = HORN_DUR_MS;
  }
}

// ── v0.4 🔇 EMP：哑火最近的 N 座塔（黑烟+刺啦声）─────────────────
static void empBlast(Enemy& boss) {
  g_abLog |= AB_EMP;
  sfxPost(SFXID_EMP);
  addFx(boss.x, boss.y, 5);               // kind=5 = EMP 紫环（render）
  const float bx = boss.x, by = boss.y;
  for (int n = 0; n < EMP_TOWERS; ++n) {
    int best = -1; float bestD2 = 1e30f;
    for (int i = 0; i < MAX_TOWERS; ++i) {
      Tower& t = g_w.towers[i];
      if (!t.active || t.stunMs > 0) continue;         // 已哑的不重复罚
      float dx = t.col*CELL+CELL/2 - bx, dy = MAP_TOP+t.row*CELL+CELL/2 - by;
      float d2 = dx*dx + dy*dy;
      if (d2 < bestD2) { bestD2 = d2; best = i; }
    }
    if (best < 0) break;
    Tower& t = g_w.towers[best];
    t.stunMs = EMP_DUR_MS;
    markDirtyPx(t.col*CELL+CELL/2, MAP_TOP+t.row*CELL+CELL/2, CELL);
  }
}

// ── v0.4 💰 GREED：受击计数，每 GREED_HITS 次偷金（封顶当前金%）──
static void greedHit(Enemy& boss) {
  if (++boss.hitCnt < GREED_HITS) return;
  boss.hitCnt = 0;
  int amt = GREED_AMT;
  const int cap = (int)(g_w.gold * GREED_CAP_PCT / 100);
  if (amt > cap) amt = cap;
  if (amt <= 0) return;
  g_w.gold -= amt;
  if (g_w.gold < 0) g_w.gold = 0;
  g_abLog |= AB_GREED;
  sfxPost(SFXID_COIN);                    // 偷钱也用叮咚（金币语义）
  addFx(boss.x, boss.y - (int)boss.size / 2 - 6, 6);   // kind=6 = -$红字（render）
  g_w.hudDirty = true;
}

// ── v0.4 🛡 MOLT：半血蜕壳（甲归零、+速度、体积缩小）─────────────
static void moltTrigger(Enemy& boss) {
  boss.molted = 1;
  g_abLog |= AB_MOLT;
  sfxPost(SFXID_BOOM);                    // 壳裂=闷响
  boss.armor = 0;                         // 蜕掉的壳留在原地当视觉，本体变小变快
  boss.speed = (uint16_t)(boss.speed * (100 + MOLT_SPD_PCT) / 100);
  if (boss.size > MID_SIZE_DROP + 8) boss.size -= MOLT_SIZE_DROP;
  addFx(boss.x, boss.y, 7);             // kind=7 = 蜕壳裂纹白闪（render）
}

// ── v0.4 🌫 GHOST：周期隐身（塔不可锁定；已出膛子弹照打）──────────
static void ghostTrigger(Enemy& boss) {
  g_abLog |= AB_GHOST;
  sfxPost(SFXID_FROST);                   // 幽灵=冰霜叮铃变体（够用，不加新音色）
  boss.ghostMs = GHOST_DUR_MS;
}

// ── v0.4.2 🚧 SIEGE：以王为中心封锁建造区数秒（圈内建塔被拒）───────
// 空间型技能：逼玩家在王的推进路线上提前布阵，而不是等它贴脸再补塔。
// 全局单实例（同帧只可能一个王在放；多王关轮流放也自然错峰）。
static void siegeTrigger(Enemy& boss) {
  g_abLog |= AB_SIEGE;
  sfxPost(SFXID_EMP);                     // 封锁=EMP 变体（低频事件，音色够用）
  g_w.siegeMs = SIEGE_MS;
  g_w.siegeX = boss.x; g_w.siegeY = boss.y;
  g_w.siegeR = SIEGE_R;
  addFx(boss.x, boss.y, 8);               // kind=8 = 封锁红框（render）
  g_w.hudDirty = true;                    // HUD 警示条刷新
}

// 某像素点是否处于封锁区（buildTower / 渲染共用）
static inline bool siegeBlocksPx(float px, float py) {
  if (g_w.siegeMs == 0) return false;
  const float dx = px - g_w.siegeX, dy = py - g_w.siegeY;
  return dx * dx + dy * dy <= (float)g_w.siegeR * g_w.siegeR;
}

// ── v0.4 受击被动统一入口：💰GREED 偷金 / 🛡MOLT 半血蜕壳 ──────────
static void bossHurt(Enemy& e) {
  if (e.kind != K_BOSS || !e.active) return;
  const uint16_t ab = LV().ability;      // 被动技：mini/真王都按关表（GREED/MOLT
                                          // 是"受击才触发"，不构成兵潮风暴）
  if (ab & AB_GREED) greedHit(e);
  // ⚠️ MOLT 只在"还活着且真掉血"时蜕：死了就别再变异了
  if ((ab & AB_MOLT) && !e.molted && e.hp > 0 && e.hp * 100 < e.maxHp * MOLT_PCT)
    moltTrigger(e);
}

// ── v0.4：mini 王只带**一个"一次性展示型"技能**——半程小王是预告演出。
//    ⚠️ 绝不给 mini 续航/产兵技（HEAL/BROOD）：仿真实测 L8 w5 mini 带
//    BROOD=半程产卵工厂，叠加压叠→同屏26只、bot 11塔饿死（难度失控）。
//    优先级：号角/隐身/蜕壳/EMP/分裂（看得见、会结束）> 产卵/自愈。
static inline uint16_t bossAbilityOf(const Enemy& e) {
  const uint16_t ab = LV().ability;   // ⚠️ 必须 16 位：L11 ability=268（含 SIEGE bit8）
  if (!e.bossMini) return ab;                    // 真王：全套
  static const uint8_t PRIO[5] = { AB_HORN, AB_GHOST, AB_MOLT, AB_EMP, AB_SPLIT };
  for (int i = 0; i < 5; ++i) if (ab & PRIO[i]) return PRIO[i];
  return 0;                                      // 只剩续航技→mini 裸奔（宁可不给）
}

// ── v0.4 击杀结算唯一收口：金币/计分/SPLIT。三处死亡路径全走这里 ──
static void killEnemy(Enemy& e) {
  if (!e.active) return;                  // ⚠️ 防 SPLIT/溅射同帧双杀重复发钱
  e.active = false;
  g_w.gold += (long)e.gold * KILL_GOLD_PCT / 100;
  ++g_w.score; g_w.hudDirty = true;
  if (e.kind == K_BOSS && (bossAbilityOf(e) & AB_SPLIT)) splitDeath(e);  // 💀
}

// ── v0.4.1 BOSS 技能节拍：每个主动技能位各走各的表（abNextMs[8]）────
// 旧实现（v0.4.1 前）只有一个 abilityMs：到期后所有置位技能同一帧齐放，
// 且 next 取 if 链里最后一个技能的常量——GHOST 排在最后永远赢。
// 实测扭曲：L6(HEAL+GHOST) 治疗变 7s/次（标称 2s）；L10 五技能同 tick、
// 全场节拍被 EMP 的 9s 劫持；L13/L16/L19/L20 的产卵/自愈全部变慢。
// 现在：到期哪个放哪个；abilityMs 只当"最早到期位"的门控复用。
// mini 王节拍×2 保留（半程小王是预告演出，不能比真王还吵）。
static void bossAbilities(Enemy& e, uint32_t now) {
  const uint16_t ab = bossAbilityOf(e);   // v0.4：mini王只带第一个技能（预告演出）
  uint32_t earliest = 0xFFFFFFFFu;
  for (int i = 0; i < 9; ++i) {
    const uint16_t bit = (uint16_t)(1u << i);   // ⚠️ 必须 16 位：i=8 时 1u<<8=256，uint8_t 会截成 0
    if (!(ab & bit)) continue;                     // 未设置位：永不到期
    if (e.abNextMs[i] > now) {                     // 未到点：只跟踪最早到期位
      if (e.abNextMs[i] < earliest) earliest = e.abNextMs[i];
      continue;
    }
    uint32_t iv = 500;                             // 默认低频复查位
    switch (bit) {
      case AB_HEAL:
        // 王的治疗=自愈（自回血）。曾试过 r=420 光环奶全场：仿真实测
        // L6 慢王+兵潮互相续航打不完（630s 超时）——光环只奶自己。
        if (e.hp < e.maxHp) {
          e.hp += e.maxHp * BOSS_HEAL_PCT / 100.0f;
          if (e.hp > e.maxHp) e.hp = e.maxHp;
          addFx(e.x, e.y, 2);
        }
        iv = BOSS_HEAL_MS;
        break;
      case AB_BROOD: broodSpawn(e); iv = BOSS_BROOD_MS; break;
      case AB_HORN:  hornBlast(e);   iv = HORN_MS;      break;
      case AB_EMP:   empBlast(e);    iv = EMP_MS;       break;
      case AB_GHOST: ghostTrigger(e); iv = GHOST_MS;    break;
      case AB_SIEGE: siegeTrigger(e); iv = SIEGE_IV;    break;   // v0.4.2（节拍>持续，见 layout.py 注释）
      default: break;     // 被动技（SPLIT/GREED/MOLT）挂死亡/受击路径，不进节拍
    }
    if (e.bossMini) iv *= 2;                  // mini 王节拍×2（预告演出）
    e.abNextMs[i] = now + iv;
    if (e.abNextMs[i] < earliest) earliest = e.abNextMs[i];
  }
  e.abilityMs = earliest;                     // 门控=最早到期技能（不变则 500ms 复查）
  if (e.abilityMs == 0xFFFFFFFFu) e.abilityMs = now + 500;
}

// ── v0.4：进入无尽模式（L10 CLEAR 后调用；地图=TERMINAL=L10 本身）──
static void enterEndless(void) {
  levelLoad(LEVEL_COUNT - 1);            // TERMINAL 图+全兵种池
  g_w.endless = 1;
}

// ── 刷一只怪：关卡池 + 波次门控（确定性伪随机，无 rand()）───────
// 返回 true = 消耗了一个 toSpawn 名额（普通怪/BOSS）；无槽 → false
static bool spawnEnemy(void) {
  // v0.4 mid-boss：混在兵潮中段出（刷掉1/3名额后），吓玩家一跳
  if (g_w.miniPending && !g_w.miniSpawned
      && g_w.spawnedOfWave >= waveSize(g_w.wave) / 3) {
    for (int i = 0; i < MAX_ENEMIES; ++i) {
      if (g_w.enemies[i].active) continue;
      spawnKind(i, K_BOSS, (uint8_t)(g_w.spawnedOfWave % LV().routes_n), false, true);
      g_w.miniSpawned = true;
      return true;
    }
  }
  // BOSS：压轴波【先出场领跑】——它最慢，兵潮在其后涌出并穿过治疗光环。
  // （旧逻辑"压轴最后才出"会让 BOSS 永远落在所有快兵身后，光环空转。）
  if (g_w.bossPending) {
    int spawned = 0;
    for (int i = 0; i < MAX_ENEMIES && spawned < LV().boss_count; ++i) {
      if (g_w.enemies[i].active) continue;
      spawnKind(i, K_BOSS, (uint8_t)(spawned % LV().routes_n), false);
      if (LV().boss_count > 1) g_w.enemies[i].x -= spawned * 40.0f; // 双王拉开
      ++spawned;
    }
    if (spawned > 0) { g_w.bossPending = false; return true; }
    // 满场没槽：等下一拍，不消耗名额（防 BOSS 凭空消失）
    return false;
  }

  for (int i = 0; i < MAX_ENEMIES; ++i) {
    if (g_w.enemies[i].active) continue;

    // 兵种池（-1 终止）；虫群连发优先
    int kind = LV().kinds[0];
    if (g_w.swarmLeft > 0) { kind = K_SWARM; --g_w.swarmLeft; }
    else {
      int pool[6], n = 0;
      for (int k = 0; k < 6 && LV().kinds[k] >= 0; ++k) {
        int kd = LV().kinds[k];
        if (kd == K_SWARM) continue;                    // 虫群走连发通道
        if (kd == K_ARMOR && g_w.wave < ARMOR_FROM_WAVE) continue;
        if (kd == K_HEAL  && g_w.wave < 3) continue;   // 奶妈中后期登场
        if (kd == K_CHOPPER && g_w.wave < AIR_FROM_WAVE) continue;  // v0.3.0：第3波起有空袭
        if (kd == K_SHIELD && g_w.wave < SHIELD_FROM_WAVE) continue; // v0.3.2：第5波起弹盾登场
        pool[n++] = kd;
      }
      if (n == 0) { pool[0] = 0; n = 1; }
      uint32_t roll = (g_w.wave * 7u + i * 13u) % 100u;
      // 首个兵种（GRUNT）保底 45%，其余均分剩余权重
      if (roll >= 45 && n > 1) kind = pool[1 + roll % (n - 1)];
      else kind = pool[0];
    }

    bool elite = (LV().elite_from > 0 && g_w.wave >= LV().elite_from
                  && (g_w.spawnedOfWave % 6) == 5);   // 每6只一精英（/4+HP×2.1=L7火力墙死局）
    // v0.3.2b：特殊兵改"固定节拍强制刷"——每 3 只一个空袭/一面盾。
    //   旧版(概率摇+事后降级)：roll 种子一抖整波没盾，玩家学不到规律、
    //   仿真也 flaky。强制节拍＝可学习的节奏感（同屏≤2 防洪水）。
    if (kind != K_SWARM) {
      if (g_w.spawnedOfWave % 3 == 2) {              // 节拍位
        int airAlive = 0, shAlive = 0;
        for (int j = 0; j < MAX_ENEMIES; ++j) if (g_w.enemies[j].active) {
          if (g_w.enemies[j].flying) ++airAlive;
          if (g_w.enemies[j].shield) ++shAlive;
        }
        bool hasCh = false, hasSh = false;
        for (int k = 0; k < 6 && LV().kinds[k] >= 0; ++k) {
          if (LV().kinds[k] == K_CHOPPER && g_w.wave >= AIR_FROM_WAVE)   hasCh = true;
          if (LV().kinds[k] == K_SHIELD  && g_w.wave >= SHIELD_FROM_WAVE) hasSh = true;
        }
        if (hasCh && airAlive < 2)        kind = K_CHOPPER;   // 空袭优先
        else if (hasSh && shAlive < 2)    kind = K_SHIELD;    // 同屏≤2面盾
      } else if (kind == K_CHOPPER || kind == K_SHIELD) {
        kind = LV().kinds[0];                 // 非节拍位摇到特殊兵 → 留作普通兵
      }
    }
    uint8_t route = (uint8_t)(g_w.spawnedOfWave % LV().routes_n); // 双路轮流投放
    spawnKind(i, kind, route, elite);
    if (kind == K_SWARM) g_w.enemies[i].x += (float)(g_w.swarmLeft * 14); // 连发拉开间距
    return true;
  }
  return false;   // 满场：等下一拍
}

// ── 建造 / 升级 / 卖出（金币校验全在这里）────────────────────────
static int findTowerAt(int col, int row) {          // -> towers[] 下标或 -1
  for (int i = 0; i < MAX_TOWERS; ++i)
    if (g_w.towers[i].active && g_w.towers[i].col == col
        && g_w.towers[i].row == row) return i;
  return -1;
}

static bool buildTower(int col, int row, uint8_t type) {
  if ((unsigned)col >= COLS || (unsigned)row >= MAP_ROWS) return false;
  if ((unsigned)type >= TT_COUNT) return false;
  if (g_w.path[col][row] || g_w.occupied[col][row]) return false;
  if (g_w.blocked[col][row]) return false;            // v0.4：禁建岩层
  // v0.4.2：SIEGE 封锁区——圈内一格都不能建（塔心判定）
  if (siegeBlocksPx(col * CELL + CELL / 2.0f, MAP_TOP + row * CELL + CELL / 2.0f))
    return false;
  const int cost = TOWER_TYPES[type].cost;
  if (g_w.gold < cost) return false;

  for (int i = 0; i < MAX_TOWERS; ++i) {
    if (g_w.towers[i].active) continue;
    Tower& t = g_w.towers[i];
    t.active = true; t.col = col; t.row = row;
    t.type = type; t.level = 1; t.invested = cost;
    // v0.3.0：能量塔别落地第一帧就吐钱——给一个完整节拍再产
    t.cooldownMs = (TOWER_TYPES[type].behavior == 3) ? TOWER_TYPES[type].fire_ms : 0;
    t.recoil = 0;
    t.adx = 0.0f; t.ady = -1.0f;
    g_w.occupied[col][row] = true;
    g_w.gold -= cost;
    g_w.hudDirty = true;
    return true;
  }
  return false;                                  // MAX_TOWERS 已满
}

// 升级费 = TT_UP_COST[type][level]（升到 level+1）
static int upgradeCost(const Tower& t) {
  if (t.level >= TOWER_MAX_LEVEL) return -1;
  return TT_UP_COST[t.type][t.level];        // [type][lv]：1->2 用 [1]，2->3 用 [2]
}

static bool upgradeTower(int idx) {
  Tower& t = g_w.towers[idx];
  int c = upgradeCost(t);
  if (c < 0 || g_w.gold < c) return false;
  g_w.gold -= c; t.invested += c; ++t.level;
  g_w.hudDirty = true;
  markDirtyPx(t.col*CELL+CELL/2, MAP_TOP+t.row*CELL+CELL/2, CELL);
  return true;
}

static int sellRefund(const Tower& t) {
  return (int)((long)t.invested * SELL_REFUND_PCT / 100);
}

static bool sellTower(int idx) {
  Tower& t = g_w.towers[idx];
  if (!t.active) return false;
  g_w.gold += sellRefund(t);
  markDirtyPx(t.col*CELL+CELL/2, MAP_TOP+t.row*CELL+CELL/2, CELL);
  g_w.occupied[t.col][t.row] = false;
  t.active = false;
  g_w.hudDirty = true;
  return true;
}

static inline void addFx(float x, float y, uint8_t kind) {
  for (int i = 0; i < MAX_FX; ++i) {
    if (!g_w.fx[i].active) { g_w.fx[i].active = true; g_w.fx[i].x = x; g_w.fx[i].y = y;
                              g_w.fx[i].lifeFrames = 4; g_w.fx[i].kind = kind;
                              markDirtyPx(x, y, CELL); return; }
  }
}

// ── 溅射结算：方形范围全体受伤（无视护甲 —— ARMOR 的克制关系）───
static void splashDamage(float x, float y, int r, uint16_t dmg) {
  addFx(x, y, 1);                            // 橙色爆炸框
  sfxPost(SFXID_BOOM);                       // v0.3.1：爆炸声（队列自带节流）
  markDirtyPx(x - r, y - r, r + CELL);
  markDirtyPx(x + r, y + r, r + CELL);
  for (int j = 0; j < MAX_ENEMIES; ++j) {
    Enemy& e = g_w.enemies[j];
    if (!e.active) continue;
    float dx = e.x - x, dy = e.y - y;
    if (fabsf(dx) > r || fabsf(dy) > r) continue;   // 方形判定，无开方
    if (e.flying) continue;                           // v0.3.0：火箭只打地（溅射也够不着天）
    e.hp -= dmg;                                      // ⚠️ 不减 armor：设计如此
    e.flash = 2;
    bossHurt(e);                        // v0.4 💰🛡 受击被动（偷金/蜕壳）
    markDirtyPx(e.x, e.y, e.size);
    if (e.hp <= 0) killEnemy(e);      // v0.4：溅射击杀收口（SPLIT 联动）
  }
}

// ── 治疗：HEALER 脉冲 / BOSS 光环（healPct% maxHp，r 内友军）────
// ⚠️ HEALER 不奶 BOSS：慢速王 + 奶妈集群 = 永动续航死局（仿真 L6 实测
//    630s 打不死）。奶妈的定位是"保兵潮"，BOSS 必须集火速杀。
static void healAllies(const Enemy& src, int rPx, int pct) {
  const float r2 = (float)rPx * rPx;
  for (int j = 0; j < MAX_ENEMIES; ++j) {
    Enemy& e = g_w.enemies[j];
    if (!e.active || &e == &src) continue;
    if (e.kind == K_BOSS && src.kind != K_BOSS) continue;   // 奶妈不抬王
    float dx = e.x - src.x, dy = e.y - src.y;
    if (dx*dx + dy*dy > r2) continue;
    if (e.hp >= e.maxHp) continue;
    e.hp += e.maxHp * pct / 100.0f;
    if (e.hp > e.maxHp) e.hp = e.maxHp;
    addFx(e.x, e.y, 2);                     // kind=2 = 治疗绿光（render）
    markDirtyPx(e.x, e.y, e.size);
  }
}

// ── 每帧逻辑更新：dtMs = 距上一帧毫秒（调用方限幅）──────────────
static void gameUpdate(uint32_t dtMs) {
  // v0.4.2：SIEGE 封锁倒计时是全局时钟——必须在状态门控**之前**递减，
  // 否则通关/死亡 overlay（ST_CLEAR/GAMEOVER 下 gameUpdate 提前 return）
  // 期间封锁框会永久闪烁。关卡状态不影响它到期。
  if (g_w.siegeMs > 0) {
    g_w.siegeMs = (uint16_t)(g_w.siegeMs > dtMs ? g_w.siegeMs - dtMs : 0);
    if (g_w.siegeMs == 0) g_w.hudDirty = true;           // 警示条撤下
  }
  if (g_w.state != ST_WAVE && g_w.state != ST_READY) return;
  if (g_w.selectedTower >= 0) return;   // 升级面板打开 = 世界暂停（触屏无实体键，弹层即暂停）

  const float dt = dtMs / 1000.0f;
  uint32_t now = millis();

  // ── v0.2.1：波间倒计时。首波（wave==0）保持手动，给足布塔时间；
  //    之后每波打完自动进入下一波，制造压迫感。面板打开时不计时（上面已 return）
  if (g_w.state == ST_READY && g_w.wave > 0) {
    if (g_w.readyLeftMs == 0xFFFFFFFFu) g_w.readyLeftMs = AUTO_NEXT_MS;   // 首次进 READY：装载
    if (g_w.readyLeftMs > dtMs) g_w.readyLeftMs -= dtMs;
    else { startNextWave(); return; }                    // 归零 = 自动开波
  }
  // ── v0.4.2：无尽模式历史最高波次实时刷新（死亡/切语言时随落盘带走）──
  if (g_w.endless && g_w.wave > g_endBest) {
    g_endBest = (int16_t)g_w.wave;
    g_w.hudDirty = true;
  }
  // ── 刷怪（虫群连发用更短间隔，视觉上才是"一群"）────────────
  if (g_w.state == ST_WAVE && g_w.toSpawn > 0 && now >= g_w.nextSpawnMs) {
    if (spawnEnemy()) { --g_w.toSpawn; ++g_w.spawnedOfWave; }  // BOSS/普通怪都占名额；满场等一拍
    int iv = 900 - g_w.wave * 40; if (iv < 260) iv = 260;
    if (g_w.swarmLeft > 0) iv = 120;         // 虫群连发间隔
    g_w.nextSpawnMs = now + iv;
  }

  // ── 敌人：沿各自 route 前进（含减速）/ 漏怪扣血 / 技能 ────────
  for (int i = 0; i < MAX_ENEMIES; ++i) {
    Enemy& e = g_w.enemies[i];
    if (!e.active) continue;

    const int cnt = routeCount(e.route);
    float tx, ty;
    if (e.flying) {
      // v0.3.0 空中单位：无视路径格，从当前位置直线飞向本 route 终点。
      // wp 直接推到倒数第二段，走"最后一跳"的通用抵达逻辑。
      tx = ALL_WP[routeStart(e.route) + cnt].col * CELL + CELL / 2.0f;
      ty = MAP_TOP + ALL_WP[routeStart(e.route) + cnt].row * CELL + CELL / 2.0f - FLY_LIFT;
      e.wp = (int16_t)(cnt - 2);
    } else {
      routeWPPx(e.route, e.wp + 1, &tx, &ty);
    }
    float dx = tx - e.x, dy = ty - e.y;
    float d  = sqrtf(dx*dx + dy*dy);
    uint16_t spd = e.speed;                          // 冰霜减速生效？
    if (e.slowMs > 0) {
      e.slowMs = (uint16_t)(e.slowMs > dtMs ? e.slowMs - dtMs : 0);
      spd = (uint16_t)((long)e.speed * FROST_SLOW_PCT / 100);
    } else if (e.boostMs > 0) {                     // v0.4 📢 号角加速（冰霜优先）
      e.boostMs = (uint16_t)(e.boostMs > dtMs ? e.boostMs - dtMs : 0);
      spd = (uint16_t)((long)e.speed * (100 + HORN_BOOST_PCT) / 100);
    }
    if (e.ghostMs > 0) e.ghostMs = (uint16_t)(e.ghostMs > dtMs ? e.ghostMs - dtMs : 0); // v0.4 🌫
    float step = spd * dt;

    if (d <= step || d < 0.5f) {                     // 抵达航点
      e.x = tx; e.y = ty; ++e.wp;
      if (e.wp >= cnt - 1) {                        // 漏到左终点
        e.active = false;
        ++g_w.leakCount;                            // v0.3.0：结算面板统计
        sfxPost(SFXID_LEAK);                        // v0.3.1：基地挨打警报
        g_w.lives -= e.leakDmg;
        if (g_w.lives < 0) g_w.lives = 0;
        g_w.hudDirty = true;
        addFx(e.x, e.y, 0);
        if (g_w.lives == 0) g_w.state = ST_GAMEOVER;
        continue;
      }
    } else {
      e.x += dx / d * step;
      e.y += dy / d * step;
    }
    if (e.flash > 0) --e.flash;
    if (e.flying && g_w.state == ST_WAVE) sfxPost(SFXID_ROTOR);  // v0.3.1：天上就有突突声（队列节流380ms）

    // ── 被动技能（到点才放，避免每帧白算）────────────────────
    if (now >= e.abilityMs) {
      if (e.kind == K_HEAL) {                       // 奶妈：脉冲救队友
        healAllies(e, 90, HEAL_PCT);
        e.abilityMs = now + HEAL_MS;
      } else if (e.kind == K_BOSS) {
        bossAbilities(e, now);          // v0.4：8技能节拍统一轮转
      } else {
        e.abilityMs = now + 500;                    // 普通怪：低频检查位
      }
    }

    markDirtyPx(e.x, e.y, e.size);
  }

  // ── 塔：按 behavior 分派（索敌=射程内最近，进圈即接战）──────
  for (int i = 0; i < MAX_TOWERS; ++i) {
    Tower& t = g_w.towers[i];
    if (!t.active) continue;
    const TowerType& T = TOWER_TYPES[t.type];

    if (t.cooldownMs > 0) t.cooldownMs = (uint16_t)(t.cooldownMs > dtMs ? t.cooldownMs - dtMs : 0);
    if (t.recoil > 0) { --t.recoil; markDirtyPx(t.col*CELL+CELL/2, MAP_TOP+t.row*CELL+CELL/2, CELL); }
    if (t.beamMs > 0) {                    // v0.3.0：激光光束残留（纯视觉）
      t.beamMs = (uint8_t)(t.beamMs > dtMs ? t.beamMs - dtMs : 0);
      markDirtyPx(t.bx, t.by, CELL * 2);          // 终点周围擦一圈
      markDirtyPx(t.col*CELL+CELL/2, MAP_TOP+t.row*CELL+CELL/2, CELL * 8);
    }
    if (t.cooldownMs > 0) continue;

    const float tx = t.col * CELL + CELL / 2.0f;
    const float ty = MAP_TOP + t.row * CELL + CELL / 2.0f;
    const float rng = towerRange(t);
    float r2 = rng * rng;

    // v0.4 🔇 EMP 哑火：不索敌不开火（渲染冒黑烟），节拍继续走
    if (t.stunMs > 0) {
      t.stunMs = (uint16_t)(t.stunMs > dtMs ? t.stunMs - dtMs : 0);
      markDirtyPx(tx, ty, CELL);          // 黑烟动画逐帧擦画
      continue;
    }

    // v0.3.0 能量塔：不索敌、不开火，到点吐金币（投资换后期）
    if (T.behavior == 3) {
      g_w.gold += towerDamage(t);        // damage 列语义=每脉冲产金
      sfxPost(SFXID_COIN);                          // v0.3.1：叮咚
      g_w.hudDirty = true;
      addFx(tx, ty - 10, 3);            // kind=3 = 金币上浮（render）
      t.cooldownMs = towerFireMs(t);
      continue;
    }

    int best = -1; float bestD2 = r2 + 1.0f;
    const bool airOk = TOWER_AIR_OK[t.type] != 0;   // v0.3.0 对空矩阵
    for (int j = 0; j < MAX_ENEMIES; ++j) {
      Enemy& e = g_w.enemies[j];
      if (!e.active) continue;
      if (e.flying && !airOk) continue;  // 加农/冰霜够不着天
      if (e.ghostMs > 0) continue;       // v0.4 🌫 隐身：塔不可锁定
      float ddx = e.x - tx, ddy = e.y - ty;
      float d2  = ddx*ddx + ddy*ddy;
      if (d2 < bestD2) { bestD2 = d2; best = j; }
    }

    if (best >= 0) {
      Enemy& e = g_w.enemies[best];
      float dx = e.x - tx, dy = e.y - ty;
      float d  = sqrtf(dx*dx + dy*dy); if (d < 1) d = 1;
      t.adx = dx / d; t.ady = dy / d;
      const int oc = turretOctant(dx, dy);
      t.aoct = (int8_t)oc;

      if (T.behavior == 2) {
        // ❄️ 冰霜：光环脉冲，范围内全体减速（无子弹）
        sfxPost(SFXID_FROST);                       // v0.3.1：叮铃
        for (int j = 0; j < MAX_ENEMIES; ++j) {
          Enemy& f2 = g_w.enemies[j];
          if (!f2.active) continue;
          if (f2.flying) continue;               // v0.3.0：冰雾够不着天
          float ex = f2.x - tx, ey = f2.y - ty;
          if (ex*ex + ey*ey <= r2) {
            f2.slowMs = FROST_SLOW_MS;           // 离开光环后仍残留
            markDirtyPx(f2.x, f2.y, f2.size);
          }
        }
      } else if (T.behavior == 4) {
        // ⚡ v0.3.0 激光：瞬时命中（无弹道），伤害过护甲，光束残留纯视觉
        sfxPost(SFXID_LASER);                       // v0.3.1：咻
        int dmg = towerDamage(t) - e.armor;
        if (dmg < 1) dmg = 1;
        e.hp -= dmg;
        e.flash = 3;                              // 激光闪得更亮一点
        bossHurt(e);                            // v0.4 💰🛡 激光也算受击
        markDirtyPx(e.x, e.y, e.size);
        t.bx = e.x; t.by = e.y;
        t.beamMs = LASER_BEAM_MS;
        addFx(e.x, e.y, 0);
        if (e.hp <= 0) killEnemy(e);          // v0.4：激光击杀收口
      } else {
        // 机枪 / 加农：从枪口出膛，弹种参数发射时定死
        const int cx = t.col * CELL + CELL / 2;
        const int cy = MAP_TOP + t.row * CELL + CELL / 2;
        for (int b = 0; b < MAX_BULLETS; ++b) {
          if (g_w.bullets[b].active) continue;
          Bullet& bl = g_w.bullets[b];
          bl.active = true;
          bl.x = cx + TURRET_DIR[oc][0];
          bl.y = cy + TURRET_DIR[oc][1];
          bl.vx = (dx / d) * T.proj_speed;      // ⚠️ float，勿加 int 强转
          bl.vy = (dy / d) * T.proj_speed;
          bl.spawnDist2 = bestD2;
          bl.damage  = (uint16_t)towerDamage(t);
          bl.splashR = T.behavior == 1 ? (uint8_t)T.splash_r : 0;
          // v0.4.2：机枪三种音色轮换（GUN/GUN2/GUN3 基频 ×1/×1.25/×1.5）——
          // 旧版满场机枪同一音高，听起来像节拍器。轮换而非随机：可复现。
          if (T.behavior == 1) {
            sfxPost(SFXID_CANNON);
          } else {
            s_gunVariant = (uint8_t)((s_gunVariant + 1) % 3);
            sfxPost((uint8_t)(SFXID_GUN + s_gunVariant));   // ID 连续：GUN=0,GUN2=1,GUN3=2
          }
          break;
        }
      }

      t.cooldownMs = towerFireMs(t);
      t.recoil     = 3;
      markDirtyPx(tx, ty - 6, CELL);
    }
  }

  // ── 子弹：飞行 + AABB 碰撞；炮弹命中触发溅射──────────────────
  for (int i = 0; i < MAX_BULLETS; ++i) {
    Bullet& b = g_w.bullets[i];
    if (!b.active) continue;

    float nx = b.x + b.vx * dt, ny = b.y + b.vy * dt;
    b.x = nx; b.y = ny;                       // ⚠️ 必须写回（真机 bug #2）
    markDirtyPx(nx, ny, CELL/2);

    if (nx < -8 || nx > SCREEN_W + 8 || ny < MAP_TOP - 8 || ny > SCREEN_H + 8) {
      b.active = false; continue;
    }

    const float halfB = CELL / 2.0f;
    for (int j = 0; j < MAX_ENEMIES; ++j) {
      Enemy& e = g_w.enemies[j];
      if (!e.active) continue;
      float hs = e.size / 2.0f + halfB;
      if (nx >= e.x - hs && nx <= e.x + hs && ny >= e.y - hs && ny <= e.y + hs) {
        if (e.flying && b.splashR > 0) continue;   // v0.3.0：炮弹从直升机下方穿过
        b.active = false;
        if (b.splashR > 0) splashDamage(nx, ny, b.splashR, b.damage); // 溅射（无视护甲）
        else {                                                          // 单体：过护甲减免
          int dmg = b.damage - e.armor;
          if (e.shield) dmg >>= 1;              // v0.3.2：弹盾——破片减半
          if (dmg < 1) dmg = 1;                 // 保底 1：永远打不动=体验灾难
          sfxPost(SFXID_HIT);                           // v0.3.1：击中 tick
          e.hp -= dmg;
          e.flash = 2;
          bossHurt(e);                        // v0.4 💰🛡 子弹直击也算受击
          addFx(nx, ny, 0);
          markDirtyPx(e.x, e.y, e.size);
          if (e.hp <= 0) killEnemy(e);      // v0.4：子弹击杀收口
        }
        break;
      }
    }
  }

  // ── 特效寿命（溅射框更大，多活一帧）──────────────────────────
  for (int i = 0; i < MAX_FX; ++i) {
    Fx& f = g_w.fx[i];
    if (!f.active) continue;
    markDirtyPx(f.x, f.y, CELL);
    if (--f.lifeFrames == 0) { f.active = false; }
  }

  // ── v0.3.2：压叠窗口。刷怪完毕但残敌还在爬 → 倒计时强开下波。
  //    （旧规则"全清才进下一波"让塔永远集火单只，溢出伤害全浪费。）
  // ⚠️ static 跨关存活：必须绑 wave——换关/重开后 wave 变了就重置，
  //    否则带着上一关的 until 秒开新波。
  // ⚠️ 末波绝不压叠：wave>=waves 强开下一关怪=永远到不了 ST_CLEAR
  //    （通关判定只认"全灭瞬间在最后一波"）。末波老老实实等清场。
  static uint32_t s_overlapUntil = 0;
  static int16_t  s_overlapWave  = -1;
  if (s_overlapWave != g_w.wave) { s_overlapUntil = 0; s_overlapWave = g_w.wave; }
  if (g_w.state == ST_WAVE && g_w.toSpawn == 0 && !g_w.bossPending
      && g_w.wave < lvWaves()) {                 // v0.4：无尽用 lvWaves()（250）
    int alive = 0;
    for (int i = 0; i < MAX_ENEMIES; ++i) if (g_w.enemies[i].active) ++alive;
    if (alive >= 8) {                          // v0.3.2：残敌≥8才压叠（漏1~2只不值得开新波）
      if (s_overlapUntil == 0) s_overlapUntil = now + OVERLAP_MS;
      else if (now >= s_overlapUntil) { g_w.state = ST_READY; g_w.readyLeftMs = 1; }
    } else s_overlapUntil = 0;
  }

  // ── 波次完成判定（含 BOSS/虫群全部清场）──────────────────────
  if (g_w.state == ST_WAVE && g_w.toSpawn == 0) {
    bool anyAlive = false;
    for (int i = 0; i < MAX_ENEMIES; ++i) if (g_w.enemies[i].active) { anyAlive = true; break; }
    if (!anyAlive && !g_w.bossPending) {
      g_w.gold += WAVE_BONUS_BASE + g_w.wave * WAVE_BONUS_STEP; // 近似固定，防滚雪球
      g_w.hudDirty = true;
      if (!g_w.endless && g_w.wave >= LV().waves) {   // 🏆 通关：解锁下一关（无尽永不触发）
        sfxPost(SFXID_CLEAR);                       // v0.3.1：上行号角
        g_w.state = ST_CLEAR;
        g_cleared |= (1u << g_lv);
        // v0.3.0：按剩余生命评星，取历史最高（收集目标）
        uint8_t st = starsFor(g_w.lives);
        if (st > g_stars[g_lv]) g_stars[g_lv] = st;
        if (g_lv + 1 < LEVEL_COUNT) g_unlocked |= (1u << (g_lv + 1));
      } else {
        g_w.state = ST_READY;
        g_w.readyLeftMs = 0xFFFFFFFFu;              // v0.2.1：下帧装载倒计时
      }
    }
  }
}
