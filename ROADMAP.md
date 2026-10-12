# Roadmap

[中文版 →](ROADMAP_CN.md)

> **Status 2026-10-12:** v0.4.2 shipped the whole §7 bundle (endless UI, onboarding,
> per-tower SFX, bot route-splitting, the spatial boss ability SIEGE, the L14–L20
> difficulty data) plus a title screen and a full bilingual UI. See the update note
> in §7 below and `docs/FIXLOG-v0.4.2.md`. Still open: §2 human play-test reports,
> §4 real-device photo, §5 extra maps, §6 star thresholds / palette pass, and the
> §7 "needs a decision" item on splitting the translation unit.

Concrete, claimable work. Everything here is scoped so that one person can finish
it in a session or two. If you want an item, open an issue saying so — first
comment wins, and I will not start it in parallel.

## 1. Wire up endless mode (engine already exists)

The engine is done: `g_endless`, `lvWaves()`, `lvDiff()`, `ENDLESS_*` constants,
wave scaling, boss every round. What is missing is the way in:

- after clearing L10, offer "endless" (or add an ENDLESS node to the level select),
- HUD shows `∞` and the current round/wave,
- difficulty ramp uses `lvDiff()` at the endless rate,
- best round/score persisted in NVS (there is already a `stars`/`mute` pattern to copy in `src/main.cpp`),
- sim coverage: an assertion that the endless loop survives 3 rounds without leaking state.

Acceptance: bot clears 3 endless rounds in `sim/run.cpp`, the record survives a reboot.

## 2. A human difficulty pass over L14–L20 (the data we do not have)

The bot fails L14–L20 and the bot is not a benchmark — it cannot split its defence
between two routes. What is needed is **play-test reports**: level, build order,
lives left, waves survived. Then, in order:

1. `diff` first (numerator) if the level is simply over-tuned,
2. `band D` second (coverage) if the map gives too few good spots,
3. wave composition last, if only specific waves break it.

Do not touch `KILL_GOLD_PCT` as a first move — reward was already raised to 150 %
because a thicker economy is what makes "hard" instead of "stingy".

## 3. Turn the kid playtester's findings into a checklist

We have one real playtester with a strong opinion. Formalise it: a short
`docs/PLAYTEST.md` template (level → waves reached → where it felt unfair → build
order), so the next kid's session produces comparable data instead of anecdotes.

## 4. A real device photo for the README

The screenshots are real frames from the real renderer, but they are still pixels
out of a simulator. One honest photo of the Tab5 running the game beats any mock-up.
Needs somebody with the hardware and a camera.

## 5. More content — the cheap axes

- **2–3 more maps** using existing map families (the generator makes this cheap);
  each must pass `map_design.py` with ≥3 viable build orders.
- **One extra boss ability** that creates a *spatial* problem rather than a numeric
  one (e.g. an ability that temporarily walls off a build region).
- **A second enemy that ignores the path partially** (a jumper) — but only if it
  stays readable at 40 px per cell.

## 6. Smaller polish

- Per-tower SFX variation so a board of twelve guns stops sounding like a metronome.
- Star thresholds re-checked against the new wave sizes (`START_LIVES`-relative).
- HUD readability pass on a bright room / dim room (the palette is tuned for dark).
- Chinese in-game HUD? The font is hand-written 5×7 bitmaps — a CJK subset would mean
  authoring those bitmaps by hand. Interesting, but it is a real chunk of work;
  propose it if you care about it.

## 7. Suggested bundle for the next version (v0.5.0) — added after the v0.4.1 review

> **Update 2026-10-12: this bundle was implemented in v0.4.2** (the user asked for a
> small patch number on purpose — features merged into 0.4.2 instead of a 0.5.0 jump).
> Shipped: ① L14–L20 quantified (`docs/PLAYTEST.md`, all seven clear with 2× gold =
> economy-locked, not structurally broken — no numbers nerfed) ② vendor tarballs
> archived, sha256 in `BUILDING.md` ③ `docs/PLAYTEST.md` template landed ④ endless UI
> (clear L20 → endless button, best wave in NVS) ⑤ first-run onboarding overlay
> (three steps, NVS flag) ⑥ per-tower SFX variation (three rotating gun variants)
> ⑦ bot now splits upgrades by route coverage ⑧ spatial boss ability SIEGE
> (L11, temporary no-build zone). Also shipped on top of the bundle: a title screen
> with language select and a full bilingual UI (EN/中文, generated 16×16 bitmap font).
> Details in `docs/FIXLOG-v0.4.2.md`. Remaining from this section: the two
> "needs a decision" items below.

A version-bundle view over the sections above. Ordering principle: **data first
(zero risk) → retention features (low risk) → engineering leverage (medium) →
needs-a-decision (do not schedule yet)**.

**Tier 1 — data and foundations (zero risk, do first)**

1. ~~**Quantify L14–L20 difficulty**~~ — **done in v0.4.2**: all seven levels PASS
   with 2× starting gold (3,296–7,619 gold to spare) → economy-locked, not
   structurally broken; no nerfs shipped. Data in `docs/PLAYTEST.md`.
2. ~~**Build reproducibility archive**~~ — **done in v0.4.2**: tarballs stored in
   `~/esp32/vendor/`, sha256 recorded in `BUILDING.md` / `BUILDING_CN.md`.
3. ~~**Land `docs/PLAYTEST.md`**~~ — **done in v0.4.2** (template + x2gold results).

**Tier 2 — retention features (low risk, the body of v0.5.0)**

4. ~~**Endless mode UI**~~ — **done in v0.4.2** (L20 clear screen → endless; best
   wave in NVS; sim assertions §21).
5. ~~**First-run onboarding overlay**~~ — **done in v0.4.2** (three steps, NVS flag,
   sim assertions §20).
6. ~~**Per-tower SFX variation**~~ — **done in v0.4.2** (three rotating gun variants;
   sim assertions §23).

**Tier 3 — engineering leverage (medium risk, v0.5.x or its own bump)**

7. ~~**Upgrade the bot to split its defence between routes**~~ — **done in v0.4.2**
   (`botUpgradeSplit()`: weakest route's lowest tower upgrades first; gate levels
   still all pass, failure set unchanged).
8. ~~**New content**~~ — **partly done in v0.4.2**: the *spatial* boss ability
   (SIEGE, L11) landed; the 2–3 extra maps are still open.

**Needs a decision (do not schedule yet)**

9. **Chinese in-game HUD** — **done in v0.4.2**, by a different route than this item
   assumed: not hand-authored 5×7 CJK bitmaps but a generated 16×16 font
   (`tools/gen_cnfont.py` renders the actual strings from `src/i18n.h`). The whole
   UI is bilingual now, with a title-screen language toggle.
10. **Split the single translation unit** (`game.h` → `game.cpp`): v0.4.1
    documented the convention in the header; only worth the diff once a second
    contributor appears. Still open.

**Not version-gated (run in parallel)**: M5Burner entry update (waiting on
review), a real-device photo for the README (§4), and the push/release flow
(already proven end-to-end in v0.4.1).

## Non-goals

- Real image/audio assets (that is the whole point of the project).
- Save/resume of a level in progress (the whole game is minutes long).
- Networking, leaderboards, accounts.
- Porting to a second device is fine in principle (only `render.h` is device
  specific) — but please make it a fork rather than a `#ifdef` pile in this repo.
