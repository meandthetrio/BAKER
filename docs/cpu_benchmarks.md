# CPU Benchmark Log

A running record of audio-callback CPU measurements, anchored to the build that
produced them. Append a new entry per build of interest; never edit old entries
(they are point-in-time facts).

## ⭐⭐⭐ BIGGEST WIN EVER FOUND — HOT CODE IN ITCM (QSPI-XIP I-CACHE FIX) ⭐⭐⭐

> ***THE single biggest CPU reduction in this project's history.*** Moving the audio
> DSP **code** into ITCM took the **callback peak 84 → 69** (full-FX arp test) and the
> **reverb stage alone 20 → 11 (~45%)** — done in a few HOURS after *days* of other
> attempts that moved ~nothing. **If you need to cut audio CPU, DO THIS FIRST.**
> _Validated on hardware, 2026-07-02. Shipped: `90d1634`, `849c79f`, `ce9c7b9`, `dcb1abb`._

**__Why it works here__ (and why generic "memory placement doesn't help" advice is
WRONG for this board):** this app **executes from QSPI flash via XIP**. So an I-cache
miss on a hot audio function does **not** refetch from fast internal flash — it
**stalls all the way out to slow QSPI, inside the audio callback**, worst during dense
note-ons that evict audio code from the I-cache. **ITCM is 64 KB of zero-wait memory
that sits in front of the caches and can never miss.** Put the hot DSP code there and
those stalls disappear entirely.

**This is CODE placement — categorically different from DATA placement**, which *did*
flop (see the "What did NOT help" note below). Data in AXI SRAM is absorbed by the
D-cache; **code fetched from QSPI is not**, because a miss goes to a genuinely slow bus.
That one distinction is the whole game on this board.

**Measured, stage by stage (hardware, full-FX playing):**

| Stage moved to ITCM | Bucket effect | Commit |
|---|---|---|
| Dattorro reverb hot path | **reverb pk 20 → 11** | `90d1634` |
| Voice render (fetch + batched loops + dispatcher + mix) | **voice pk 25 → 17** | `849c79f`, `dcb1abb` |
| FX chain (EQ + delay + sat) + master | **fx total pk 47 → 33** | `ce9c7b9`, `dcb1abb` |
| **CUMULATIVE** | **callback pk 84 → 69** | — |

**How it's wired (build-time A/B via `ADSR2_ITCM_ENABLED` in `build_config.h`):**
- `.lds`: `.itcm_text` section — VMA in `ITCMRAM`, load image in QSPI (`> ITCMRAM AT > QSPIFLASH`).
- `main()`: size-based boot copy QSPI → ITCM **before `StartAudio`** (no-op when the flag is 0).
- `mem_regions.h`: tag hot functions with **`ADSR2_ITCM_TEXT`** at the definition.
- ITCM usage after the campaign: **~16.8 KB / 64 KB** — ~47 KB still free for more hot code.
- Benign gotcha: QSPI→ITCM calls exceed a direct-branch range, so the linker inserts one
  **veneer** per cross-call — once per block, negligible.

**What to move (the pattern):** tight **per-sample DSP loops** win biggest
(reverb ≫ voice render > FX). Cold/branchy control code (note-on/event handling) is a
weaker, still-unproven candidate. **Do NOT** bother promoting DATA to DTCM — `g_voice`
was tried and measured flat/noise (17 → 19), then reverted; the D-cache already covers it.

---

## ⭐ Most reliable *steady-state* cut — control-rate coeff caching

> **The most beneficial *steady-state* optimization — second only to ITCM code
> placement (above); the two are complementary (ITCM kills I-cache-miss stalls,
> this kills redundant per-block recompute).** Applying it to just three stages
> (process filter, tilt EQ, reverb damping) moved the **limit-test callback peak from
> 97 → 91 (~6%)** and `fx_total` peak **48 → 40** — with every effect still fully
> engaged. Nothing else *before ITCM* came close. When asked to cut audio CPU, do
> ITCM first, then **start here**.

**Control-rate coefficient caching.** Effects derive DSP coefficients from control
knobs using expensive math (`tanf`/`expf`/`powf`/`sin`/`cos`) and re-run it **every
audio block even when the knob hasn't moved**. Coefficients are *control-rate* — they
only change when a control turns. So: **dirty-check the raw control params and
recompute the coefficients only when one changed** (force a recompute on any
re-engage edge, since the cache is stale while a stage is off).

Why this beats everything else:
- It deletes genuinely redundant work — a predictable, measurable saving, not one
  the cache absorbs or measurement noise hides.
- It is **worst-case-valid, not gating** — the effect stays fully engaged; only an
  active *sweep* re-pays. So it lowers the "everything on" number that decides
  whether a user can ever make it click.
- Audio-identical and safe (coeff-only writes; no filter state touched).

Shipped: process filter (`be7d3ab`, a `tanf`/block) + tilt EQ (`016cb4d`, an `exp` +
4 biquads of trig/`pow`) + **reverb damping** (`3d0f258`, 2 `pow` + `exp` + 2 `sin` —
the biggest single piece). Cumulative: **callback peak 97→91, fx_total peak 48→40**,
all effects still engaged. The saturator (`TapeSaturator::PrepareBlock`) already does it.

Audit checklist: grep FX/voice setup for per-block `tanf`/`expf`/`powf`/`sin`/`cos`/
`SetFromParams`; cache each behind a dirty-check. Note the recompute usually lives in
the **un-bucketed `fx_total` remainder**, not the effect's own sub-bucket — so measure
`fx_total`/`callback` `now` with the control held **static**.

What did NOT reliably help (this project): moving **data** to RAM_D2/DTCM (already in
fast AXI SRAM; cache absorbs the rest — NOTE: this is **data**; moving **code** to ITCM
is the opposite — the biggest win ever, see the top section), float-vs-fixed (FPU makes
float free), and chasing per-bucket *peaks* (latched on the worst block, contaminated by
stalls/preemption — trust `callback` total and bucket `now`).

## How to read these

- Values are **`now / peak`** as **% of the per-block budget** (one audio block
  period; 100% = the deadline, over = a late callback / dropout).
- **`now`** = current/most-recent block. **`peak`** = worst block since the last
  peak-reset; peaks latch across the whole session, so an idle `now` can sit next
  to a `peak` left over from a prior stress test.
- See `docs/diagnostics.md` for what each metric means and how to open the overlay.

## Reference stress test ("limit test")

The deliberate worst case we harden against (not typical playing):

- Pad-type sample, tuned **+24 semitones** (4× playback ratio).
- Generous **~20% loop seam** on each side (loop-seam crossfade branch active).
- Exponential envelope, **attack & release both > 1000 ms** (voices stay alive
  long → pool fills → stealing happens *during the attack stage*).
- **All FX on** (sat + EQ + delay + reverb).
- **Rapid 5-note re-slams** — forces continuous voice stealing on attack-stage voices.

Goal: keep `callback peak` safely under 100 even here, so no end user can ever
make the instrument click.

---

## Entries

### 2026-07-02 — ITCM code campaign + polyphony 8→5 ⭐ (`dcb1abb`)

- **Commits (in order):** `90d1634` (reverb → ITCM), `849c79f` (voice render →
  ITCM), `ce9c7b9` (FX chain EQ/delay/sat → ITCM), `dcb1abb` (voice dispatcher +
  layer mix + master → ITCM). Polyphony cut (`kMaxVoices`/`kMaxVoicesPerLayer`
  8→5) landed in `54d94ac`.
- **Measurement condition:** the user's real-world stress — a MIDI **arp with all
  FX on**, at 5-voice polyphony. NOT the formal "limit test" above, so compare
  these deltas to each other, not to the poly-8 limit-test rows below.

| Metric | Before | After | How |
|---|---|---|---|
| callback pk | 96 | **84** | polyphony 8 → 5 |
| callback pk | 84 | **69** | ITCM code campaign |
| reverb pk | 20 | **11** | reverb hot path → ITCM (cleanest, patch-independent) |
| voice pk | 25 | **17** | voice render → ITCM |
| fx total pk | 47 | **33** | EQ/delay/sat/reverb/master → ITCM |

Notes:
- **This is the project's biggest CPU win to date** — see the ⭐⭐⭐ top section for
  the full why/how. Root cause: app runs from QSPI XIP, so I-cache misses on hot
  audio code stalled out to slow QSPI; ITCM never misses.
- `reverb pk` is the flagship number: reverb cost is intrinsic (fixed per-block
  math), so its bucket is patch-independent — 20→11 is unambiguously the ITCM
  effect, not measurement variance.
- ITCM usage ended at ~16.8 KB / 64 KB. DTCM/SRAM unchanged (code only).
- **Tried and reverted:** promoting `g_voice` (DATA) to DTCM — `voice pk` went
  17→19 (flat/noise), confirming data placement doesn't help on this board. Backed
  out; the `ADSR2_DTCM_DATA` machinery is not in the tree.
- Also this session (not CPU-peak but related): live-EQ-adjust clicks fixed via an
  engine-only param-repush gate (`Params::EnginePublishGen`) + EQ coeff smoothing.

### 2026-06-20 — `3d0f258` (+ reverb damping coeff caching)

- **Commit:** `3d0f258` (cache reverb damping coeffs), on `016cb4d`.
- **Change:** the reverb's `RenderWet_` recomputed 2 `pow` + an `exp` + 2 SVF
  `sin` every block even when `dmp` was static. Now dirty-checked on `damping_`
  (control-rate). The largest single coeff-recompute in the build.

| Metric | Limit test `now / peak` |
|---|---|
| callback total | **38 / 91** |
| fx total | **28 / 40** |

Notes:
- **callback peak 97 → 91**, fx_total peak 48 → 40, vs the start of the coeff-cache
  work — ~6% of worst-case headroom reclaimed (filter + EQ + reverb damping), every
  effect still ENGAGED. Reverb damping was the biggest single piece.
- Win applies while `dmp` is static; sweeping it re-pays that block (correct).
- This headroom is what funds the mono reverb anti-alias (the aliasing-click fix):
  ~91 + ~3% mono AA stays under 100.

### 2026-06-20 — `016cb4d` (filter + EQ coeff caching)

- **Commits:** `be7d3ab` (cache process-filter coeffs) + `016cb4d` (cache EQ
  coeffs), on `bf356f0`.
- **Working tree:** clean. Change vs `bf356f0`: both the process filter and the
  tilt EQ now recompute their coefficients (the filter's `tanf`; the EQ's `exp` +
  4 biquads of trig/`pow`) only when their controls actually move — control-rate
  instead of every block. Both live in the un-bucketed `fx_total` remainder.

| Metric | Idle / static, `now` | Notes |
|---|---|---|
| fx total | **30** (was 33) | filter + EQ engaged but **static** |
| filter (process) | ~1 (was 2) | static cutoff; per-block `tanf` removed |

Notes:
- ~3% of steady, always-on cost reclaimed with EQ + filter still **engaged** —
  worst-case-valid (only an active *sweep* now pays the recompute), not gating.
- Caveat: the win only applies while the controls are static. Sweeping the EQ or
  filter re-pays the recompute that block (correct + click-free).
- `eq` bucket (~1-2) is the per-sample filtering only; unaffected by this (the
  cached recompute is in `fx_total`'s remainder, not the `eq` sub-bucket).

### 2026-06-20 — `bf356f0`

- **Commit:** `bf356f0` ("perf: scale steal fade-out length to victim amplitude"),
  on `207c097`.
- **Working tree:** clean. Change vs `207c097`: steal fade-out length now scales
  with the stolen voice's amplitude — quiet (attack-stage) victims get a short
  fade down to a 0.25 ms floor; full-level victims keep the 1.25 ms fade.

| Metric | Idle (no MIDI), `now` | Limit test, `peak` |
|---|---|---|
| callback total | — | **97** |
| steal (fade-out render) | 0 | **11** |

Notes:
- vs `207c097`: `steal` 15 → 11, `callback` 100 → 97. Verified click-free by ear
  (only low-amplitude victims get the shorter fade, so no anti-click loss).
- Other buckets unchanged from `207c097` (not re-measured this entry).

### 2026-06-20 — `207c097`

- **Commit:** `207c097` ("perf: note-on cap, smoothstep steal fade, mono process
  filter, EQ neutral gate, poly 8"), on `2d8953c`.
- **Working tree:** clean (measured on this build). Changes vs `2d8953c`:
  - polyphony max 10 → 8
  - per-block note-on cap (`kMaxNoteOnsPerBlock = 2`) + deferral
  - smoothstep-shaped steal fade-out
  - global process filter made mono (was stereo)
  - EQ stage gated off when neutral (flat tilt / `eq_on` false)

| Metric | Idle (no MIDI), `now` | Limit test, `peak` |
|---|---|---|
| callback total | 43 | 100 |
| pre-voice | 6 | 19 |
| voice render | 3 | 32 |
| fx total | 33 | 48 |
| events (note-on handling) | 0 | 13 |
| steal (fade-out render) | 0 | 15 |
| fetch (sample read) | 0 | 10 |

Notes:
- `LATE` count was 0 at `callback peak ≈ 100` (right at the edge, not over).
- For comparison, the clean **`2d8953c` baseline** (poly 10, no cap) reached
  `callback peak 103` with late callbacks on the same limit test — the WIP cap is
  what brought the edge down to ~100.
- `events`, `steal`, `fetch` are all transient (0 at idle) and peak *together* at
  the slam instant — that co-incident stack is what sets the 100.
- `fx total` idle `now = 33` is high for "no notes" — likely a reverb tail still
  ringing during the reading (or the always-on FX floor). Worth confirming with a
  fully-decayed idle reading.

#### Targets being worked (this build)
- **events** — `AllocateVoice_` does a multi-pass victim scan per note-on; single-pass selection would cut it.
- **steal** — scale steal-fade length to victim env level; attack-stage victims are quiet, so they need little/no fade (cheaper *and* click-safe).
- **fetch** — dominated by +24 ratio + active seam crossfade; revisit after the above.

---

<!-- Template for new entries:

### YYYY-MM-DD — `<short-hash>`

- **Commit:** `<short-hash>` ("<subject>")
- **Working tree:** clean | dirty (list changes)

| Metric | Idle (no MIDI), `now` | Limit test, `peak` |
|---|---|---|
| callback total |  |  |
| pre-voice |  |  |
| voice render |  |  |
| fx total |  |  |
| events |  |  |
| steal |  |  |
| fetch |  |  |

Notes:
-

-->
