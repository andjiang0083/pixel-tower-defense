# Changelog

Versions are small patch bumps on purpose: features are merged into a tiny
version number instead of 1.x.0 jumps. Entries describe what a player can feel,
not internal refactors.

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
