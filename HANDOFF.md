# HANDOFF — tab5-tower-defense（历史详述）

> ⚠️ **状态块（2026-10-12 17:00 CST，覆盖本文下方一切旧描述）**
> **新会话的权威提示词已迁到 [`docs/HANDOFF-PROMPT.md`](docs/HANDOFF-PROMPT.md) —— 要唤起新会话就贴那一份，不要再贴本文件。**
> 本文件保留为**发布前的详述存档**（含 v0.4 开发期细节）；与提示词文件冲突时，**以 `docs/HANDOFF-PROMPT.md` 为准**。
>
> 已发生、本文尚未反映的变化：项目已**命名 PIXEL TOWER DEFENSE**、已**开源**（github.com/andjiang0083/pixel-tower-defense，MIT，release v0.4.0 已挂 8 个资产）、
> 已**提交 M5Burner**（PENDING 审核，fid `2109325922859773953` / vid `2109325922863968258`）、
> 已出**双语宣传图**（`releases/cover-*.png`、`releases/poster-*.png`，`tools/mkposter.py` 可重出）、
> 已有**开发树→公开树单向同步**纪律（`tools/sync-public.sh`）与**依赖钉版**（M5Unified 0.2.25 / M5GFX 0.2.32）。
>
> **v0.4.1（2026-10-12）：** 对 v0.4.0 做了一轮全量 code review，
> 修掉 2 个真 bug（NVS 去重缓存整型截断 → L17-20 通关画面每帧写 flash；
> 重打刷高星级静默丢失）+ BOSS 技能节拍劫持 + 6 类设计/工程问题，
> 仿真断言 165 → 178。**问题/修法/经验/下次提示词全部在
> [`docs/FIXLOG-v0.4.1.md`](docs/FIXLOG-v0.4.1.md)**（那份是 v0.4.1 的论据库，
> 本文件与它冲突时以 FIXLOG + HANDOFF-PROMPT 为准）。v0.4.1 已**刷入真机回读验证**
> （ESP32-P4 rev 1.3，固件本体逐字节一致，NVS 旧存档保留）、已 **push 到 GitHub**
> （`26439e4`，CI 实跑通过）、已**建 GitHub Release v0.4.1**（tag 指向 `26439e4`，
> 8 资产，merged bin 下载回读 sha256 一致）；**尚未更新 M5Burner**（条目审核中，
> 仍指向 v0.4.0）。
>
> **v0.4.2（2026-10-12）：** 用户点名"启动界面 + 好看标题 + 版本 + 语言选择"，
> 并把 v0.5.0 建议清单剩余条目全部做完压进 patch 号（不跳 0.5.0）：开机标题界面
> （星空+彩虹像素字+版本+语言按钮）、全 UI 双语（`src/i18n.h` + 16×16 中文点阵
> `src/cn_font.h`，`tools/gen_cnfont.py` 生成）、首次三步引导 overlay、无尽模式入口
> （L20 通关画面 + NVS 最高波次）、机枪三音色轮换、第 9 个 BOSS 技能 SIEGE
> （空间封锁建造区，L11 首现）、bot 分兵升级、L14–L20 难度数据化（x2gold 扫描
> 七关全 PASS = 经济卡死非结构问题）。抓修 3 个 uint8_t 截断真 bug（L11 王 SIEGE
> 静默失效）+ SIEGE 持续/节拍混淆。仿真 **178 → 203**。**档案在
> [`docs/FIXLOG-v0.4.2.md`](docs/FIXLOG-v0.4.2.md)**。
> **v0.4.2 状态：已全部完成并推送 GitHub**（2026-10-12 晚，仿真 203、merged 944,272 B、
> 真机刷入目视确认、CI 首跑红已修）。剩余仅 **GitHub Release v0.4.2 等用户发话**、
> M5Burner 更新（审核中）、开发树 sync-public.sh 同步。详见
> [`docs/FIXLOG-v0.4.2.md`](docs/FIXLOG-v0.4.2.md)。

---

## 给 AI 的开场白（直接复制）

```
接续 ~/esp32/tab5-tower-defense（M5Stack Tab5 / ESP32-P4 像素塔防，零位图零音频文件：
纯 fillRect 画图 + tools/gen_sfx.py 程序化合成音效）。当前 v0.4.0，已编译并刷机。

【单一真源】tools/layout.py → 生成 src/config_gen.h（20 关 / 9 兵种 / 5 塔种 / 禁建掩码 BLOCKED）。
改数值/关卡/地图只改 layout.py，然后 `python3 tools/layout.py`，**必须先确认 GEN_EXIT=0**
（validate() 会因"难度非单调""路格打不到""最少塔数 > MAX_TOWERS"拒生成，重定向日志会静默失败 §380）。

【v0.4.0 现状（真实执行过）】
- 20 关：L1-L10 老图；L11-L20 按"空间难度"新哲学设计（band D 可建带 3→1 + 禁建岩层）。
- BOSS：8 技能位掩码（HEAL/BROOD/SPLIT/HORN/EMP/GREED/MOLT/GHOST）+ mid-boss（一关多王）+ 双王。
- PC 仿真：`g++ -std=c++17 -O2 -I sim/compat -I src sim/run.cpp -o sim/sim && ./sim/sim`
  → **165 passed / 0 failed**；门禁关 = L1-L4 + L11-L13（bot 必通），其余关 bot 打不过只记录。
- 地图审计：`python3 tools/map_design.py` → 20 关指标表（最少塔数 5→18 ≤ MAX_TOWERS=20、零死角）
  + 热力图 shots/preview-map-lvNN.png + 拼图 shots/preview-maps-sheet.png。
- 固件：`pio run -e tab5` SUCCESS（RAM 35,472B / 6.9%；Flash 819,150B / 26.0%）。

【已知未完成（建议按这个顺序接）】
1. 无尽模式 UI 未接线：引擎侧 g_endless / lvWaves / lvDiff / ENDLESS_* 都就绪，
   但「L10 CLEAR → 点屏进无尽」「选关界面 ENDLESS 节点」「GAMEOVER 显示 BEST WAVE（NVS 存）」都没做。
2. 技能视觉只做了部分：幽灵半透明棋盘、EMP 黑烟塔、号角金环、抢钱红字、蜕壳裂纹——真机观感待评审。
3. 双路关 L14/L15/L16/L20 的仿真 bot 未通过（真人可打，bot 不会分兵）——**bot 不是难度标尺**（§382/§387）。
4. 手感/难度：真机 20 关需要一轮实测，重点看"新图是否真的逼出行间位取舍"。

【规矩（踩过的坑，别重犯）】
- 真机不串口调试：靠 PC 仿真 + 重渲染 + 截图看结构（PIL 数值统计），改完先仿真再刷。
- 刷机必须显式端口：`--upload-port /dev/cu.usbmodemXXXX`（编号随 USB 口变，先 `ls /dev/cu.usbmodem*`；
  自动选端会挑中蓝牙口失败）。刷机等用户说"插好了，刷机吧"。
- 提交：`git add <具体路径>`，**禁止 `git add -A`**（工作区常有实验改动，§267）。
  仓库 ~/esp32 暂无 remote（本地提交）；作者署名 andjiang0083。
- 版本号厌恶膨胀：小步迭代，一版一个 patch 号，不跳号。
- 压叠铁律：末波绝不压叠（否则永远到不了 ST_CLEAR）；static 计时器绑 wave。
- 回复一律中文；用户常以一个词批准（"做吧"），不必二次确认。
```

---

## 一、项目定位与硬件

- 目标机：M5Stack **Tab5**（ESP32-P4，1280×720 横屏，`M5.Display.setRotation(3)`）。
- 硬约束：**零位图、零音频文件**——所有画面是 `fillRect` 拼的像素图；音效由 `tools/gen_sfx.py`
  合成成 `src/sfx_data.h` 的采样数组。
- 帧缓冲在 PSRAM（DSI video mode）；`psramFound()` 为假直接停住不空转。
- 30fps（`FRAME_MS=33`）；HUD 走 `hudDirty` 按需重画。

## 二、代码地图（谁负责什么）

| 文件 | 责任 |
|---|---|
| `tools/layout.py` | **唯一真源**：网格/塔表/兵种表/经济/关卡表/调色板/主题/禁建带 band/审计 `audit_level()`；生成 `src/config_gen.h` |
| `tools/map_design.py` | 地图审计器：贪心集合覆盖算"最少塔数"、宝地/死角、出热力图 PNG 与 20 关拼图 |
| `tools/gen_sfx.py` | 程序化音效 → `src/sfx_data.h`（HORN/EMP/SPLIT 等） |
| `src/game.h` | 世界状态 `g_w`、关卡装载、波次编成、击杀收口 `killEnemy()`、BOSS 技能引擎、无尽参数 |
| `src/render.h` | 全部绘制：格子三态（草/路/岩）、塔、敌人、HUD、面板、选关、结算 |
| `src/actions.h` | 触摸处理：建塔/升级/卖出/开波/静音 |
| `src/main.cpp` | 启动、NVS 进度（unlock/clear/stars/stars2/mute）、主循环 |
| `sim/run.cpp` | PC 仿真（跑的是同一份 game/render/actions/touch）+ bot 自动通关回归 |

## 三、常用命令

```bash
cd ~/esp32/tab5-tower-defense

# 1) 真源 → 头文件（务必看 GEN_EXIT）
python3 tools/layout.py; echo "GEN_EXIT=$?"

# 2) 地图审计（指标表 + shots/preview-*.png）
python3 tools/map_design.py

# 3) PC 仿真回归（20 关 bot）
g++ -std=c++17 -O2 -I sim/compat -I src sim/run.cpp -o sim/sim && ./sim/sim | tail -30

# 4) 固件编译 / 刷机
pio run -e tab5
ls /dev/cu.usbmodem*                      # 先看端口
pio run -e tab5 -t upload --upload-port /dev/cu.usbmodem2101
```

## 四、v0.4.0 交付清单（已核验）

- [x] 20 关（L11-L20 新地图：GLASS FIELDS / HAIRPIN RIDGE / SAND COIL / CROSSING FIRE /
      COMB LINE / TWIN RAVINE / LONG SERPENT / STONE WEAVE / GRINDER TWO / THE LAST WALL）
- [x] 禁建岩层：`BLOCKED[20][14]` 位掩码 → `g_w.blocked[][]` → `buildTower` 拒绝 + 深灰乱石渲染
- [x] `validate()` 覆盖审计断言（可玩 + 可通）；`tools/map_design.py` 20 关全 PASS
- [x] 20 关解锁位图 uint32 修复（原 uint16 导致第 17 关起永远解不开）
- [x] 选关界面 5×4=20 节点；星级拆 stars/stars2 两键
- [x] 仿真 165 checks / 0 failed；固件编译 SUCCESS（RAM 6.9% / Flash 26.0%）
- [x] 经验沉淀 §384-§388（地图指标口径、band 雕刻、集合覆盖起点、双路乘数、位宽陷阱）

## 五、下一步的候选（按性价比排序）

1. **无尽模式 UI 接线**（引擎已就绪，只差入口/HUD/存档三处）。
2. **技能视觉评审**：幽灵半透明棋盘、EMP 黑烟塔在真机上的辨识度。
3. **L14/L15/L16/L20 双路关难度**：先真人试玩，再决定是"加第二条咽喉"还是"承认它就该这么硬"。
4. **卖塔搬家的正反馈**：新哲学下"位置"是决策，可考虑拆除动画/退款提示强化这个循环。

## 六、踩坑速查（完整版见 ~/esp32/ESP32-经验沉淀.md §380-§388）

- §380 `layout.py` 静默失败：重定向日志时也要读 `GEN_EXIT`。
- §381 末波压叠 → 永远到不了 `ST_CLEAR`；static 计时器必须绑 wave 并在 `levelLoad` 复位。
- §382 改经济/难度后先问"仿真 bot 的行为模型还配当标尺吗"——bot 失败 ≠ 难度合理。
- §384 地图难度指标必须是**预算量**（最少塔数），不是潜在覆盖量。
- §385 禁建区不手画：用 band(D) 生成 + 断言兜底。
- §386 集合覆盖起点必须是**全部路格**（用"已满足集"当起点 = 检测永真）。
- §387 双路图是难度乘数，排序上必须排在单路图之后。
- §388 扩容前先扫位图/打包字段的位宽（uint16 解锁图 = 只能玩到第 16 关）。

## 七、公开仓与发布（v0.4.0 起）

- 公开仓：**https://github.com/andjiang0083/pixel-tower-defense**（MIT，双语文档）。
- 本地是「开发树 + 导出树」两份：开发树 `~/esp32/tab5-tower-defense/`（本文件所在，
  仍在大仓 `~/esp32` 中被跟踪，其历史不动），导出树 `~/esp32/pixel-tower-defense-public/`
  （= 公开仓的工作副本，只放可公开内容）。
- 同步（在开发树里执行，别在公开树里手改任何东西）：
  ```bash
  tools/sync-public.sh          # rsync 开发树 → 公开树，含文档与生成物
  ```
  脚本排除了 `.pio/`、`sim/sim`、`*.ppm`、`releases/*.bin`（发行二进制走 Release 附件，
  只在公开树里有 `releases/*.sha256` 与 `releases/README_M5Burner.md`）。
- 发行件（每次发版重做，不留旧版本）：
  ```bash
  cp .pio/build/tab5/firmware.factory.bin releases/pixel-tower-defense-vX.Y.Z-tab5-merged.bin
  shasum -a 256 releases/*-merged.bin > releases/*-merged.bin.sha256
  ```
  Tab5 的 bootloader 在 **0x2000**（不是 0x0）、`--flash-mode dio`（PIO 实际就是这个，
  别看 platformio.ini 里写的 qio）——合错了就是 M5Burner 刷完黑屏。
- 商店条目：M5Burner 作者名 `andjiang`；封面用 `releases/cover-1280x720-*.png`
  （1280×720 正好是网页版封面画布尺寸）。
