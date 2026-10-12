# PIXEL TOWER DEFENSE

**A pixel tower defence for the M5Stack Tab5 — where every pixel is a `fillRect`
call and every sound effect is arithmetic. No image files. No audio files.
No third-party fonts.**

[中文版 README →](README_CN.md) · [Build & flash →](BUILDING.md) · [Contributing →](CONTRIBUTING.md) · [中文贡献指南 →](CONTRIBUTING_CN.md)

[![CI](https://github.com/andjiang0083/pixel-tower-defense/actions/workflows/sim.yml/badge.svg)](https://github.com/andjiang0083/pixel-tower-defense/actions/workflows/sim.yml)

![PIXEL TOWER DEFENSE](releases/cover-1280x720-pixel-tower-defense-tab5.png)

> Architecture and most of the code in this repository were written by a
> **local LLM** (a 4-bit Qwen3.8-Flash-Next running on the author's Mac, not a
> cloud model). A human set the goals and a **primary-school playtester** found
> most of the balance bugs. See [CREDITS.md](CREDITS.md).

## What it is

20 levels of build-a-maze tower defence on a 1280×720 touch screen. Enemies walk
a fixed path from the right edge to your base; you spend gold on five tower types,
upgrade them, and try to keep 20 lives. Later maps are *rock fields*: most of the
ground is unbuildable, so the good spots are a budget you have to spend well.

It is a small game with an unusually strict constraint: **the repository contains
zero asset files**. The 5×7 font is 95 hand-written bitmaps, the 16×16 Chinese font
is *generated* from the game's own string table (`tools/gen_cnfont.py` renders the
actual strings out of `src/i18n.h`), all PCM for the sound effects is synthesised at
build time (square waves, sweeps, an LFSR noise source), and every sprite, tower,
bullet and UI element is a stack of rectangles. Clone it, build it, and nothing gets
downloaded except the toolchain.

On boot you get a **title screen**: a starfield, a rainbow pixel logo, the version,
and an EN / 中文 toggle. Your first level opens with a three-step tutorial that never
comes back.

| | |
|---|---|
| Device | M5Stack Tab5 (ESP32-P4, 16 MB flash, PSRAM @200 MHz) |
| Display | 1280×720 MIPI-DSI, 40 px cells, 32×14 grid |
| Input | Touch only (no physical buttons on this device) |
| Levels | 20 + endless mode (unlocked by clearing L20, best wave remembered) |
| Towers | 5 types, 3 levels each (+12% range, ×1.3 damage per level) |
| Enemies | 9 kinds, incl. air (flies straight to your base) and healers |
| Bosses | 9 abilities; up to two bosses per level, mid-bosses too |
| Languages | English / 中文, switchable on the title screen (16×16 bitmap font, generated from source) |
| RAM / Flash | 37,248 B (7.3%) / 833,808 B (26.5%) of a v0.4.2 build |

## Screenshots

All of these are **real frames from the game's own renderer** (dumped by the PC
simulator running the firmware sources, not mock-ups):

| Level select | Level 13 SAND COIL |
|---|---|
| ![level select](docs/screenshots/level-select.png) | ![L13 combat](docs/screenshots/combat-l13-sand-coil.png) |

| Level 12 HAIRPIN RIDGE | All 20 maps (audit render) |
|---|---|
| ![L12 combat](docs/screenshots/combat-l12-hairpin-ridge.png) | ![map sheet](docs/screenshots/maps-sheet.png) |

## Install

**Easiest — M5Burner.** Search for *PIXEL TOWER DEFENSE* in
[M5Burner](https://burner.m5stack.com) (or the desktop app) and flash it. M5Burner
writes one merged image at offset 0x0, which is what the `*-merged.bin` release asset
is built for — see [BUILDING.md](BUILDING.md). (The store entry is still v0.4.0
while it waits for review; for the newest build use the GitHub release.)

**GitHub Releases** — grab `pixel-tower-defense-vX.Y.Z-tab5-merged.bin` from the
[latest release](https://github.com/andjiang0083/pixel-tower-defense/releases/latest)
and flash it at offset 0x0 with esptool:

```bash
esptool --chip esp32p4 write-flash -z 0x0 pixel-tower-defense-vX.Y.Z-tab5-merged.bin
```

**From source.** Short version:

```bash
git clone https://github.com/andjiang0083/pixel-tower-defense.git
cd pixel-tower-defense
pio run -e tab5                      # build
pio run -e tab5 -t upload            # flash over USB-C
```

Details, pitfalls and recovery steps: **[BUILDING.md](BUILDING.md)**.

## How to play

1. **Pick a tower** in the bottom bar (tap a slot).
2. **Tap a green cell** to build. Path cells, occupied cells and rock cells are
   refused with a red cross — and rock cells are visibly rock, so you can see
   where you cannot build before you tap.
3. **Tap a built tower** to open its panel: upgrade to LV2/LV3 or sell for 70% back
   (the world pauses while the panel is open).
4. **NEXT WAVE** (top right) sends the next wave early. Waiting is free; calling it
   early is how you keep tempo.
5. **Lose all 20 lives and it is over** — tap the screen to retry the level. You get
   a star rating per level based on lives left.

| Tower | Cost | Range | Role |
|---|---|---|---|
| M.GUN | 60 | 150 px | Cheap single-target rapid fire. Struggles against armour. |
| CANNON | 100 | 170 px | Splash 64 px — the armour answer. Slow. |
| FROST | 80 | 130 px | Aura pulse, slows to 45 %, deals **zero** damage. Support only. |
| ENERGY | 80 | — | Pays out gold every 4 s. Costs a tower slot to make money. |
| LASER | 140 | 260 px | Continuous beam, full damage against shields, longest reach. |

| Enemy | Behaviour |
|---|---|
| GRUNT / RUNNER | Baseline walker / fast and squishy |
| TANK | 3.4× health, slow |
| SWARM | Arrives in bursts of five |
| ARMOR | Flat −6 damage per bullet hit (from wave 4) — beats M.GUN |
| SHIELD | Takes **half** bullet damage (from wave 5) — beats M.GUN and CANNON |
| CHOPPER | Air. Flies straight to the base, ignoring your maze (from wave 4) |
| HEALER | Heals nearby enemies — kill it first |
| BOSS | Every few waves, with abilities, up to two per level |

**Boss abilities** (9 total, mixed per level): `HEAL` self-healing aura · `BROOD`
spawns troops · `SPLIT` bursts into minions on death · `HORN` hastes every enemy
on the map · `EMP` silences your nearest towers · `GREED` steals gold when it hits
your base · `MOLT` sheds its armour at half health and speeds up · `GHOST` turns
periodically untargetable · `SIEGE` plants a temporary no-build zone on the field
(first seen on L11 — the first ability that constrains *space*, not numbers).
Mid-bosses get exactly one ability, and never a sustain ability — a mini boss
should show off, not stall the game.

## Level design: difficulty comes from space, not from bigger numbers

The rule the maps are built on:

```
difficulty = enemy total HP / (firepower × coverage × time covered)
```

The numeric axis (health growth 1.10×/wave, wave size, per-level multiplier)
only moves the numerator. **Level design only moves "coverage"** — how much of the
path a tower can actually reach, and how many good spots you can afford.

Concretely, in `tools/layout.py`:

| Mechanism | What it does |
|---|---|
| `band D` | Only cells within Chebyshev distance D of the path are buildable; the rest become rock automatically (never hand-drawn) |
| `BLOCKED[20][14]` bitmask | Generated into `src/config_gen.h`; expanded into `g_w.blocked[][]` at level load |
| Rock rendering | `drawCellBase` draws unbuildable cells as grey rubble with highlights — *a spot you cannot use must be visible* |
| `audit_level()` | Generator assertion: every path cell must be reachable by at least one buildable cell, and the minimum tower count must fit in `MAX_TOWERS` |
| `tools/map_design.py` | Greedy set-cover audit: "how few towers can cover the whole path", per-level heat map and a 20-map contact sheet |

The metric that matters is the **budget number** (minimum towers to cover the path),
not "potential coverage": with far more buildable cells than tower slots, potential
coverage is green for every map and therefore useless as a signal.

Two design rules that came out of this work:

- **No single-solution maps.** Each map must be clearable by at least three
  different build orders (the audit checks GAT LV1, GAT LV3 and LASER LV3);
  a level you can only beat one way is a bug, not a hard level.
- **Two-route maps are a difficulty multiplier** (two independent fronts beat
  massing one defence), so they must be ordered *after* single-route maps. The
  first draft put a two-route map at L13 and the bot died on wave 7.

## Status — what works and what does not

| | Status |
|---|---|
| 20 levels, unlock chain, star ratings, NVS progress | ✅ |
| 5 towers with upgrades and selling | ✅ |
| 9 enemy kinds, air units, shields, armour | ✅ |
| 9 boss abilities (incl. SIEGE no-build zones), mid-bosses, multi-boss levels | ✅ |
| Procedural SFX (three rotating gun variants) + mute preference | ✅ |
| Rock (no-build) layers on L15/L16/L18/L20 | ✅ |
| Title screen with language select (EN/中文) + first-run tutorial | ✅ |
| PC simulator that plays all 20 levels with assertions | ✅ 203 checks, 0 failures |
| **Endless mode** | ✅ clear L20 to enter; best wave remembered in NVS |
| Difficulty of L14–L20 | ✅ quantified: all seven clear with 2× starting gold (3.3k–7.6k spare) — *economy-locked, not structurally broken*; data in `docs/PLAYTEST.md`. Human play-test reports still welcome. |
| Save/resume of a game in progress | ❌ deliberately out of scope |

Honest note on the simulator: it compiles the **real** `game.h` / `render.h` /
`touch.h` / `actions.h` against a fake M5GFX and plays every level with a greedy
bot. When a level fails there, it is not proof the level is unfair — only that the
bot's single-defence strategy broke down. Levels 1–4 and 11–13 are gated (must pass);
the rest are recorded for information.

## Verification (real output, not plans)

| Check | Result |
|---|---|
| `./sim/sim` (20 levels + map data + title/onboard/endless/SIEGE) | **203 checks passed, 0 failed** (v0.4.1: 178) |
| Gated levels cleared by the bot | L1–L4 and L11–L13, incl. L13 with 20/20 lives, 318 kills |
| `./sim/sim --x2scan` (L14–L20 with 2× starting gold) | **all seven PASS** (3,296–7,619 gold spare) — the "hard" is economic, not structural; `docs/PLAYTEST.md` |
| Coverage audit, all 20 maps (`tools/map_design.py`) | All PASS: minimum towers 5→18 ≤ 20, no dead path cells, three viable build orders |
| `pio run -e tab5` (v0.4.2) | SUCCESS — RAM 37,248 B (7.3 %), Flash 833,808 B (26.5 %) (the final size-check step was blocked by the local sandbox; compile+link succeeded, `firmware.bin` rebuilt from the ELF with esptool — same as v0.4.1) |
| Merged image (944,272 B) | built with `esptool merge-bin`; boot/partition/app headers verified identical to the v0.4.0 layout (offsets 0x2000 / 0x8000 / 0xe000 / 0x10000); sha256 `b5e70110…d5f5` |
| Device flash + readback | **PASS** — flashed to a real Tab5 (ESP32-P4 rev 1.3) on 2026-10-12; bootloader / partition / boot_app0 / app regions read back **byte-identical** (app sha256 `682b739d…`); boot banner `=== PIXEL TOWER DEFENSE v0.4.1 | board=22 | 1280x720 ===`; the previous NVS progress (`unlock=0007 cleared=0003`) survived and loaded correctly (v0.4.1 image; the v0.4.2 flash is pending) |

Bugs found and locked in by the simulator (now regression assertions): `int8_t`
projectile speed truncation, coordinates not written back, target selection
preferring far targets over the one under the gun, and a test bot that upgraded a
single tower and faked a healthy economy. The v0.4 work also caught a `uint16_t`
unlock bitmap that would have permanently locked level 17. v0.4.1 caught the
same species of bug in the NVS save-dedup cache (a `uint16_t` cache against a
`uint32_t` bitmap, writing flash every frame on the level-clear screen) — now
covered by assertions in `sim/run.cpp` §15. v0.4.2 caught the same species again:
three `uint8_t` truncation sites that would have silently disabled the SIEGE
ability (bit 256) on hardware — §22.

## Repository layout

```
tools/layout.py     ← single source of truth: grid, paths, economy, level table, palette
                      → generates src/config_gen.h (+ preview.png)
tools/gen_font.py   ← the 5×7 font's only source (hand-written glyph bitmaps) → src/pixel_font.h
tools/gen_cnfont.py ← renders the 16×16 Chinese font from the strings in src/i18n.h → src/cn_font.h
tools/gen_sfx.py    ← synthesises every PCM effect → src/sfx_data.h
tools/map_design.py ← coverage-budget auditor + heat maps + 20-map contact sheet
tools/mkposter.py   ← builds the promo art from a real frame + the game's own font/PCM
sim/run.cpp         ← compiles the real firmware sources and plays all levels (assertions)
sim/compat/         ← fake M5Unified/M5GFX so the same code compiles on a PC
src/i18n.h          ← StrId enum + parallel EN/CN string tables + runtime language switch
src/game.h          ← World/Enemy/Tower/Bullet/Fx + gameUpdate(): no drawing
src/render.h        ← drawing only: fillRect / drawPixel, nothing else
src/main.cpp        ← setup/loop, touch → action dispatch, NVS progress
```

Change a number or a map **only** in `tools/layout.py`, then:

```bash
python3 tools/gen_font.py && python3 tools/layout.py   # must print GEN_EXIT=0
```

Generated headers are committed; if the generator fails, the build must not pick
up stale output.

## Contributing

Ideas, maps and balance reports are all welcome — start with
[CONTRIBUTING.md](CONTRIBUTING.md) (中文: [CONTRIBUTING_CN.md](CONTRIBUTING_CN.md))
and [ROADMAP.md](ROADMAP.md). The two constraints worth repeating: **zero asset
files** and **one source of truth**. A patch that adds a PNG, a WAV or a second
config file will be declined.

## Credits and license

[CREDITS.md](CREDITS.md) — including the local-LLM authorship note and the
third-party dependency list. MIT licensed ([LICENSE](LICENSE)).
