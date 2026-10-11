## What this changes / 这个 PR 改了什么

<!-- One or two sentences. 一两句话。 -->

## How it was verified / 怎么验证的

Tick what you actually ran — not what you plan to run.
请只勾选真正跑过的。

- [ ] `python3 tools/layout.py` — regenerated `src/config_gen.h`, `GEN_EXIT=0`
- [ ] `g++ -std=c++17 -O2 -I sim/compat -I src sim/run.cpp -o sim/sim && ./sim/sim`
      → checks passed, 0 failed
- [ ] `pio run -e tab5` — compiles (quote the RAM/Flash numbers below)
- [ ] Flashed and played on a real Tab5 (say which level/wave)
- [ ] Visual check of the affected screens (frame dump or photo attached)

## Constraints checklist / 约束自检

- [ ] No new asset files (no images, no audio, no third-party fonts)
- [ ] All tunable numbers live in `tools/layout.py` (no hand-edited generated files)
- [ ] Generated files (`src/config_gen.h`, `src/pixel_font.h`, `src/sfx_data.h`)
      are committed *after* the generator ran successfully
- [ ] No global timer that survives a level load (wave data is reset per level)
- [ ] If it touches difficulty: does it keep more than one viable build order?
      (a map that has exactly one solution is a design bug, not a hard level)

## Numbers from the build (paste the real output) / 构建实测数字

```
RAM:   xx.x% (used xxxxx bytes)
Flash: xx.x% (used xxxxxx bytes)
```
