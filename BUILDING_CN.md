# 构建与刷机

[English →](BUILDING.md)

在 macOS（Apple silicon）+ PlatformIO Core 上实测通过。Windows/Linux 同理，只有
串口名不同（`COM5` 而不是 `/dev/cu.usbmodem*`）。

## 1. 前置

```bash
brew install platformio        # 或 pipx install platformio
python3 --version              # 3.9+（只给生成器和 PC 仿真用）
```

**不需要** Arduino IDE，而且**用不了** TFT_eSPI：Tab5 是 MIPI-DSI 视频模式
（帧缓冲常驻 PSRAM、DMA 连续扫描输出），而 TFT_eSPI 只实现了 SPI 命令模式面板。
本项目把 M5GFX（LovelandGFX 分支）纯粹当作 `fillRect`/`drawPixel` 的画布用。

## 2. 编译

```bash
git clone https://github.com/andjiang0083/pixel-tower-defense.git
cd pixel-tower-defense
pio run -e tab5
```

v0.4.1 的期望输出：

```
RAM:   [=         ]   6.9% (used 35472 bytes from 512000 bytes)
Flash: [===       ]  26.0% (used 819150 bytes from 3145728 bytes)
```

`platformio.ini` 里有几项**别顺手"整理"掉**：

| 配置 | 为什么 |
|---|---|
| `board = esp32-p4-evboard` + `-DARDUINO_M5STACK_TAB5` | M5GFX 只有在这个宏下才会识别 Tab5 面板与 GT911 触控 |
| `board_build.psram = enable`，200 MHz | 帧缓冲在 PSRAM。不满足时 M5GFX 直接报 `M5Tab5 need PSRAM SPEED 200MHz` |
| `src/sdkconfig.h` | 覆盖 Arduino 库默认的 PSRAM 速度 |
| `board_build.partitions = app3M_fat9M_16MB.csv` | 3 MB 的 app 分区 |

## 3. USB 刷机

```bash
pio run -e tab5 -t upload --upload-port /dev/cu.usbmodem2101
pio device monitor            # 115200，开机横幅里有版本号
```

端口号会随 USB 口变化 —— 刷之前先 `ls /dev/cu.usbmodem*` 确认，并且**永远显式传
`--upload-port`**。猜端口是把固件刷进另一块板子的经典姿势。

### PlatformIO 真正发出去的命令（用 `pio run -v -t upload` 实测）

```
esptool --chip esp32p4 --port "/dev/cu.usbmodem2101" --baud 1500000 \
  --before default-reset --after hard-reset write-flash -z \
  --flash-mode dio --flash-freq 80m --flash-size detect \
  0x2000  .pio/build/tab5/bootloader.bin \
  0x8000  .pio/build/tab5/partitions.bin \
  0xe000  .../framework-arduinoespressif32/tools/partitions/boot_app0.bin \
  0x10000 .pio/build/tab5/firmware.bin
```

两个必须注意的点，都是 Tab5 的经典坑：

1. **bootloader 在 `0x2000`，不是 `0x0`**（ESP32-P4）。合并镜像如果把它放在 0x0，
   刷完就是黑屏。
2. **PIO 实际用 `--flash-mode dio`**，尽管 `platformio.ini` 里写的是 `qio` ——
   board 定义把它覆盖了。不要因为"ini 里写的是 qio"就用 qio 去合镜像。

## 4. 合并镜像（给 M5Burner / esptool 从 0x0 刷）

M5Burner 永远把文件写到**偏移 0x0** 并且会 `eraseAll`，所以它需要一整份 flash
布局，而不是只有 app 的 `firmware.bin`。

PlatformIO 12+ 已经直接产出这个文件：

```bash
cp .pio/build/tab5/firmware.factory.bin \
   releases/pixel-tower-defense-v0.4.1-tab5-merged.bin
shasum -a 256 releases/pixel-tower-defense-v0.4.1-tab5-merged.bin \
  > releases/pixel-tower-defense-v0.4.1-tab5-merged.bin.sha256
```

发布前先验各段位置（这一步就是用来拦"黑屏"的）：

```bash
python3 - <<'PY'
import struct, pathlib
b = pathlib.Path('releases/pixel-tower-defense-v0.4.1-tab5-merged.bin').read_bytes()
def hdr(off):
    return dict(magic=hex(b[off]), nseg=b[off+1], mode=b[off+2],
                size_freq=hex(b[off+3]), entry=hex(struct.unpack_from('<I', b, off+4)[0]))
print('len', len(b))
print('bootloader @0x2000', hdr(0x2000))   # magic 0xe9，mode 2 = DIO
print('partitions @0x8000', b[0x8000:0x8002].hex())  # aa50
print('boot_app0  @0xe000', hex(b[0xe000]))          # 0x01
print('app        @0x10000', hdr(0x10000))  # magic 0xe9，mode 2 = DIO
PY
```

v0.4.1 镜像的期望输出：

```
len 929360
bootloader @0x2000 {'magic': '0xe9', 'nseg': 3, 'mode': 2, 'size_freq': '0x4f', 'entry': '0x4ff29ed0'}
partitions @0x8000 aa50
boot_app0  @0xe000 0x1
app        @0x10000 {'magic': '0xe9', 'nseg': 7, 'mode': 2, 'size_freq': '0x4f', 'entry': '0x4ff00c6a'}
```
sha256 `d018587ba897a69b27e7f44a7918679ef52391eec8fcf3e183dbc50cd077aa81`

真要手搓（工程不产出 `factory.bin`，或编译后的 size 检查步骤被环境拦截——
v0.4.1 就是在编译+链接成功后被拦截，按下面两步搓的）：

```bash
python3 ~/.platformio/penv/bin/esptool --chip esp32p4 elf2image \
  --flash_mode dio --flash_freq 80m --flash_size 16MB \
  -o .pio/build/tab5/firmware.bin .pio/build/tab5/firmware.elf
python3 ~/.platformio/penv/bin/esptool --chip esp32p4 merge-bin \
  --output merged.bin --flash-mode dio --flash-freq 80m --flash-size 16MB \
  0x2000  .pio/build/tab5/bootloader.bin \
  0x8000  .pio/build/tab5/partitions.bin \
  0xe000  ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin \
  0x10000 .pio/build/tab5/firmware.bin
```

**不要**加 `--pad-to-size`/`--fill-flash-size`：合并镜像应该不到 1 MB，而不是 16 MB。

> ⚠️ 合并镜像会 `eraseAll`。已经配过网、NVS 里有凭据的设备刷合并镜像会把它们清掉；
> 想保留 NVS 原地升级，只写 app 分区
> （`esptool write-flash 0x10000 .pio/build/tab5/firmware.bin`）。本游戏把关卡进度
> 存在 NVS 里，同理：刷合并镜像 = 全新安装。

## 5. 重新生成"零素材"文件

玩家能看到听到的一切都由源码生成，所以改过数值后必须重新生成：

```bash
python3 tools/gen_font.py     # → src/pixel_font.h   （95 个手写字形）
python3 tools/layout.py       # → src/config_gen.h + preview.png
python3 tools/gen_sfx.py      # → src/sfx_data.h     （全部 PCM，合成而来）
```

`tools/layout.py` 的断言全过时会打印 `GEN_EXIT=0`（关卡数量、路径合法性、覆盖
审计）。**没打印 `GEN_EXIT=0` 就不要编译** —— 磁盘上的生成头是上一次的产物，
构建会静默使用旧内容。

## 6. PC 仿真（不需要硬件）

仿真把**固件真实的源码**对着一个假 M5GFX 编译，验证的是逻辑与渲染，不是另写一份
模拟器：

```bash
g++ -std=c++17 -O2 -I sim/compat -I src sim/run.cpp -o sim/sim && ./sim/sim
# → 165 passed, 0 failed
```

顺手能用的几条：

```bash
# 导出一张战场帧（真渲染）给文档/海报用
SIM_SHOT=13 SIM_SHOT_WAVE=9 SIM_SHOT_OUT=/tmp/frame.ppm ./sim/sim
python3 tools/to_png.py /tmp/frame.ppm        # PPM → PNG

# 重建覆盖预算审计 + 20 图拼图
python3 tools/map_design.py                   # → shots/preview-map-lvNN.png、shots/preview-maps-sheet.png

# 用真实帧重建宣传图
python3 tools/mkposter.py --frame /tmp/frame.ppm --out releases/
```

## 7. 排障

| 症状 | 原因 | 处理 |
|---|---|---|
| M5Burner 刷完黑屏 | 合并镜像把 bootloader 放在 0x0，或用了 `qio` | 按第 4 节重做镜像（`0x2000` + `--flash-mode dio`） |
| 开机报 `M5Tab5 need PSRAM SPEED 200MHz` | PSRAM 没开或速度不对 | 保留 `board_build.psram = enable` 与 `src/sdkconfig.h` 原样 |
| `Could not open /dev/cu.usbmodemXXXX` | 端口不对，或被占用 | `ls /dev/cu.usbmodem*`、关掉占用的 monitor、显式传端口 |
| 冷启动黑屏、复位后又正常 | M5GFX 上电时触控 I2C 探测偶发失败 | 软件复位而不是断电重插；反复出现请提 issue |
| `tools/layout.py` 没打印 `GEN_EXIT=0` | 有地图/数值违反断言（路格打不到、可建位太少） | 读它打印的关卡号改 `layout.py`，**不要去静音断言** |
| 改完仿真检查不过 | 真的逻辑回归（这就是它的用途） | 改代码，不改断言；除非断言本身错了 —— 那就改断言并在提交信息里说清原因 |
