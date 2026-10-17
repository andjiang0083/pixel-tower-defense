# Changelog

Versions are small patch bumps on purpose: features are merged into a tiny
version number instead of 1.x.0 jumps. Entries describe what a player can feel,
not internal refactors.

## v0.5.4 — level 10 stopped choking, and the Chinese UI came back

**Level 10 no longer snowballs.** On that map the second wave is a small one, and
the rule for "may the next wave start yet?" was counting *every* enemy on the
field. So the third wave was allowed to start while the second one was still
walking, then the fourth, and so on — the field filled up until the framerate
collapsed and the game looked frozen. A wave now has to see its *own* enemies
nearly gone before the next one may overlap it, and level 10 holds a steady
30 fps.

**The heaviest maps are smoother.** Drawing the ground cost more than 2000
separate rectangles per frame — the frame budget was going into *calling* the
renderer rather than into pixels. Each grid cell is now rendered once and
reused, so clearing a cell is a single blit. On hardware, the worst case went
from ~24 ms of clearing per frame to ~8 ms, and 48 enemies plus 20 towers on
screen now hold 30 fps. The temporary cap on how many enemies may be alive at
once is therefore gone — that cap was hiding the slowdown, not fixing it.

**English / 中文 switching is back on the title screen.** The language buttons
disappeared in 0.5.0. They are back, the choice is remembered between sessions,
and every screen is now checked in *both* languages — including a guard that no
Chinese character can quietly go missing from the generated font.

Player-visible:
- Level 10: waves no longer pile up into a field-wide freeze.
- Clearing a cell: one blit instead of ~20 rectangles (measured on the device).
- Enemy count cap removed — it was masking the slowdown.
- Title screen: EN / 中文 buttons restored; the choice is saved.
- Chinese "最高" no longer collides with the "波次" label in endless mode.
- The Chinese title is drawn from the same string table the font is generated
  from, so a missing glyph can no longer slip through unnoticed.

Details: `docs/FIXLOG-v0.5.3.md`.

## v0.4.4 — the sound is back

**Every gun, cannon and explosion now actually makes a noise.** This one was
a big one: since v0.4.0 the game has been completely silent on hardware —
including the build published to M5Burner. The cause was in the audio codec's
setup: the DAC output was never routed to the output mixer, so every sound
was computed perfectly and then quietly died on its way to the speaker. We
rewrote the codec init from Espressif's official driver sequence, and audio
is verified working on hardware — including after a full cold boot (unplug,
wait five seconds, power up).

The investigation is documented in `docs/AUDIO-HANDOFF.md`; the fix lives in
`src/sfx.h`. Thanks to the WorkBuddy testers whose issue report (#1) started
the hunt.

## v0.4.3 — the UI audit: text that overlapped, and glyphs that were missing

v0.4.2 shipped a lot of new UI, so this release is a pass over every screen
looking for text that collides with something else or silently fails to draw.
Eight defects turned up. Three of them were invisible at normal zoom on the
emulator and needed a pixel count to prove; one of them I missed entirely and
the user caught it on hardware. Details in `docs/FIXLOG-v0.4.3.md`.

Player-visible:
- **The title screen no longer has overlapping text.** "LANGUAGE" used to sit
  at a hardcoded x, running 26 px underneath the ENGLISH button. It is now
  centred above the button pair.
- **The Chinese title screen is complete.** It used to read 塔防 — the glyphs
  for 像素塔防 were never in the font, because the title was drawn from a
  hardcoded string that the font generator never saw. Same for the 中文 button
  label in English mode, which rendered as an empty box.
- **Full-width punctuation renders.** 已击败： and 拦住所有敌人！were missing
  their ：and ！: the glyph collector only accepted CJK ideographs, so anything
  outside U+4E00–9FFF was silently skipped.
- **The gold "$" sign is back.** v0.4.2's SIEGE warning strip cleared a band
  wide enough to erase it every frame — it had quietly disappeared from the HUD.
- **The endless "BEST" readout no longer sits on top of "WAVE".** It moved into
  the empty gap between the gold counter and the wave label.
- **The ENDLESS MODE button on the L20 victory screen** no longer has its second
  line cut off by the build bar.
- **"TAP TO START" on the title screen is one clean line of text again.** It was
  drawn twice as a cheap outline effect — and the second pass (dark, offset left
  by 3 px) landed on top, so the text body came out dark with a 3 px bright
  fringe. At a 15 px glyph the offset is the same size as the stroke itself, so
  the two passes interleaved into a doubled, blurry look in both languages.
  Reordering them does not help; that size has no room for an outline.
- Nothing you can see was removed, but one screen's worth of machinery is gone:
  the "tap grass to build" hint strip was dead code (the tutorial overlay
  replaced it back in v0.4.2) and is now deleted outright rather than kept
  around with stale coordinates.

Under the hood:
- `sim/run.cpp` gained `checkUiTextFit()`: every UI string is measured against
  the box it must fit in, in both languages — 167 new assertions (203 → 370),
  including a HUD column-collision set that references the same named layout
  constants the renderer draws with.
- The renderer now has a **missing-glyph counter**; the simulator renders every
  screen in Chinese and asserts the counter is zero. A hardcoded Chinese string
  in render code can no longer slip through unnoticed.
- Glyph collection is now a single shared rule (`tools/cn_glyph_rule.py`) used by
  both the generator and the CI checker: *every non-ASCII character in the
  string table needs a glyph*. Previously both files each hardcoded the CJK
  range, which is how the punctuation escaped both.
- `./sim/sim --shots <dir>` renders all 11 screens in both languages (22 PPMs)
  for visual review, and each shot repaints a clean base frame first.

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
