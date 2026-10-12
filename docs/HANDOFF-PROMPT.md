# 新会话唤起提示词 — PIXEL TOWER DEFENSE（Tab5 像素塔防）

> **这是本项目的权威交接文件**（单一真源）。需要新开一个会话继续干活时，**整份复制粘贴**即可。
> 聊天里若另有一版，以本文件为准（两份一漂移，下个会话就不知道听谁的）。
> 状态块核实于 **2026-10-12 晚间 CST**，**覆盖本文下方及 `HANDOFF.md` 里更早的一切描述**。
> **v0.4.1 已刷入真机并回读验证**（ESP32-P4 rev 1.3，2026-10-12；固件本体逐字节一致，NVS 旧存档按设计保留）。
> **v0.4.2 已实现"启动界面+双语+引导+无尽入口+SIEGE+音色+bot 分兵+难度数据化"，仿真 203 全绿，但尚未编译/commit/push/建 Release** —— 改动全在工作树（`git status` 可见 M/??）。
> **v0.4.1 / v0.4.2 两轮的档案（问题/修法/经验）在 [`FIXLOG-v0.4.1.md`](FIXLOG-v0.4.1.md) / [`FIXLOG-v0.4.2.md`](FIXLOG-v0.4.2.md)** —— 动手前先读 v0.4.2 那份。

---

## 粘贴给新会话的提示词（从这里开始，到文件末尾都可以照抄）

继续一个已经发布出去的项目，别从零调研。先读本文件，再动手。

### 0. 一分钟起步（设备 / 代码 / 构建）

| 项 | 值 |
|---|---|
| 设备 | **M5Stack Tab5**（ESP32-P4，1280×720 触摸屏，16MB flash / 32MB PSRAM） |
| 开发树（真源） | `/Users/jiangweizhong/esp32/tab5-tower-defense/`（PlatformIO 工程） |
| 发布镜像树 | `/Users/jiangweizhong/esp32/pixel-tower-defense-public/` = **github.com/andjiang0083/pixel-tower-defense 的工作副本**，只准用 `tools/sync-public.sh` 从开发树单向同步，**禁止手改** |
| 大仓 | `~/esp32/`（该项目被它跟踪，但**已不再维护**：42 项改动未提交，**别在大仓里找最新代码**） |
| 构建 | `cd ~/esp32/tab5-tower-defense && pio run -e tab5` |
| 刷机 | `pio run -e tab5 -t upload --upload-port /dev/cu.usbmodem2101`（**端口号会变**：刷前先 `ls /dev/cu.usbmodem*`；串口调试被否决，见附录 B） |
| 真机验收 | 开机横幅自报版本，脚本靠这行核对"刷进去的是哪份配置"：`=== PIXEL TOWER DEFENSE v0.4.1 | board=22 | 1280x720 ===`（注意 `board=%d` 打的是 M5Unified 枚举值 **22=Tab5**，不是字符串 "Tab5"——旧文档写成 board=Tab5 是错的）。**v0.4.2 起横幅号会变**，且开机第一屏是**标题界面**（不是选关）：星空 + 彩虹像素字 + 金色版本号 + EN/中文按钮 |
| PC 仿真（比真机快 1000 倍，先在这里验） | `g++ -std=c++17 -O2 -I sim/compat -I src sim/run.cpp -o sim/sim && ./sim/sim` |
| 难度扫描（v0.4.2 新增） | `./sim/sim --x2scan`：bot 起手金币 ×2、lives=99999 重跑 L14–L20，判"经济卡死 vs 输出结构性问题" |
| 发布门禁 | `python3 ~/esp32/tools/release.py tab5-tower-defense --strict`（校验 merged 镜像结构 + 封面尺寸 + 卡片字段） |
| CI（v0.4.1 新增，v0.4.2 扩到四处漂移） | `.github/workflows/sim.yml`：每次 push/PR 自动跑仿真 + config_gen/pixel_font/sfx_data/**cn_font** 四个生成物漂移检查 |

### 1. 几棵树、几个真源（先认清这个，否则每条任务都会落到错的地方）

1. **数值/关卡/地图真源 = `tools/layout.py`（唯一）**。它生成 `src/config_gen.h`（还有 `src/pixel_font.h` ← `tools/gen_font.py`、`src/sfx_data.h` ← `tools/gen_sfx.py`、**`src/cn_font.h` ← `tools/gen_cnfont.py`（v0.4.2 新增，需 Pillow：`pip install Pillow`；多平台字体候选，`CN_FONT` 环境变量可覆盖；从 `src/i18n.h` 的 STR_CN 表自动收字）**）。
   **改数值只改生成器，然后必须回读 `GEN_EXIT=0`**（生成器自己会跑一致性校验），改完再编译。
   手改 `src/*_gen.h` 一律无效（下次生成即被覆盖）。**改中文文案必须重跑 gen_cnfont.py**，否则 CI 漂移红。
2. **代码真源 = 开发树**；公开树是它的镜像（`tools/sync-public.sh` 单向 rsync；公开树有独立 `.git`，`main` 与远端 `5eced67` 一致）。
3. **商店文案真源 = `~/esp32/tools/store-copy.json`**（中英双语，`python3 ~/esp32/tools/copycheck.py` 体检）+ 项目内 `publish.yaml`（发布卡片）。

### 2. 已核实事实（都自带复跑命令，别凭记忆改数字）

| 事实 | 复跑命令 | 核实值（2026-10-12） |
|---|---|---|
| 全部关卡可玩、机器人能过门禁关 | `./sim/sim` | **203 passed, 0 failed**（`RESULT` 行；v0.4.1 是 178，v0.4.2 新增 25 条断言：标题/语言/引导/无尽入口/SIEGE/音色/中文宽度，见 FIXLOG-v0.4.2 §三） |
| 生成器与源码一致（无脏漂移） | `python3 tools/layout.py; echo GEN_EXIT=$?` | `GEN_EXIT=0`（v0.4.2 新增 SIEGE 三常量 + L11 ability=268） |
| 中文点阵与 i18n.h 一致 | `python3 tools/gen_cnfont.py && git diff --exit-code src/cn_font.h`（需 Pillow） | 零漂移（92 字，17.7KB，CI 检查） |
| L14–L20 难度判据（v0.4.2 数据化） | `./sim/sim --x2scan` | **七关双倍金币全 PASS**（结余 3,296–7,619）= 经济卡死非输出结构性问题；未削弱任何数值；明细 `docs/PLAYTEST.md` |
| 构建占用（最新源码） | `pio run -e tab5` | 见 README 验证表（v0.4.1 实数：RAM 37,016B / Flash 819,586B；**v0.4.2 未编译，跑完更新**） |
| 发行镜像结构（Tab5 的 bootloader **在 0x2000，不是 0x0**） | `python3 -c "import pathlib;b=pathlib.Path('releases/pixel-tower-defense-v0.4.1-tab5-merged.bin').read_bytes();print(len(b),b[0x2000:0x2004].hex(),b[0x8000:0x8002].hex(),b[0x10000:0x10004].hex())"` | `929360 | boot e903024f | parts aa50 | app e907024f`（DIO） |
| 发行件哈希 | `shasum -a 256 releases/pixel-tower-defense-v0.4.1-tab5-merged.bin` | `d018587ba897a69b27e7f44a7918679ef52391eec8fcf3e183dbc50cd077aa81` |
| 真机刷入+回读（2026-10-12） | `esptool.py --chip esp32p4 --port /dev/cu.usbmodem1101 --baud 460800 write_flash 0x0 releases/pixel-tower-defense-v0.4.1-tab5-merged.bin`，然后 `read_flash 0x0 929360` 回读 | **固件本体（0x0-0x8FFF / 0xE000-0xE2FFF）逐字节一致**，app 段 sha256 两侧同为 `682b739d…`；NVS 区（0x9000-0xDFFF）与镜像的 0xFF 空隙有 62 字节差=**旧存档被保留**（esptool 压缩写入跳过全 FF 块），开机横幅 `v0.4.1 | board=22 | 1280x720`，旧进度 `unlock=0007 cleared=0003` 被正确读入 |
| 全新克隆能构建（唯一算数的"开源可用"承诺） | `git clone … /tmp/x && cd /tmp/x && pio run -e tab5` + `./sim/sim` | 已实测通过（165/0，占用同上） |
| GitHub 已公开 | `curl -s https://api.github.com/repos/andjiang0083/pixel-tower-defense` | MIT / main / topics 已设 |
| GitHub Release v0.4.1（8 资产，2026-10-12 发布） | `curl -s https://api.github.com/repos/andjiang0083/pixel-tower-defense/releases/latest` | `draft:false`，含 merged bin（929,360 B，sha256 `d018587b…`，已从 Release 实际下载回读比对一致）/ 封面 / 海报 / LICENSE / CREDITS / THIRD-PARTY-NOTICES |
| M5Burner 已提交 | 登录态 `fetch('/api/v1/users/me/firmwares?pageSize=100')`（返回字段是 **`rows`**） | `PIXEL TOWER DEFENSE` · Tab5 · Games · **status=PENDING** · `fid=2109325922859773953` · `vid=2109325922863968258` |

### 3. 不许改的东西（这几条是用户拍板的设计约束，改之前必须先问）

- **四条卖点**（全部对外材料双语贯穿）：① 本机大模型写就（架构+大部分代码，4-bit 本地模型）② 魔性音效 ③ 小挑战的难度 ④ 小学生测试员亲手测试。
- **零素材铁律**：固件里**不允许出现任何图片/音频文件**。画面只用 `fillRect`，音效由 `tools/gen_sfx.py` 在构建期算成 PCM，字库是 `tools/gen_font.py` 里 95 个手写位图。宣传图也必须零外部素材（用真实帧 + 游戏自带字库 + 真实 PCM 波形，见 `tools/mkposter.py`）。
- **压叠铁律（§381）**：末波绝不与其他波压叠；任何静态/全局计时器必须绑 wave 并在 `levelLoad` 复位。
- **难度标尺（§382）**：bot 失败 ≠ 难度不合理。**关卡是预算题不是拼图**：每关要多条活路、失败可渐进挽救、禁"围棋化"唯一解。
- **版本号厌恶膨胀**：功能再多也只进小 patch 号（视觉大改也别跳到 1.2.0）。
- 依赖已钉精确版（`m5stack/M5Unified@0.2.25` + `m5stack/M5GFX@0.2.32`）——**别放宽成 `^`**：M5GFX 会飘最新，别人构建出的固件就与发布件不同。

### 4. 待办（按风险排：低风险在前，动构建/必须真机验证的单独列出）

0. **【发布·v0.4.2 主体待做】v0.4.2 改动全在工作树、仿真 203 全绿，但尚未走发布链**：
   ① `pio run -e tab5` 编译（沙箱可能拦 size 检查的文件操作，备选 esptool `elf2image --flash_mode dio` 补齐 firmware.bin）→ merge_bin 合成 v0.4.2 merged → sha256；
   ② 真机刷入+回读（端口 `/dev/cu.usbmodem1101`，P4 偶发重枚举需重试）；验收点：开机第一屏是标题界面、语言按钮切换中英文、首次进 L1 有三步引导、L20 通关画面有无尽按钮、L11 王 SIEGE 红框封锁；
   ③ commit + push（**需要用户重新提供 GitHub token**，2026-10-12 那个已失效）；
   ④ **GitHub Release v0.4.2 等用户明确发话再建**（沿用 v0.4.1 纪律）；
   ⑤ 合回开发树 → `sync-public.sh` 单向同步；M5Burner 更新条目（vid `2109325922863968258`，审核中，仍指向 v0.4.0）。
1. ~~【零风险·文档/数据】x2gold 扫描~~（**v0.4.2 已完成**：L14–L20 全 PASS = 经济卡死，明细 `docs/PLAYTEST.md`）。
2. ~~无尽模式入口 UI~~（**v0.4.2 已完成**：L20 通关画面进无尽 + NVS 最高波次 + 仿真断言；引擎早 ready）。
3. **【中风险·要真机】**真人试玩回收：请用户（或小学生）拿真机跑 L11–L20，记录"第几波崩、崩在哪条路"；顺带回收 v0.4.2 新界面（引导三步是否讲清、中文 HUD 可读性）。判据：能给出至少一关的真实失败波次与原因；据此调 `layout.py` 的经济/血量（改完 `GEN_EXIT=0` → 仿真 → 刷机）。**硬件现在就在线**。
4. **【中风险·要真机+相机】**README 首图换成 **Tab5 真机实拍**（`docs/screenshots/` 现在全是仿真渲染图）。硬件在场即可拍；v0.4.2 的标题界面也值得拍一张。
5. **【等外部】**M5Burner 审核结果：过审后卡片状态变 `PUBLISHED`，回写 `publish.yaml` 的 `readback:` 与 `store-copy.json`。判据：`release.py --strict` 通过 + 回读接口里状态不再是 PENDING。
6. **【可选·需要用户拍板】**是否把"本机模型写就"的过程细节（写成哪部分、提示词、返工率）单独写一篇博客/帖子——对外的话术要避开把模型名写进产品名（阿里商标风险）。

### 5. 动手纪律（本项目踩过，别重犯）

- **先讨论再动手**；大改动先落 `SPEC-*.md` 再改代码（回复会被截断，计划必须持久化）。
- **完成 = 有真实执行输出**：任何"已发布/已刷机"的结论都要给回读证据（远端 sha、CDN 对象元信息、接口状态）。
- **不要用串口调试**（已否决）。视觉/仿真优先；真机验收靠屏幕与开机自报行。
- **收尾顺序**：门禁全绿 → 提交并推送 → **回读远端 sha 确认落地** → 才写文档。
- 提交信息用文件传（`git commit -F`），别在 terminal 里用 heredoc 写中文提交信息（bash 求值会炸）。
- 凭据只走环境变量（`GITHUB_TOKEN` 在 `~/.hermes/.env`），推送用临时 credential.helper 注入，**绝不落盘进 `.git/config`**。
- 临时脚本放在 `~/.hermes/cache/scratch/`（**24 小时会被清理**，需要复用就挪进 `tools/`）：`ptd_gh.py`（建仓）、`ptd_push.sh`（推送）、`ptd_release.py`（建 release + 回读）、`ptd_retag.py`（重指 tag 并复发布）、`ptd_store_copy.py`（写商店文案）。

### 附录 A：30 秒体检（新会话开工第一件事）

> v0.4.2 的改动在公开仓克隆的工作树里：`/Users/jiangweizhong/WorkBuddy/QQwbb/pixel-tower-defense`
> （`git status` 有 M/?? 即 v0.4.2 未提交的改动）。跑下面命令前先确认自己在这个目录。

```bash
cd /Users/jiangweizhong/WorkBuddy/QQwbb/pixel-tower-defense
python3 tools/layout.py; echo "GEN_EXIT=$?"          # 期望 0
g++ -std=c++17 -O2 -I sim/compat -I src sim/run.cpp -o sim/sim && ./sim/sim | tail -1   # 期望 203 passed, 0 failed
./sim/sim --x2scan | grep -c PASS                     # 期望 7（L14-L20 双倍金币）
shasum -a 256 releases/pixel-tower-defense-v0.4.1-tab5-merged.bin   # 仍是 v0.4.1 的 d018587b…（v0.4.2 merged 尚未产出）
```

### 附录 B：已被否决的方案（别再提，括号里是机制）

- **串口调试/日志驱动开发**：用户明确否决。人眼在屏幕上看、仿真里跑，别指望 `idf.py monitor`（轻睡眠还会断 USB CDC）。
- **手改 `src/config_gen.h` / `pixel_font.h` / `sfx_data.h`**：生成物，下次生成即覆盖；改生成器。
- **给地图加"常驻 UI/网格线/条纹"**：用户审美的否决项（信息也事件化，不做常驻界面）。
- **把模型名写进产品名/商店标题**：商标风险，署名只放 `CREDITS.md` 与商店描述正文。
- **M5Burner 只传 app-only 的 `firmware.bin`**：平台永远写 `0x0`，会盖掉 bootloader → 黑屏。必须传 merged（`firmware.factory.bin` 改名成 `-merged.bin`）。
- **release 建好后再移动 tag 来"修正"指向**：删 tag 会让 release **异步**变 draft（API 当场读回还是已发布，几分钟后变 draft，`/releases/tags/<t>` 直接 404 → 对全世界不可见）。**根治：先 push 完最后一个提交，再建 release**；不得已动了 tag，就要 `PATCH {"draft": false}` 并在 +5s/+20s/+45s 三个时点、`/releases/<id>` 与 `/releases/tags/<t>` 两个口径都复读。
- **"仿真里顺手截图"**：长跑模式关着渲染，跑完导出的帧缓冲是**关卡开局那一帧**（空棋盘）。要图必须走 `SIM_SHOT=<lv> SIM_SHOT_WAVE=<w> SIM_SHOT_OUT=<path>` 的真渲染模式（内含 `lives>=15` 守门，避免拍到"已经输了"的帧）。
