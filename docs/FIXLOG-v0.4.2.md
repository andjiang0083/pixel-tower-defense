# FIXLOG v0.4.2 — 启动界面/双语/引导/无尽入口/SIEGE：问题、修正与经验

> **本文是 v0.4.2 的工作档案**：做了什么、发现什么问题、怎么修的、踩过什么坑、下次开工从哪继续。
> 与 `docs/HANDOFF-PROMPT.md`（权威交接提示词）冲突时，以那份为准；本文是它的论据库。
> 工作树：`/Users/jiangweizhong/WorkBuddy/QQwbb/pixel-tower-defense`（公开仓克隆）。
> 本轮范围：用户点名"启动界面 + 好看标题 + 版本 + 语言选择"，外加 v0.5.0 建议清单的剩余全部条目，
> 版本号按用户要求克制在小步号（v0.4.2，不跳 0.5.0）。

---

## 一、做了什么（一句话）

把 v0.5.0 建议清单剩余条目全部做完并压进 v0.4.2：新增开机启动界面（像素大标题 + 版本 + 语言选择）、
全 UI 双语（EN/中文，16×16 中文点阵从源码生成）、首次三步引导 overlay、无尽模式入口
（L20 通关画面进入 + NVS 最高波次）、机枪三音色轮换、第 9 个 BOSS 技能 SIEGE（空间封锁）、
bot 分兵升级、L14–L20 难度数据化扫描（`--x2gold`）；仿真 **178 → 203** 断言，连抓 3 个
uint8_t 截断真 bug + 1 个持续/节拍混淆设计缺陷。

## 二、问题清单（按严重度）

### P0-1　三个 uint8_t 截断点，会让 SIEGE 在真机上静默失效 ✅已修+有回归

- **现象**：L11 王（ability=268 = SPLIT|HORN|SIEGE）的 SIEGE 技能排不上触发表——
  只有仿真里手动逼停所有其他技能才能观察到它放出来。
- **根因**：v0.4.1 把技能位掩码从 8 位扩到 9 位（SIEGE=256）时，只改了 `LevelDef.ability`
  字段类型和前向声明，漏了三处**局部变量/循环变量**仍是 `uint8_t`：
  1. `bossAbilityOf` 定义体内的 `const uint8_t ab`（前向声明是 uint16_t，定义体漏改——
     编译器按定义体走，截断在这里发生）
  2. `spawnEnemy` 的 `const uint8_t ab0`
  3. `bossAbilities` 循环里的 `uint8_t bit`
  268 & 0xFF = 12（=SPLIT|HORN 的位），bit8=256 在传入瞬间被抹掉。
- **为何漏网**：这三个点都是"声明在 A 处、使用在 B 处"的截断形态；仿真第一轮 4 连败后才
  逼出第一处，其余两处是靠"把所有 `ab` 相关局部变量全搜一遍"的同族排查抓到的——
  **测试只暴露了路径上的一处，不是全部**。
- **修法**：三处全部升 `uint16_t`；`Enemy.abNextMs[8] → abNextMs[9]`。
- **回归**：`sim/run.cpp` §22——L11 王触发 SIEGE、圈内建塔被拒、圈外通过、到期解除。

### P1-1　SIEGE 持续时间和重放节拍共用一个数 → 封锁几乎永久 ✅已修

- **现象**：第一版 SIEGE_MS=4000 同时当"持续 4s"和"每 4s 重放"。王每 4s 重放一次，
  而 siegeMs 在第 4s 到期前的最后一帧即被重置 → 玩家眼里封锁框几乎不灭。
- **根因**：把"一个状态能存多久"和"这个状态多久再来一次"当成一个参数——本工程第二次
  犯这个错（v0.4.1 的 BOSS 节拍是同族：一个计时器驱动多个行为）。
- **修法**：拆成三个常量并写进 `layout.py`：`SIEGE_MS=3500`（持续）、`SIEGE_R=180`
  （半径）、`SIEGE_IV=10000`（节拍），注释里记录"曾把两者都设 4000 → 封锁几乎永久"。
- **回归**：§22 断言 3500ms 后封锁到期、`siegeBlocksPx()` 圈内 true / 圈外 false。

### P1-2　siegeMs 在通关画面不递减 ✅已修

- **现象**：带着激活的封锁进通关/死亡画面，封锁状态永远挂着（视觉上无感，但状态脏）。
- **根因**：`gameUpdate` 的 siegeMs 递减写在状态门控（`state != ST_WAVE && != ST_READY`
  提前 return）**之后**——overlay 期间整个函数直接返回。
- **修法**：递减移到两个 return 之前（封锁是全局时钟，与波次状态无关）。
- **回归**：§22 覆盖（ST_CLEAR 后 siegeMs 仍归零）。

### P2-1　layout.py 断言把 uint16 当 uint8 校验 ✅已修

- L11 ability=268 直接触发 `L11 ability out of range`（旧断言 `< 256`）。
- **修法**：`LevelDef.ability` uint8_t → uint16_t，断言放宽到 `< 512`（留一个技能位的
  增长空间），模板新增 SIEGE 输出。这是本工程"位掩码一扩宽，所有宽度跟着升"纪律的第三次应用。

### P2-2　中文渲染链路的首跑坑 ✅已修

- `gen_cnfont.py` 首跑 `ModuleNotFoundError: No module named 'PIL'`（系统 python3 无 Pillow）。
- **修法**：生成器加多平台字体候选列表（macOS STHeiti/Hiragino、Linux Noto/文泉驿）+
  `CN_FONT` 环境变量覆盖；本地跑改用隔离 venv；CI 里 `pip install Pillow`。
- 产出：从 `i18n.h` 的 STR_CN 表自动收 92 个字，渲染 16×16 二值化，`src/cn_font.h` 17.7KB，
  UTF-8 转义保证 ASCII 安全。**零素材铁律保持**：中文是生成的点阵，不是字体文件。

### P2-3　定义/宏出现在使用之后（3 处编译错误）✅已修

- `drawSiege` 定义在文件末、`renderFrame` 里调用 → 加前向声明。
- `ENDLESS_BTN_*` 几何宏在 `drawLevelClear` 之后定义 → 移到文件头。
- `botUpgradeSplit` 定义在 `botBuildWave` 之后 → 用脚本把整块剪切到定义前。
- **教训**：单 TU 工程里"用到才声明"全靠写码顺序，新函数落笔时就要放在调用点之前。

### P3-1　测试自身的坑（不是产品 bug，但各烧了一轮 FAIL）✅已修

1. **无尽入口测试错用 L10**：20 关的终关是 L20；且 `enterEndless` 走解锁校验是对的，
   测试要设 `g_unlocked=0xFFFFFFFF` 才模拟"已通关"。
2. **宽度断言语义理解错**：`px_textWidth` 的语义是"总宽减最后一个字符缝"（ASCII 路径
   本就如此），我按含缝宽度算期望 → 中文路径期望改为 33/11/22。
3. **endBest 不更新（显示 5）**：测试手工设 `g_w.wave=8` 时 `readyLeftMs` 是 memset
   遗留的 0，被当成"倒计时归零"立刻自动开波 → 测试补 `g_w.readyLeftMs = 0xFFFFFFFFu`。
   这正是 v0.4.1 FIXLOG 经验 5 的复读：**断言失败先怀疑断言**。

### P3-2　难度判据数据化（x2gold 扫描）✅已完成，结论写入 PLAYTEST.md

- L14–L20 双倍金币重跑：**七关全部 PASS**，结余 3,296–7,619 金币（见 `docs/PLAYTEST.md`）。
- **结论：这七关是"经济卡死"不是"输出结构性问题"**——不需要削弱数值；真人攻略是"敢花钱"
  （bot 的保守建塔留了 3k+ 余量）。同时 bot 的结余区间说明它仍偏保守，可作为后续标尺。

## 三、验证（全部真实执行过）

| 检查 | 结果 |
|---|---|
| `g++ -std=c++17 -O2 -I sim/compat -I src sim/run.cpp -o sim/sim && ./sim/sim` | **203 passed, 0 failed**（v0.4.1 基线 178；新增 §19–§24 共 25 条） |
| `python3 tools/layout.py; echo GEN_EXIT=$?` | `GEN_EXIT=0`（新增 SIEGE 三常量 + L11 ability=268） |
| `python3 tools/gen_cnfont.py`（隔离 venv，Pillow） | 输出 `src/cn_font.h` 与入库一致；CI 新增漂移检查步骤 |
| `python3 tools/gen_font.py && python3 tools/gen_sfx.py` | 输出与入库逐字节一致（新增 GUN2/GUN3 音色已入库） |
| 门禁关 bot | L1–L4 + L11–L13 仍全通；非门禁关失败集合与 v0.4.0 **完全一致**（分兵升级未改变难度读数） |
| `./sim/sim --x2scan` | L14–L20 双倍金币**全部 PASS**（详见 PLAYTEST.md） |
| 依赖资产存档 | `~/esp32/vendor/` 两个 tarball（esp32-core-3.3.10.zip / arduino-libs-20260519）已存，sha256 记入 BUILDING.md/.CN |
| `pio run -e tab5` | 编译+链接成功：RAM 37,248 B（7.3%）/ Flash 833,808 B（26.5%）（v0.4.1：37,016/819,586；+1,232 B RAM ≈ abNextMs[9] + siege 状态，+14,222 B Flash ≈ cn_font.h 17.7KB + i18n 表 + GUN2/GUN3 PCM）。最后一步 size 检查被本机沙箱拦（与 v0.4.1 同型），firmware.bin 由 esptool elf2image（DIO@80m）从 ELF 重建 |
| 合并镜像 | `releases/pixel-tower-defense-v0.4.2-tab5-merged.bin`，944,272 B，sha256 `b5e70110…d5f5`；四段头与 v0.4.0/v0.4.1 布局逐字节一致（boot e903024f/mode2 / parts aa50 / boot_app0 0x01 / app e907024f，DIO） |

## 四、经验（下次别再交学费）

1. **位掩码扩宽的第三条漏网点：局部变量。** v0.4.1 扩 8→9 位改了字段和前向声明，
   这次漏了 3 个 `uint8_t` 局部/循环变量——其中一个（`bossAbilityOf` 定义体）前向声明
   明明是 uint16_t，定义体却还是 uint8_t，**编译不报错，行为静默截断**。
   **规则：改宽度时按类型名全文搜索（`uint8_t` × 该语义的所有名字），不是改"知道的那几处"。**
2. **"持续/节拍"必须是两个参数。** SIEGE 和 v0.4.1 的 BOSS 技能节拍是同一个病：
   一个计时器既当持续时间又当重放周期。**规则：任何周期性效果落地时先问"这个数在管
   几件事"，一件事一个常量。**
3. **全局时钟不要藏在状态机门控后面。** siegeMs 递减被 `gameUpdate` 的提前 return 挡住，
   因为它是"跟着波次状态递减"的写法。**规则：与玩法状态无关的计时（封锁/BUFF 全局时钟）
   放门控之前。**
4. **中文渲染的最小侵入路径**：`px_text` 遇 UTF-8 高位字节自动切 16×16 字库（固定尺寸、
   17px 步进），ASCII 路径一行未动——双语不是重写渲染器，是在文本入口加分流。
   生成器从字符串表自动收字，加文案忘跑生成器=CI 红，和 config_gen 同一纪律。
5. **测试环境坑优先于产品 bug排查。** 本轮 3 个 FAIL（L10 vs L20、宽度语义、readyLeftMs
   遗留值）全是测试自己的设置错误。**规则：先打印被测对象的实际状态再改代码。**
6. **版本号小步走的实践**：一个 patch 号装下了启动界面+双语+引导+无尽+音色+新技能+数据化
   七件事——版本号是发布记号不是功能标尺，用户明确要求"别迭代太快"时就这样打包。

## 五、下次开工提示词（整份复制粘贴）

```
接续 pixel-tower-defense（M5Stack Tab5 / ESP32-P4 像素塔防，零素材铁律）。
v0.4.2 已实现"启动界面+双语+引导+无尽入口+SIEGE+音色+bot 分兵+难度数据化"，
先读 docs/FIXLOG-v0.4.2.md 再动手。

【树的位置】
- 本目录 = 公开仓克隆（github.com/andjiang0083/pixel-tower-defense），v0.4.2 的改动
  全部在工作树（git status 有 M/??），仿真 203 全绿，**未编译、未 commit、未 push**。
- 作者开发树 ~/esp32/tab5-tower-defense、发布镜像树 ~/esp32/pixel-tower-defense-public
  （只准 tools/sync-public.sh 单向同步）。

【已验证事实（2026-10-12，复跑命令在 docs/HANDOFF-PROMPT.md）】
- sim：203 passed / 0 failed（v0.4.1 是 178；新增 §19-§24 全在 sim/run.cpp）
- 生成器：layout.py GEN_EXIT=0；gen_cnfont.py（需 Pillow，多平台字体候选/CN_FONT 覆盖）、
  gen_font、gen_sfx 输出与入库一致，CI 查四处漂移（含中文点阵）
- 门禁关 L1-4 + L11-13 bot 全通；非门禁关失败集合与 v0.4.0 完全一致
- 难度：./sim/sim --x2scan → L14-L20 双倍金币全 PASS（经济卡死非结构问题，结论在
  docs/PLAYTEST.md，未削弱任何数值）

【v0.4.2 修了什么（细节见 FIXLOG 第二节）】
P0：3 个 uint8_t 截断点（bossAbilityOf 定义体/spawnEnemy ab0/bossAbilities bit）——
    268&0xFF=12，L11 王 SIEGE 静默失效；已全升 uint16_t，仿真 §22 有回归。
P1：SIEGE 持续/节拍拆成 SIEGE_MS=3500 / SIEGE_IV=10000（曾共用 4000 → 封锁永续）；
    siegeMs 递减移到 gameUpdate 状态门控之前。
P2：layout ability 断言 <256→<512 + uint16；gen_cnfont 多平台字体+venv；3 处
    "定义在使用后"编译错误；测试自身 3 个坑（L20 不是 L10、宽度语义、readyLeftMs）。
P3：x2gold 扫描结论入 PLAYTEST.md。

【规矩（沿用，别重犯）】
- 改数值只改 tools/layout.py，然后 python3 tools/layout.py，必须 GEN_EXIT=0。
- 零素材铁律：固件里不许出现图片/音频文件；中文点阵是生成物，改文案必须重跑
  gen_cnfont.py（否则 CI 漂移红）。
- 位掩码/位图一扩宽，按类型名全文搜所有缓存/局部变量/掩码。
- 持续时间和重放节拍必须是两个常量；全局时钟放状态机门控之前。
- 末波绝不压叠；静态计时器绑 wave 并在 levelLoad 复位。
- 版本号小步走（patch 号），一版一个 CHANGELOG 条目。
- 完成 = 有真实执行输出；提交用 git add <具体路径>，禁止 git add -A。
- 不用串口调试；真机验收靠开机横幅自报版本行。

【建议的下一步（按风险排）】
1. pio run -e tab5 编译（注意沙箱可能拦 size 检查的文件操作，备选 esptool elf2image
   --flash_mode dio 补齐 firmware.bin）→ merge_bin 合成 v0.4.2 merged → sha256。
2. 真机刷入+回读（端口 /dev/cu.usbmodem1101，P4 偶发重枚举需重试）；验收点：开机
   第一屏是标题界面（不是选关）、语言按钮切换中英文、首次进 L1 有三步引导、L20 通关
   画面有无尽按钮、L11 王 SIEGE 红框封锁。
3. commit + push（token 需用户重新提供，2026-10-12 那个已失效）。
4. 用户明确要求后才建 GitHub Release v0.4.2（沿用 v0.4.1 纪律）。
5. 后续建议：真人试玩回收 L11-L20 失败波次；README 首图换真机实拍；无尽模式深度调平衡。
```

## 六、遗留事项（需要你拍板/外部条件）

1. **未刷机/未 push/未建 Release**：v0.4.2 已编译打包（merged 944,272 B，sha256 `b5e70110…d5f5`，
   见 §三）、已本地 commit（`987a657`，30 个文件），但**尚未刷真机、尚未 push、未建 Release**。
   刷机验收点见上第五条。
2. **push 需要新 token**：2026-10-12 提交时用的 GitHub token 已失效，需要用户重新提供。
3. **GitHub Release v0.4.2**：按 v0.4.1 纪律，等用户明确发话再建。
4. **M5Burner 条目**（vid `2109325922863968258`）仍在审核，更新需要用户账号操作。
5. **真人试玩回收**：L11–L20 第几波崩、引导三步是否讲清了玩法、中文 HUD 在真机上的
   可读性（16×16 点阵的笔画密度）——硬件在线（`/dev/cu.usbmodem1101`）。
