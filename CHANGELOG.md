# Changelog

Versions are small patch bumps on purpose: features are merged into a tiny
version number instead of 1.x.0 jumps. Entries describe what a player can feel,
not internal refactors.

## v0.4.2 — a title screen, Chinese, and the v0.5.0 list done early

Every remaining item from the v0.5.0 suggestion list, folded into a patch bump on
purpose — the version number is not the feature. New screens, a second language,
a ninth boss ability, and one data-driven difficulty verdict. Details in
`docs/FIXLOG-v0.4.2.md`.

Player-visible:
- **Title screen on boot.** A starfield, a rainbow pixel logo ("PIXEL TOWER
  DEFENSE" / 像素塔防), the version in gold, and a language button — pick EN or
  中文 and the game remembers it. The screen is the first thing you see after
  power-on; tap the logo area to reach the level select.
- **The whole game speaks Chinese.** Not a translation overlay — every HUD
  label, button, panel and hint has a real Chinese string. The glyphs are a
  16×16 bitmap font generated from source (`tools/gen_cnfont.py` renders the
  actual strings out of `i18n.h` with Pillow), so the zero-asset rule still
  holds: no font files, just `src/cn_font.h`. Language switches live on the
  title screen; the choice persists in NVS.
- **A three-step tutorial on your first level**, then never again: build a
  tower, the wave button, the upgrade panel. Dismissed per save file.
- **Endless mode has a door.** Clear L20 and the victory screen offers it; the
  best wave you ever reach is tracked and shown on the title/HUD.
- **Towers sound different.** Gunfire rotates through three synthesized
  variants, so long waves no longer sound like one machine-gun loop.
- **The ninth boss ability: SIEGE.** The boss plants a no-build zone on the
  field — flashing red frame, tower construction refused inside — the first
  ability that constrains *space* instead of numbers. First seen on L11.
- Difficulty verdict, from data: a full x2-gold rescan of L14–L20 shows those
  seven levels clear with 3.3k–7.6k gold to spare — they were "hard because
  you must spend", not "hard because the math is broken". No nerfs shipped.

Under the hood:
- New `src/i18n.h`: `StrId` enum + parallel EN/CN tables + runtime `TR(id)`;
  the renderer's ASCII-only assumption replaced by UTF-8 aware pixel drawing.
- `LevelDef.ability` widened uint8_t → uint16_t for the 9th ability — and the
  sim caught three real truncation sites (`bossAbilityOf`, `spawnEnemy`,
  `bossAbilities`) that would have silently disabled SIEGE on hardware.
- Siege duration and cadence are now separate constants (SIEGE_MS / SIEGE_IV);
  the first attempt used one number for both and the zone became permanent.
- The bot splits its upgrades by route coverage instead of round-robin, and
  the PC simulator gained 25 assertions (178 → **203**), plus an `--x2scan`
  mode that re-runs late levels with doubled gold to classify difficulty.
- CI checks the three deterministic generators for drift and the Chinese font
  for *coverage* (every character in the string table has a glyph) — pixel
  bitmaps are rasterised from a system font and cannot be byte-identical
  across machines, so the semantic invariant is what CI enforces.
- The simulator mirrors the device's `ST_TITLE` static branch (it used to keep
  simulating and rendering the world behind the title screen), gained a
  `SIM_SHOT=title` capture mode, and `renderFrame` defends against the state.
  Cover art regenerated: title screenshot in the READMEs, 9 boss skills on
  the poster stat bar.

## v0.4.1 — the fixes you feel only after an hour of play

A full code review of the v0.4.0 tree, with every finding either fixed or
covered by a regression assertion. Details in `docs/FIXLOG-v0.4.1.md`.

Player-visible:
- **Bosses now keep their own schedules.** In v0.4.0 every active boss ability
  fired on the same tick and the cadence was whatever the last one in the chain
  said — e.g. the L6 boss healed every 7 s instead of 2 s, and the final boss
  fired all five abilities at once every 9 s. Each ability now runs on its own
  timer (mini bosses keep their slowed-down ×2 cadence).
- **Star ratings stick.** Replaying a level for a better star rating used to be
  lost silently if the unlock/clear bitmaps had not changed. Stars are now part
  of the save-dedup signature.
- **The level-clear screen no longer writes flash every frame.** On levels
  17–20 the NVS dedup cache was 16-bit against a 32-bit bitmap, so the game
  saved progress 30 times a second while the victory panel was up.
- **No more digit smudge next to your HP.** When lives dropped from two digits
  to one (10 → 9) the old number's right edge stayed on screen: the HP erase
  box was 6 px too narrow.
- **Tapping a locked level** flashes the red cross for 20 frames and cleans up
  after itself, instead of leaving the mark on the screen.
- Game-over / level-clear dimming is a bit cheaper to paint (same look, a
  quarter of the fill calls) — less of a hitch the moment you win or lose.

Under the hood:
- New `src/progress.h`: all "when to write NVS" logic in one testable place;
  the PC simulator now asserts the clear screen saves exactly once.
- Per-ability boss timers (`Enemy.abNextMs[8]`), staggered on spawn so bosses
  no longer open with a simultaneous volley.
- `waveSize()` helper (the wave-count formula existed twice and could drift).
- CI (`.github/workflows/sim.yml`): simulator + generator drift checks on every
  push/PR — the repo's test gate was previously manual-only.
- Renderer: run-length merged pixel-font drawing; HUD erase boxes sized for
  their longest possible content (incl. endless-mode 6-digit gold).

Verified on hardware (2026-10-12): the merged image was flashed to a Tab5
(ESP32-P4) and read back byte-identical outside the NVS partition; the
previous save survived and loaded. Evidence in `docs/FIXLOG-v0.4.1.md` §三.

## v0.4.0 — 20 levels, and difficulty that comes from space

- **10 new maps (L11–L20)** designed around coverage budget instead of bigger numbers
- **No-build rock layers**: on the later maps parts of the field are rock, so the
  good tower spots are a finite budget you have to spend well
- **8 boss abilities**: heal, brood, death-split, horn, EMP, greed, molt, ghost —
  invisible bosses, hasted pushes, silenced towers, stolen gold
- **Mid-bosses**: a mini boss with exactly one shown-off ability can appear mid-level
- **Endless engine** (parameters + wave scaling) — engine ready, UI not wired yet
- 20-node level select (5×4) replaces the old 5×2 grid; stars packed into two 32-bit bitmaps
- Difficulty readjusted for the primary-school playtester's notes: denser waves,
  shorter levels, bosses that spawn troops on death

## v0.3.2 — shields and armour

- **SHIELD** unit: takes half damage from bullets — forces laser/splash mixing
- **ARMOR** unit: flat −6 per bullet hit
- Economy tightened (kill gold 150%, upgrade curve steeper): "hard because it makes
  you think, not because it is stingy"

## v0.3.1 — sound polish

- Sound effects dispatched in the frame gap; mute preference remembered in NVS

## v0.3.0 — air, stars and the level select screen

- **CHOPPER**: air unit that flies straight to the exit, ignoring the maze path
- **Level select screen** with per-level star ratings, progress in NVS
- A dedicated anti-air role for towers; per-level star thresholds

## v0.2.0 — 10 levels, verified by a bot

- 10 hand-designed levels, multi-route spawning, unlock chain, level select state machine
- **PC simulation harness** (`sim/run.cpp`) that compiles the real game sources and
  plays every level with a greedy bot — assertions live in the same file
- Healer and boss abilities, elite units, economy sampled per wave

## v0.1.2 — the economy you can feel

- Rebalance so money always "almost" runs out: early squeeze, demand ahead of
  income, no snowball interest, **leaking enemies earns nothing**
- Result: 1–41 gold banked between waves instead of 500+

## v0.1.0 — first playable

- Pure-`fillRect` pixel tower defence for the M5Stack Tab5 (1280×720, touch only)
- 3 towers, 5 enemy kinds, HUD, upgrade/sell panel, wave button, game over
- Zero image/audio assets; the 5×7 font and all PCM are generated from source
