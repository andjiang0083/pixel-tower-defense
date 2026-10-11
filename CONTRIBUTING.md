# Contributing

[中文贡献指南 →](CONTRIBUTING_CN.md)

Thanks for wanting to help. This project has unusual constraints and a specific
workflow, so please skim this before opening a PR — most rejected patches are
rejected for breaking one of the rules below, not for being bad work.

## Two hard rules

1. **Zero asset files.** No `.png`, `.bmp`, `.jpg`, `.wav`, `.mp3`, no third-party
   font, no sprite sheet, no tilemap file. Everything visible is `fillRect` /
   `drawPixel` (`src/render.h`); everything audible is synthesised by
   `tools/gen_sfx.py`; the font is hand-written bitmaps in `tools/gen_font.py`.
2. **One source of truth.** Every tunable number, every level's path, economy and
   palette lives in `tools/layout.py`, which generates `src/config_gen.h`. Never
   hand-edit a generated file, and never add a second config file.

## Workflow

```bash
# 1. change the source of truth, then regenerate — and check the gate
python3 tools/layout.py            # must print GEN_EXIT=0
python3 tools/gen_font.py          # only if you touched the font
python3 tools/gen_sfx.py           # only if you touched the sounds

# 2. prove the logic on the PC first (no hardware, no serial debugging)
g++ -std=c++17 -O2 -I sim/compat -I src sim/run.cpp -o sim/sim && ./sim/sim

# 3. build
pio run -e tab5

# 4. if it touches rendering or layout, dump a frame and look at it
SIM_SHOT=13 SIM_SHOT_WAVE=9 SIM_SHOT_OUT=/tmp/frame.ppm ./sim/sim
python3 tools/to_png.py /tmp/frame.ppm
```

A pull request should contain **the real output** of steps 1–3 (the check count, the
RAM/Flash numbers), not promises. If you cannot run the hardware, say so — the
simulator plus a frame dump is usually enough for review.

## Rules that come from bugs we actually hit

| Rule | Why it exists |
|---|---|
| **No global timers.** Anything time-based belongs to the wave, and is reset in `levelLoad()`. | A static timer that survived a level load made overlapping waves unwinnable in a way that looked random. |
| **The last wave never overlaps the next one.** Wave scheduling must be monotonic. | Overlap on the final wave caused unkillable pile-ups. |
| **`killEnemy()` is the only place that pays out gold.** | Three damage paths (bullet, laser, splash) double-paid on the same frame. |
| **Difficulty is judged by humans, not by the bot.** A failing bot is not a broken level. | The bot cannot split its defence across two routes and will lose maps a person clears. |
| **Every map needs at least three viable build orders.** | A map with one solution is a puzzle with a leaked answer sheet, not a tower defence level. |
| **Unbuildable ground must be visible.** | Silent refusal to place a tower reads as a bug. |
| **Small version bumps.** Features get merged into a patch number (`v0.4.0`, not `v1.2.0`). | The changelog stays readable and releases stay honest. |

## Proposing a map

Levels are *spatial budget problems*, not decoration. A map proposal should say:

- what spatial question it asks ("the only overlap spots are inside the U-turn"),
- its `band D` value and why,
- the audit numbers from `python3 tools/map_design.py` (path cells, buildable cells,
  rock cells, **minimum towers to cover the path**, three-route tower counts),
- that minimum towers ≤ `MAX_TOWERS` (20), and every path cell is reachable.

## Language

English or Chinese, both are welcome. In-repo docs are maintained as parallel
files (`README.md` / `README_CN.md`); if you change one, update the other or say
in the PR that the translation is pending.

## Reporting balance problems

"I beat level 17 on the first try" or "level 14 ate six attempts and felt unfair"
is genuinely useful — please include the level, your rough build order, and how
many lives you finished with. That is the data the difficulty numbers are missing
(no bot can supply it).
