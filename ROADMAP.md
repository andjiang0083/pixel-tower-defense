# Roadmap

[中文版 →](ROADMAP_CN.md)

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

A version-bundle view over the sections above. Ordering principle: **data first
(zero risk) → retention features (low risk) → engineering leverage (medium) →
needs-a-decision (do not schedule yet)**.

**Tier 1 — data and foundations (zero risk, do first)**

1. **Quantify L14–L20 difficulty** (first half of §2): re-run the bot with 2×
   starting gold on those seven levels and read the `gold/alive/lives` columns
   (`SIM_SHOT=<lv>` diagnostics) to classify each as *economy-locked* vs
   *structurally broken*. Acceptance: one conclusion sentence per level, with
   the supporting trend, recorded below this section.
2. **Build reproducibility archive**: download the dated `launcher_tab5_*`
   libs tarball once, store it, record its sha256 in `BUILDING.md`. Acceptance:
   a fresh clone builds with the network unplugged.
3. **Land `docs/PLAYTEST.md`** (§3) so the next kid session produces
   comparable data instead of anecdotes.

**Tier 2 — retention features (low risk, the body of v0.5.0)**

4. **Endless mode UI** (all of §1). Acceptance: the bot clears 3 endless
   rounds, the record survives a reboot, and the HUD needs no rework — the
   GOLD/WAVE erase boxes were already sized for endless digits in v0.4.1.
5. **First-run onboarding overlay**: on the very first L1 entry, three steps
   (place a tower → start the wave → watch it kill something), with an
   "onboarded" flag in NVS. Rationale: the playtester is in primary school and
   today's teaching is one static line of hint text.
6. **Per-tower SFX variation** (§6, first bullet): the synth pipeline already
   computes PCM at build time, so a per-tower base frequency/timbre parameter
   adds zero assets. Acceptance: twelve towers firing at once are
   distinguishable by ear.

**Tier 3 — engineering leverage (medium risk, v0.5.x or its own bump)**

7. **Upgrade the bot to split its defence between routes.** The roadmap itself
   (§2) admits the bot is not a benchmark because it cannot split its defence.
   Giving it route-budgeted building makes every future balance change
   automatically verifiable — the one item that pays rent forever.
8. **New content** (§5): 2–3 more maps plus one *spatial* boss ability
   (temporarily walls off a build region) — which lands cleanly on the
   per-ability timer refactor from v0.4.1.

**Needs a decision (do not schedule yet)**

9. **Chinese in-game HUD** (§6, last bullet): hand-authoring a 5×7 CJK subset
   is real work, and the readable font size on 1280×720 needs a trial first.
10. **Split the single translation unit** (`game.h` → `game.cpp`): v0.4.1
    documented the convention in the header; only worth the diff once a second
    contributor appears.

**Not version-gated (run in parallel)**: M5Burner entry update (waiting on
review), a real-device photo for the README (§4), and the push/release flow
(already proven end-to-end in v0.4.1).

## Non-goals

- Real image/audio assets (that is the whole point of the project).
- Save/resume of a level in progress (the whole game is minutes long).
- Networking, leaderboards, accounts.
- Porting to a second device is fine in principle (only `render.h` is device
  specific) — but please make it a fork rather than a `#ifdef` pile in this repo.
