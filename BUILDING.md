# Building and flashing

[中文版 →](BUILDING_CN.md)

Tested on macOS (Apple silicon) with PlatformIO Core. Windows/Linux work the same
way; only the serial port name differs (`COM5` instead of `/dev/cu.usbmodem*`).

## 1. Prerequisites

```bash
brew install platformio        # or: pipx install platformio
python3 --version              # 3.9+ (only for the generator and the PC simulator)
```

You **do not** need Arduino IDE, and you **cannot** use TFT_eSPI: the Tab5 panel is
MIPI-DSI in video mode (frame buffer in PSRAM, DMA-scanned), while TFT_eSPI only
implements SPI command-mode panels. The project uses M5GFX (LovelandGFX fork)
purely as a `fillRect`/`drawPixel` surface.

## 2. Build

```bash
git clone https://github.com/andjiang0083/pixel-tower-defense.git
cd pixel-tower-defense
pio run -e tab5
```

Expected on a v0.4.0 tree:

```
RAM:   [=         ]   6.9% (used 35472 bytes from 512000 bytes)
Flash: [===       ]  26.0% (used 819150 bytes from 3145728 bytes)
```

`platformio.ini` pins the important things; do not "clean them up":

| Setting | Why |
|---|---|
| `board = esp32-p4-evboard` + `-DARDUINO_M5STACK_TAB5` | M5GFX only auto-detects the Tab5 panel and GT911 touch with this define |
| `board_build.psram = enable`, 200 MHz | The frame buffer lives in PSRAM. M5GFX aborts with `M5Tab5 need PSRAM SPEED 200MHz` otherwise |
| `src/sdkconfig.h` | Overrides the PSRAM speed for the Arduino libraries |
| `board_build.partitions = app3M_fat9M_16MB.csv` | 3 MB app partition |

## 3. Flash over USB

```bash
pio run -e tab5 -t upload --upload-port /dev/cu.usbmodem2101
pio device monitor            # 115200, boot banner shows the version string
```

The port number changes between USB sockets — check with `ls /dev/cu.usbmodem*`
before flashing. **Always pass `--upload-port` explicitly**; guessing is how you
flash the wrong board.

### What PlatformIO actually sends (verified with `pio run -v -t upload`)

```
esptool --chip esp32p4 --port "/dev/cu.usbmodem2101" --baud 1500000 \
  --before default-reset --after hard-reset write-flash -z \
  --flash-mode dio --flash-freq 80m --flash-size detect \
  0x2000  .pio/build/tab5/bootloader.bin \
  0x8000  .pio/build/tab5/partitions.bin \
  0xe000  .../framework-arduinoespressif32/tools/partitions/boot_app0.bin \
  0x10000 .pio/build/tab5/firmware.bin
```

Two things to notice, because both are classic Tab5 mistakes:

1. **The bootloader goes at `0x2000`, not `0x0`** (ESP32-P4). A merged image that
   puts it at 0x0 boots to a black screen.
2. **PIO flashes with `--flash-mode dio`** even though `platformio.ini` says `qio` —
   the board definition overrides it. Never build a merged image with `qio` "because
   the ini says so".

## 4. A merged image (for M5Burner / esptool at offset 0x0)

M5Burner always writes its file at **offset 0x0** with `eraseAll`, so it needs the
whole flash layout in one file — not the app-only `firmware.bin`.

PlatformIO 12+ already produces exactly that file:

```bash
cp .pio/build/tab5/firmware.factory.bin \
   releases/pixel-tower-defense-v0.4.0-tab5-merged.bin
shasum -a 256 releases/pixel-tower-defense-v0.4.0-tab5-merged.bin \
  > releases/pixel-tower-defense-v0.4.0-tab5-merged.bin.sha256
```

Verify the segments before shipping (this is the check that catches a black screen):

```bash
python3 - <<'PY'
import struct, pathlib
b = pathlib.Path('releases/pixel-tower-defense-v0.4.0-tab5-merged.bin').read_bytes()
def hdr(off):
    return dict(magic=hex(b[off]), nseg=b[off+1], mode=b[off+2],
                size_freq=hex(b[off+3]), entry=hex(struct.unpack_from('<I', b, off+4)[0]))
print('len', len(b))
print('bootloader @0x2000', hdr(0x2000))   # magic 0xe9, mode 2 = DIO
print('partitions @0x8000', b[0x8000:0x8002].hex())  # aa50
print('boot_app0  @0xe000', hex(b[0xe000]))          # 0x01
print('app        @0x10000', hdr(0x10000))  # magic 0xe9, mode 2 = DIO
PY
```

Expected output for the v0.4.0 image:

```
len 928816
bootloader @0x2000 {'magic': '0xe9', 'nseg': 3, 'mode': 2, 'size_freq': '0x4f', 'entry': '0x4ff29ed0'}
partitions @0x8000 aa50
boot_app0  @0xe000 0x1
app        @0x10000 {'magic': '0xe9', 'nseg': 7, 'mode': 2, 'size_freq': '0x4f', 'entry': '0x4ff00c6a'}
```

If you ever need to build it by hand (a project that does not emit `factory.bin`):

```bash
python3 ~/.platformio/penv/bin/esptool --chip esp32p4 merge-bin \
  --output merged.bin --flash-mode dio --flash-freq 80m --flash-size 16MB \
  0x2000  .pio/build/tab5/bootloader.bin \
  0x8000  .pio/build/tab5/partitions.bin \
  0xe000  ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin \
  0x10000 .pio/build/tab5/firmware.bin
```

Do **not** add `--pad-to-size`/`--fill-flash-size`: the merged image should be under
1 MB, not 16 MB.

> ⚠️ A merged image `eraseAll`s the flash. On a device that already has WiFi
> credentials in NVS, flashing the merged image erases them; to upgrade in place
> while keeping NVS, write only the app partition
> (`esptool write-flash 0x10000 .pio/build/tab5/firmware.bin`). This game stores
> level progress in NVS, so the same applies here: merged = fresh install.

## 5. Regenerating the assets (the "zero asset" files)

Anything players see or hear is generated from source, so it must be regenerated
after changing a number:

```bash
python3 tools/gen_font.py     # → src/pixel_font.h   (95 hand-written glyphs)
python3 tools/layout.py       # → src/config_gen.h + preview.png
python3 tools/gen_sfx.py      # → src/sfx_data.h     (all PCM, synthesised)
```

`tools/layout.py` prints `GEN_EXIT=0` when its assertions pass (level count, path
sanity, coverage audit). **If it does not print `GEN_EXIT=0`, do not build** — the
generated header on disk is from the previous run and the build would silently use
stale content.

## 6. PC simulator (no hardware needed)

The simulator compiles the *real* game sources against a fake M5GFX, so it verifies
logic and rendering, not a re-implementation:

```bash
g++ -std=c++17 -O2 -I sim/compat -I src sim/run.cpp -o sim/sim && ./sim/sim
# → 165 passed, 0 failed
```

Useful extras:

```bash
# dump a battle frame (real renderer) for docs/posters
SIM_SHOT=13 SIM_SHOT_WAVE=9 SIM_SHOT_OUT=/tmp/frame.ppm ./sim/sim
python3 tools/to_png.py /tmp/frame.ppm        # PPM → PNG

# rebuild the coverage-budget audit + 20-map contact sheet
python3 tools/map_design.py                   # → shots/preview-map-lvNN.png, shots/preview-maps-sheet.png

# rebuild the promo art from a real frame
python3 tools/mkposter.py --frame /tmp/frame.ppm --out releases/
```

## 7. Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| Black screen right after flashing with M5Burner | Merged image built with the bootloader at 0x0, or with `qio` | Rebuild the merged image with `0x2000` and `--flash-mode dio` (section 4) |
| `M5Tab5 need PSRAM SPEED 200MHz` at boot | PSRAM not enabled / wrong speed | Keep `board_build.psram = enable` and `src/sdkconfig.h` as shipped |
| `Could not open /dev/cu.usbmodemXXXX` | Wrong socket, or the port is busy | `ls /dev/cu.usbmodem*`, close any open monitor, pass the port explicitly |
| Screen stays dark on a cold boot, works after reset | M5GFX panel touch-I2C probe failed on power-up (rare) | Software-reset rather than power-cycling; report it if it repeats |
| `GEN_EXIT != 0` from `tools/layout.py` | A map/value violates an assertion (unreachable path cell, too few buildable spots) | Read the printed failing level and fix the map in `layout.py`, do not silence the assertion |
| Simulator check fails after a change | A real logic regression (that is the point) | Fix the code, not the assertion — unless the assertion itself is wrong, then fix it and say why in the commit |
