# FIXLOG v0.4.1 — 全量 code review 的问题、修正与经验

> **本文是 v0.4.1 的工作档案**：发现了什么问题、怎么修的、踩过什么坑、下次开工从哪继续。
> 与 `docs/HANDOFF-PROMPT.md`（权威交接提示词）冲突时，以那份为准；本文是它的论据库。
> 工作树：`/Users/jiangweizhong/WorkBuddy/QQwbb/pixel-tower-defense`（公开仓的克隆，
> 本地 commit，**未 push**）。作者开发树 `~/esp32/tab5-tower-defense` 与公开树
> `~/esp32/pixel-tower-defense-public` 的同步走 `tools/sync-public.sh`，本目录的改动
> 需要先合回开发树再走原发布流程（见文末"遗留事项"）。

---

## 一、做了什么（一句话）

对 v0.4.0 全仓做了一轮 review：先复现基线（sim 165 断言全过、生成物与真源一致），
再通读 `src/` 全部源码，共确认 **2 个真 bug + 1 个静默丢进度 + 6 类设计/工程问题**，
全部修掉并补了回归断言（**165 → 178**），最后编译打包 v0.4.1。

## 二、问题清单（按严重度）

### P0-1　通关画面每帧写 NVS（flash 磨损 + 掉帧）✅已修+有回归

- **现象**：L17–L20 通关后，结算画面停留期间每帧（30fps）都写一次 NVS。
- **根因**：`main.cpp` 的去重缓存 `static uint16_t s_savedUnlock/s_savedClear`，
  而 20 关的 `g_unlocked/g_cleared` 是 `uint32_t`。L17 解锁后 bit16 置位，
  16bit 缓存装不下 → 比较恒不相等 → 每帧都判定"进度变了"。
  已用独立小程序复现：L16 停留 5 帧写 1 次，L17 停留 5 帧写 5 次。
- **为何漏网**：`saveProgress` 在仿真里是空实现，去重逻辑又在 `main.cpp` 的
  `loop()` 里——正好在测试覆盖不到的那 20 行。
- **修法**：新建 `src/progress.h`，把"何时落盘"抽成纯逻辑层
  （`progressTickOnClear/progressSaveNow/progressInvalidate`），缓存一律 `uint32_t`。
  `loop()` 只调 `progressTickOnClear()`。
- **回归**：`sim/run.cpp` §15——L17 通关画面停留 30 帧，断言只落盘 1 次。

### P0-2　重打关卡刷高星级，静默丢失 ✅已修+有回归

- **现象**：已通关的关重打拿到更高星级（如 1★→3★），进度不落盘。
- **根因**：旧去重只看 `unlock/clear` 两位图；`g_stars` 只在
  `if (st > g_stars[g_lv])` 时变大，两位图没变 → 不写 NVS → 断电即丢。
- **修法**：`progress.h` 的进度签名 = 解锁位图 ⊕ 通关位图 ⊕ 两组星级打包
  （`progressSigA/B`），任一变化即落盘。
- **回归**：`sim/run.cpp` §15——改 `g_stars[16]=3` 后必须触发第二次落盘；
  无变化时再 tick 10 帧零写入。

### P1-1　BOSS 技能节拍被最后一个 if 劫持 ✅已修+有回归

- **现象**：多技能 BOSS 的所有主动技同一帧齐放，且节拍 = if 链里最后一个技能的常量
  （GHOST 排最后永远赢）。实测扭曲 7/20 关：
  - L6 FROST LORD（HEAL+GHOST）：自愈变 7s/次（标称 2s）
  - L10 THE END（HEAL+BROOD+HORN+EMP 四主动技）：同一 tick 全炸，全场 9s 一轮
  - L13/L16/L19/L20：产卵/自愈全部被拖慢
- **根因**：`bossAbilities()` 只有一个 `abilityMs`，到期后顺序执行所有置位技能，
  `next` 被反复覆盖成最后一个的值。
- **修法**：`Enemy` 增加 `uint32_t abNextMs[8]`（按技能位独立倒计时，
  未设置位 = `0xFFFFFFFF` 永不到期）；`abilityMs` 降级为"最早到期位"门控；
  BOSS 出生时各技能错峰起手（+1000ms 起、每位 +350ms），杜绝开场技能风暴；
  mini 王 ×2 节拍保留。
- **回归**：`sim/run.cpp` §16——L6 逼停所有表后：2s 时 HEAL 必须已自愈且
  GHOST 未动；7s 时 GHOST 必须现身。

### P1-2　波量公式两处各写一遍 ✅已修+有回归

- `game.h` `startNextWave` 与 `spawnEnemy`（mini 王出场阈值）都硬编码
  `6 + (wave*3)/2`。改一处忘另一处 → mini 王出场时点静默漂移。
- **修法**：抽 `static inline int waveSize(int w)`，两处都改调用。
- **回归**：§18 逐点断言（w=1→7, 2→9, 3→10, 10→21，注意整数除）。

### P2-1　HUD 数字擦除框的像素账没算平 ✅已修

- HP 擦除框宽 35px（196..231），但 scale-3 两位数字宽 36px（204..237）：
  生命 10→9 时旧数字右缘 6px 擦不掉，留一道旧 '0' 残边。
- GOLD 框按 5 位数字配——无尽模式金币超 5 位会留残影。
- WAVE 框按 "15/15" 配——无尽 "250/250" 会伸进 NEXT WAVE 按钮区，
  重演 v0.2.1 真机 bug #3（按钮左缘被刷黑底）。
- **修法**：三个框按"最长可能内容"配宽；WAVE 文本左移（964→900），
  右缘仍 < BTN_X=1064；顺手把 `LV().waves` 改成 `lvWaves()`（无尽总波数）。

### P2-2　选关界面点锁定关的红叉永留 ✅已修+有回归

- `actions.h` 画完 X 没有任何擦除路径，只有下次 `drawLevelSelect` 才整屏重画。
- **修法**：`SelFx`（node + 20 帧寿命），`updateSelFx()` 到期时重画该节点
  （为此把节点绘制从 `drawLevelSelect` 抽成 `drawSelNode()`）；`main.cpp` 选关
  分支和仿真 `tick()` 都调它。
- **回归**：§17 像素级断言——红叉出现（==C_BAD）、20 帧后节点复原（==before）。

### P2-3　单编译单元是隐式约定 ✅已立字为据

- 所有状态（`g_w`/音效队列/render 缓存/`g_buildFx`…）都是头文件 static，
  只靠 `build_src_filter` 恰好只有一个 main.cpp 才成立。谁加第二个 .cpp，
  就是静默双份状态、无链接报错。
- **修法**：`game.h` 顶部写明"单 TU 约定"与拆分前提（先搬 game.cpp 再谈）。

### P3-1　Game Over / 通关画面压暗太贵 ✅已修

- 57,600 次 2×2 fillRect（约百毫秒），死亡/通关瞬间明显卡顿。
- **修法**：4×4@8px 步进，14,400 次，25% 覆盖率不变，像素风下肉眼无差。

### P3-2　像素字逐列 fillRect ✅已修

- `px_char` 每字每行逐列画。改为行内连续置位 RLE 合并——**输出像素完全一致**
  （连续段合并不改变任何像素），HUD/面板文字场景调用数降约 2/3。

### P3-3　工程化缺口 ✅已修

- **无 CI**：`.github` 只有 issue/PR 模板。新增 `.github/workflows/sim.yml`：
  编译跑 sim + 三个生成物（config_gen/pixel_font/sfx_data）漂移检查。
  三个生成器已实测可复现入库版本（纯 stdlib，无第三方依赖）。
- **README 数值漂移**：塔升级"每级射程 +25%、伤害 ×1.5" vs 代码
  `TOWER_RANGE_SCALE=0.12 / TOWER_DMG_SCALE=1.3`（v0.3.2 重平衡后没更新文档）。
- **死宏清理**：`BOSS_HEAL_R`（光环半径，治疗改自愈后失效）从 layout.py 删除；
  `lvWaves()` 的魔数 250 提为 `ENDLESS_WAVES` 常量；`g_lv` 注释 "0..9"→实际 20 关。
- **依赖资产指纹**：`platform_packages` 钉的 bmorcelli libs 是带日期的 GitHub
  release 资产，BUILDING.md 已提示记录/归档（见文末遗留事项）。

## 三、验证（全部真实执行过）

| 检查 | 结果 |
|---|---|
| `g++ -std=c++17 -O2 -I sim/compat -I src sim/run.cpp -o sim/sim && ./sim/sim` | **178 passed, 0 failed**（v0.4.0 基线 165） |
| `python3 tools/layout.py; echo GEN_EXIT=$?` | `GEN_EXIT=0`，config_gen.h 仅差预期两行（删死宏+加常量） |
| `python3 tools/gen_font.py && python3 tools/gen_sfx.py` | 输出与入库逐字节一致（CI 漂移检查会过） |
| 门禁关 bot | L1–L4 + L11–L13 仍全通，非门禁关失败集合与 v0.4.0 **完全一致**（节拍修复未改变难度读数） |
| `pio run -e tab5` | 编译+链接成功：RAM 37,016 B（7.2%）/ Flash 819,586 B（26.1%）（v0.4.0：35,472/819,150；+1,544 B ≈ Enemy.abNextMs[8] 48×32B） |
| 合并镜像 | `releases/pixel-tower-defense-v0.4.1-tab5-merged.bin`，929,360 B，sha256 `d018587b…aa81`；三段头与 v0.4.0 布局逐字节一致（boot e903024f / parts aa50 / app e907024f，DIO） |
| **真机刷入+回读**（2026-10-12，ESP32-P4 rev 1.3，`/dev/cu.usbmodem1101`） | `write_flash 0x0 merged.bin` → `read_flash 0x0 929360` 回读：**固件本体（bootloader/分区/boot_app0/app）逐字节一致**，app 段 sha256 两侧同为 `682b739d…`；开机横幅 `=== PIXEL TOWER DEFENSE v0.4.1 | board=22 | 1280x720 ===`；旧 NVS 存档 `unlock=0007 cleared=0003` 被新固件正确读入 |

> 注：编译在最后一步（pioarduino 的 size 检查）被本机沙箱拦了一个文件操作，
> 但编译与链接已成功完成；firmware.bin 由 esptool elf2image 补齐（DIO@80m），
> 合并走 merge_bin。产物结构已用 BUILDING.md §4 的同款脚本核验。
>
> **回读差异只有 NVS 区（0x9000–0xDFFF）62 字节**，这是 esptool 压缩写入的
> 特性：镜像里全 0xFF 的块被跳过（不擦不写），所以设备上 v0.4.0 的旧存档
> 原样保留——v0.4.1 与 v0.4.0 的 NVS 键格式相同，旧进度直接可续用。
> （M5Burner 是 `eraseAll` 语义，走它才是全新安装；此差异已写进 BUILDING.md §4。）

## 四、经验（下次别再交学费）

1. **"去重/缓存"永远是整型宽度的重灾区。** 本项目两年内同族 bug 犯了两次：
   v0.4 的 unlock 位图 int→uint32（README 还拿它当正面案例），v0.4.1 的
   去重缓存没跟着升级。**规则：位图一扩宽，所有缓存/比较/掩码全搜一遍。**
2. **仿真覆盖不到的地方就是 bug 的住处。** P0-1/P0-2 都躲在 `main.cpp` 的
   `loop()`（NVS/落盘）——游戏逻辑全在仿真里，胶水层不在。**规则：胶水逻辑
   （落盘/输入分发/帧节奏）也要能进仿真，抽成纯函数+钩子。**
3. **"到期全做一遍"是多技能系统的默认坑。** 一个计时器 + if 链 = 后面的技能
   劫持前面技能的节拍。**规则：每个周期性行为一张自己的表。**
4. **erase-box 按"最长内容"配，不是"当前内容"。** HUD 三个框全是这个病；
   而且配宽时先画一张"内容长度 vs 邻居元素"的草图，别让擦除区伸进
   只按状态翻转重画的区域（v0.2.1 bug #3 的教训）。
5. **测试断言里的"期望值"要手算两遍。** 本轮 3 个 FAIL 全是我测试自己的算术错
   （红叉区块坐标推错、ghost 1500ms 衰减被我 tick 超时、整数除期望值算错）。
   **规则：断言失败先怀疑断言，再怀疑被测代码——但两者都要给出复算过程。**
6. **仿真与固件的分叉会骗人。** 旧仿真在 ST_SEL 也跑 `renderFrame`（会把 HUD
   画到选关屏上），与真机 `loop()` 的选关分支不符。已让 `tick()` 镜像真机。
   **规则：仿真每个分支都要能在 main.cpp 找到对应行。**
7. **CI 漂移检查是"单一真源"纪律的牙齿。** layout.py → config_gen.h 的一致
   此前靠人自觉；现在 PR 里改生成器不重新生成直接红。

## 五、下次开工提示词（整份复制粘贴）

```
接续 pixel-tower-defense（M5Stack Tab5 / ESP32-P4 像素塔防，零素材铁律）。
v0.4.1 已由一轮全量 review 修完并打包，先读 docs/FIXLOG-v0.4.1.md 再动手。

【树的位置】
- 本目录 = 公开仓克隆（github.com/andjiang0083/pixel-tower-defense），v0.4.1 的
  修复都在这里，已本地 commit、已刷真机回读验证，未 push、未建 release。
- 作者开发树 ~/esp32/tab5-tower-defense、发布镜像树 ~/esp32/pixel-tower-defense-public
  （只准 tools/sync-public.sh 单向同步）——本目录的改动要合回开发树再走原发布流程。
- 大仓 ~/esp32 已不维护，别在里面找代码。

【已验证事实（2026-10-12，复跑命令在 docs/HANDOFF-PROMPT.md）】
- sim：178 passed / 0 failed（v0.4.0 是 165；新增 §15-§18 全是 v0.4.1 回归）
- 生成器：layout/gen_font/gen_sfx 输出与入库一致，CI 会查漂移
- 门禁关 L1-4 + L11-13 bot 全通；L8/L14-20 bot 打不过是设计使然（bot 不是难度标尺）
- 构建/哈希/镜像结构：见 README 验证表与 BUILDING.md
- 真机（ESP32-P4 rev 1.3）已刷入并回读：固件本体逐字节一致，NVS 旧存档保留，
  开机横幅 v0.4.1 | board=22（board 打的是 M5Unified 枚举值，不是字符串）

【v0.4.1 修了什么（细节见 FIXLOG 第二节）】
P0：NVS 去重缓存 uint16→uint32（L17-20 通关画面每帧写 flash）——已抽成
    src/progress.h，仿真有断言；顺带修了"重打刷高星级不落盘"。
P1：BOSS 技能改 abNextMs[8] 独立倒计时（旧实现节拍被最后一个 if 劫持，
    L6 治疗 7s/次、L10 五技能同 tick）；waveSize() 收编两处重复公式。
P2：HUD 三个擦除框按最长内容配宽；选关锁关红叉 20 帧自擦；game.h 立单 TU 约定。
P3：压暗 4×4 步进；px_char RLE 合并；CI workflow（sim+三生成器漂移）。
清掉死宏 BOSS_HEAL_R，lvWaves 魔数 250 → ENDLESS_WAVES。

【规矩（沿用，别重犯）】
- 改数值只改 tools/layout.py，然后 python3 tools/layout.py，必须 GEN_EXIT=0。
- 零素材铁律：固件里不许出现图片/音频文件。
- 末波绝不压叠；静态计时器绑 wave 并在 levelLoad 复位。
- 版本号小步走（patch 号），一版一个 CHANGELOG 条目。
- 依赖已钉精确版，别放宽成 ^。
- 完成 = 有真实执行输出；提交用 git add <具体路径>，禁止 git add -A。
- 不用串口调试；真机验收靠开机横幅自报版本行。

【建议的下一步（按风险排）】
1. 把本目录 v0.4.1 的改动合回开发树 → sync-public.sh 同步公开树 → push →
   建 GitHub Release v0.4.1（merged bin + sha256 + 封面）→ M5Burner 更新条目。
2. 无尽模式入口 UI（引擎早 ready，缺"L10 CLEAR 后进无尽"的入口+状态）。
   注意 FIXLOG P2-1：HUD 的 GOLD/WAVE 框已按无尽位数配好，直接接即可。
4. 真人试玩回收 L11-L20 的失败波次（bot 不是难度标尺）。
5. README 首图换 Tab5 真机实拍（docs/screenshots/ 现在全是仿真渲染图）。
```

## 六、遗留事项（需要你拍板/外部条件）

1. **未 push、未建 release**：v0.4.1 的 merged bin 与 sha256 已在本地
   `releases/` 备好（文件名已改为 v0.4.1），但 GitHub Release / M5Burner 更新
   需要你的凭证与账号操作——按 HANDOFF-PROMPT.md 的纪律走。
2. **bmorcelli libs 资产指纹**：`platform_packages` 里那个带日期的 tarball
   （`launcher_tab5_esp32-arduino-libs-20260519-233957.tar.xz`）建议下载一份
   存档并把 sha256 记进 BUILDING.md——GitHub release 资产被删则构建不可复现。
3. **~~真机验收~~（已完成 2026-10-12）**：v0.4.1 已刷入 ESP32-P4 rev 1.3 并回读
   （固件本体逐字节一致，NVS 旧存档保留，横幅 v0.4.1）。v0.4.1 改了 BOSS 节拍与
   HUD 渲染，真人试玩时重点看：L6 王自愈频率（应回到标称 2s）、L10 终王技能是否
   错峰、生命 10→9 时 HP 数字无残边、通关/死亡画面卡顿是否减轻。
   剩下一击是**真人试玩回收**（L11–L20 第几波崩）与**真机实拍**（README 首图），
   硬件现在在线（`/dev/cu.usbmodem1101`）。
```
