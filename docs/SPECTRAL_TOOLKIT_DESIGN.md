# Spectral Toolkit — Design Brief

**Status:** design / pre-implementation
**Date:** 2026-06-29
**Scope:** the fundamental tools to build on top of the existing STFT analysis
(`src/dsp/craft/craft_refresh.*`) so the five "Home Frequency" spectral effects
can be built piece-by-piece instead of black-boxed one at a time.

---

## 0. Why this doc exists

We have a working, CPU-tamed STFT host on the device (the `fresh` exciter). The
next five effects (from *Manifold — Five Signals, Named*) are all spectral-domain
processors. Rather than implement each as a standalone plugin that re-rolls its
own framing and analysis, we first build the **shared fundamental tools** every
one of them needs, then each effect becomes only its small spectral-domain
operation.

The five effects and the single core operation each reduces to:

| Effect | Working name(s) | Core per-frame operation | Domain |
|---|---|---|---|
| **Spectral Delay** | Arrival, Lag, Driftwood, Late Channel, Undertow | Delay each bin independently (lows lag, highs lead) | multi-frame history |
| **Phase Randomization** | Haze, Bloom, Dissolve, Static Field, Half-Remembered | Keep `mag`, replace `phase` with noise | polar |
| **Phase Zeroing** | Totem, Monolith, Lockstep, Carrier, Iron Hour | Keep `mag`, set all `phase` equal | polar |
| **Spectral Freeze** | Keepsake, Hold, Still, Pressed, The Long Now | Hold one frame's `mag`, keep advancing `phase` | polar + propagation |
| **Spectral Thickening** | Choir, Kindred, Congregation, Halo, Resonance | Detect peaks, add octave/fifth/third sidebands at literal freqs | peaks + splatter |

**Key observation:** three of the five operate in **magnitude/phase (polar)**
form. The current `fresh` pipeline works entirely in **rectangular `re`/`im`** —
it computes `sqrt(re²+im²)` for magnitude but **never extracts phase**. That gap
is the first real tool to build.

### A note on the third (from the source doc, p.3)

Octave (2×) and fifth (3/2) are *open* intervals — they reinforce without
committing to major/minor, which is why they're the safe default for a thickener
that sits over arbitrary input. A **major third** commits the sound to major and
will clash over minor-key material. One genuine advantage of a bin-based build:
placing a sideband at a **literal just frequency (5:4)** sounds sweeter and more
fused than an equal-tempered third (which beats slightly). This is *why* the peak
detector below needs sub-bin frequency precision and the splatter needs
fractional-bin placement — they're what make the "literal just third" feature
possible. Default to octave/fifth; expose third as an opt-in.

---

## 1. What the current code already gives us (the reusable substrate)

`craft_refresh.cpp` is more than an exciter — it contains a complete,
CPU-budgeted STFT host. Everything below is already proven on hardware
(worst-case callback 91%, glitch-free; see memory `craft-live-audition-and-fresh-noise`).

- **Framing engine** — 2048-pt window, 1024 hop, 50% overlap, √-Hann at both
  analysis and synthesis (product = Hann, perfect COLA), overlap-add,
  double-buffered output, one-hop output delay. Latency `kFftSize + kHopSize`.
- **The 7-phase spreader** (`RunPhase_` + the driver in `Process`) — splits one
  frame's heavy work across ~21 audio blocks (gap 1: `7*(1+1)=14 < 21`) so no
  single block overruns. **This is the most valuable existing asset.** Every new
  effect MUST plug into this rather than rolling its own framing, or it will blow
  the per-block budget on engage.
- **Real FFT** via CMSIS `arm_rfft_fast_f32` over **DTCM-resident** twiddle/bit-rev
  tables (`RfftFwd_`/`RfftInv_`). One-sided spectrum, `kFftSize/2 + 1 = 1025` bins.
- **`logf[]`** — per-bin centre-frequency natural logs (constant, precomputed).
- **`FastLog_` / `FastExp_`** — branchless FPU approximations (~50× cheaper than
  newlib software libm; host-verified accuracy). The pattern to copy for any new
  transcendental needed per-bin.
- **Control-rate coeff caching** — recompute 1025-bin coefficient tables only when
  a control actually moves (the `roll_` dirty-check). Project-wide CPU discipline
  (memory `control-rate-coeff-caching`).

### Current memory layout (the starting point)

From `craft_chain.cpp`:

```
RAM_D2 (.ram_d2_bss, NOLOAD):
  g_refresh_state[3]   3 × sizeof(RefreshState) ≈ 3 × 64 KB = 192 KB
  g_fft_re[2048]       8 KB
  g_fft_im[2048]       8 KB
                       ────────
                       ~208 KB used  (RAM_D2 total = 256 KB → ~48 KB free)

DTCM (.dtcmram_bss):
  g_fft_cbuf[4096]            interleaved complex FFT scratch (zero-wait transform)
  g_rfft_cfft_tw / _brev / _tw   CMSIS real-FFT tables copied out of slow QSPI flash
  g_rfft_dtcm                    the rfft instance bound into every slot
```

`RefreshState` (per slot, RAM_D2) currently holds: `inFifo`, `frame`, `outFifo`,
`outAccum[2*N]`, `re`/`im` pointers (→ the shared DTCM/RAM_D2 scratch), `win`,
`logf`, `logmag`, `env`, `magavg`, `hf`.

**Hard-won placement lessons (do not relearn):**
- The strided per-bin FFT working set MUST be on-chip (RAM_D2/DTCM). Putting it on
  external SDRAM spiked the callback ~122%/hop and glitched. SDRAM is only safe for
  data accessed **sequentially / whole-frame**, never in the butterfly.
- `.ram_d2_bss` and `.sdram_bss` are **NOLOAD** — not zeroed at startup; `Reset()`
  must `memset` them.
- Anything written from a **pre-main global constructor** must not touch SDRAM
  (SDRAM isn't up until `hw.Init()`). Bind pointers pre-main, apply them in
  `Reset()`. (This is why `BindFftScratch` stores and `Reset` assigns.)

---

## 2. The fundamental tools (dependency order)

### Tool 0 — Extract a shared `SpectralEngine` from `CraftRefresh`

**The real "don't black-box it" move.** Today the framing/FFT/OLA/7-phase
machinery is welded to the whitening op. Pull it out into a reusable host whose
only variable is the spectral-domain callback:

```
class SpectralEngine {
  void Reset(float sr);
  void Process(float* buf, uint32_t n);   // owns FIFO, OLA, the 7-phase spread
  // The per-frame spectral op, supplied by each effect. Called with the
  // one-sided spectrum already forward-transformed; writes it in place.
  using SpectralOp = void(*)(void* ctx, float* re, float* im, int nbins);
};
```

Each of the five effects then becomes **only** its `SpectralOp` (plus its own
small state). `fresh` becomes the first client of the extracted engine — porting
it onto the engine is the regression test that the extraction is faithful.

Without this, every effect copy-pastes ~200 lines of framing and the 7-phase
driver, and any framing fix has to be made five times.

*Memory:* no new allocation; reorganization of existing `RefreshState`.

---

### Tool 1 — Polar decompose / recompose (`mag[]`, `phase[]`)

*Needed by:* Phase Randomization, Phase Zeroing, Spectral Freeze. Foundation for
all phase work.

- Forward: `mag[k] = hypot(re,im)`, `phase[k] = atan2(im,re)`.
- Inverse: `re = mag·cos(phase)`, `im = mag·sin(phase)`.

**This is the CPU risk.** `atan2` + `sincos` over 1025 bins is the expensive
newcomer. Build `FastAtan2_` / `FastSinCos_` as siblings to the existing
`FastLog_`/`FastExp_`, and make the polar conversion **its own phase** in the
spreader (don't fold it into an existing heavy block).

*Memory:* `mag[1025]`, `phase[1025]` → **RAM_D2**. ~4 KB each.

---

### Tool 2 — Per-bin phase propagation (`phaseAccum[]`)

*Needed by:* Spectral Freeze (mandatory), and it keeps Thickening sidebands
coherent.

A held frame replayed with frozen phase **buzzes** (static spectrum). For a
musical freeze you keep `mag` but advance each bin's phase every hop by its
nominal increment `2π·k·hop/N` (optionally plus the measured per-bin deviation —
true phase-vocoder propagation). This is the phase-propagation primitive.

*Memory:* `phaseAccum[1025]` → **RAM_D2** (~4 KB). Nominal increment is derivable
from `k`, no table needed (or precompute once if cheaper).

---

### Tool 3 — Peak / partial detector

*Needed by:* Spectral Thickening (the user's own example); useful to Freeze and to
the existing de-skirt.

- Find local maxima in `mag` above a floor.
- **Parabolic (3-point) interpolation** for sub-bin frequency precision — this is
  what enables placing a sideband at the *literal just third (5:4)* rather than at
  the nearest bin (see §0 note).
- Output a capped list of `{bin, true_freq, mag, phase}` (e.g. ≤ 64 peaks).

*Memory:* peak list ≤ 64 entries ≈ 1 KB → **DTCM** (tiny, random-access, hot).

---

### Tool 4 — Fractional-bin splatter (copy-add)

*Needed by:* Spectral Thickening.

"Add a complex value at non-integer target bin `f` with gain `g`," distributing
across the two straddling bins. Targets: octave `2f`, fifth `1.5f`, third `1.25f`.
Use the peak's own phase (coherent, via Tool 2) so sidebands fuse rather than
chorus.

*Memory:* needs a temp accumulation spectrum to avoid read/write aliasing — reuse
the existing `env[]` / `logmag[]` scratch in the engine working set. No new big
buffer.

---

### Tool 5 — Multi-frame spectral history ring

*Needed by:* Spectral Delay. (Spectral Freeze's capture is just depth-1 of this.)

A ring of past frames' `mag`+`phase`, so each bin can be read back delayed by its
own amount (lows lag, highs lead). **This is the one large allocation and the only
one that does not fit on-chip.**

Sizing (frame rate = 48000 / 1024 ≈ **46.9 frames/sec**):

- Per frame: `1025 bins × (mag+phase) × 4 B` ≈ **8.2 KB**.
- 0.5 s ≈ 24 frames ≈ **190 KB**.
- 1.0 s ≈ 47 frames ≈ **385 KB**.
- 2.0 s ≈ 94 frames ≈ **770 KB**.

→ **SDRAM** (`.sdram_bss`, NOLOAD, memset in `Reset`). Safe there **only because
it is accessed whole-frame / sequentially** — it is not in the FFT butterfly, so
it dodges the strided-SDRAM stall that forced the FFT working set on-chip. Keep
access patterns sequential (read/write a contiguous frame, or a contiguous bin
column) to stay cache-friendly.

---

## 3. Memory placement summary

| Data | Size | Section | Why |
|---|---|---|---|
| Per-bin working spectra (`mag`, `phase`, `phaseAccum`) | ~4 KB each | **RAM_D2** (`.ram_d2_bss`) | Touched every frame, strided/random — SDRAM stalls here (proven). Fits in the ~48 KB free. |
| Peak list (≤ 64 entries) | ~1 KB | **DTCM** (`.dtcmram_bss`) | Tiny, random-access, latency-critical. |
| FFT scratch / twiddle tables | existing | DTCM / RAM_D2 | Already placed; unchanged. |
| Spectral history ring (delay / freeze) | ~8 KB/frame; 0.5 s ≈ 190 KB, 1 s ≈ 385 KB | **SDRAM** (`.sdram_bss`) | Too big for on-chip. Safe only with whole-frame/sequential access. |

### Two budget warnings

1. **RAM_D2 is the tight resource, not SDRAM.** After the existing ~208 KB,
   only ~48 KB is free. Three slots × (mag+phase+accum ≈ 12 KB) = 36 KB — it fits,
   but barely. **Strongly consider making the polar/accum buffers part of the
   shared `SpectralEngine` working set** so spectral plugins reuse one set rather
   than ×3. (Confirm first that phases don't run concurrently across slots in a way
   that would corrupt a shared buffer — the 7-phase driver interleaves slot work.)

2. **CPU, not memory, is the likely wall.** `fresh` already peaks ~91%. The polar
   conversion (Tool 1) is the expensive addition — keep it on the fast-approx path
   (`FastAtan2_`/`FastSinCos_`) and give it its own spreader phase. Re-measure
   worst-case callback after Tool 1 lands, before building on top of it.

---

## 4. Recommended build order

| # | Step | New tools | Rationale |
|---|---|---|---|
| 0 | Extract `SpectralEngine`; reparent `fresh` onto it | Tool 0 | Pure substrate. `fresh` is the regression test. No new effect. |
| 1 | **Phase Zeroing** | Tool 1 | Cheapest real effect — polar only, no RNG, no history. Validates decompose→recompose through the new engine. |
| 2 | **Phase Randomization** | (RNG) | Adds only a per-bin random phase source. |
| 3 | **Spectral Freeze** | Tool 2 | Adds phase propagation + depth-1 capture. |
| 4 | **Spectral Thickening** | Tools 3 + 4 | Peak detect + fractional-bin splatter. The just-third feature. |
| 5 | **Spectral Delay** | Tool 5 | Most memory (SDRAM ring) and most plumbing — do last. |

Each step is independently auditionable and ends with a worst-case CPU
re-measure.

---

## 5. Open questions to resolve before coding

- **Shared vs per-slot working set** — can the polar/accum buffers be a single
  shared set, given the interleaved 7-phase scheduling across slots? (Resolves the
  RAM_D2 budget squeeze.)
- **Spectral Delay max time** — 0.5 s vs 1 s vs 2 s sets the SDRAM ring size.
- **Phase-propagation fidelity for Freeze** — nominal-increment only (cheap,
  slightly synthetic) vs nominal + measured deviation (true phase vocoder, more
  CPU/state).
- **Polar everywhere vs per-effect** — does `SpectralEngine` always produce
  `mag`/`phase` (simpler API, wasted work for rectangular effects like `fresh`),
  or is polar an opt-in stage the effect requests?
