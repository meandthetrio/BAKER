# CRAFT "fresh" — STFT Spectral Exciter: Implementation Brief

> **What this document is.** A self-contained technical brief on `fresh`, a real-time
> STFT (Short-Time Fourier Transform) audio effect running on a Daisy (STM32H7,
> Cortex-M7 @ 480 MHz) embedded sampler. It covers the FFT concepts the effect is
> built on, the exact signal-processing pipeline, every design decision, and the
> CPU-optimization fight that got it running glitch-free under 100% CPU. It is
> written to be uploaded to a Claude Chat Project so the author can have a deeper,
> open-ended conversation about FFT-based DSP and brainstorm new spectral effects.
>
> **Context for the reader (Claude).** The author built this and understands it well.
> They want to *deepen their conceptual grasp of the FFT as a creative tool* and
> invent novel spectral effects beyond the current exciter behaviour. Favour
> conceptual depth, concrete DSP ideas, and tradeoff reasoning over restating what's
> below. Hardware constraints (CPU, latency, RAM) are real and are described at the end.

---

## 1. The one-paragraph summary

`fresh` is a **spectral-whitening exciter**. It was reverse-engineered from the
Prosoniq/sonicWORX "Audio Refresh" plugin. Audio is sliced into overlapping 2048-sample
windows; each window is FFT'd into a frequency spectrum; the spectrum's per-bin
magnitudes are nudged toward a flat ("white") target — which *lifts buried harmonics
up toward the level of the loud ones*, the perceptual signature of an exciter — plus an
optional "skirt" stage that sprays inharmonic sidebands around each partial for grit. The
spectrum is then inverse-FFT'd and overlap-added back into a continuous stream. It runs
**live on the audio thread** (turn a knob, hear it) with no dropouts.

---

## 2. FFT / STFT fundamentals (as used here)

### 2.1 What the FFT gives you

An FFT takes `N` consecutive time-domain samples (here **N = 2048**) and re-expresses
them as `N/2 + 1 = 1025` **frequency bins**. The transform is *lossless and invertible* —
the inverse FFT reconstructs the original block exactly. Each bin `k` is a complex number
`(re[k], im[k])` describing one frequency:

- **Bin frequency:** `f_k = k · sample_rate / N`. At 48 kHz that's **~23.4 Hz per bin**.
  Bin 0 = DC, bin 1024 = Nyquist (24 kHz).
- **Magnitude:** `mag = sqrt(re² + im²)` — *how much* of that frequency is present.
- **Phase:** `atan2(im, re)` — *where in its cycle* that frequency sits at the window's
  start.

The key mental model: **the FFT rotates audio into a basis where pitch/timbre is a
directly addressable axis.** All spectral effects are "reach into the 1025 bins, change
something, transform back."

### 2.2 Why "Short-Time" — the STFT scaffold

A single FFT is a static snapshot. To process a *stream* you repeat the FFT on
**overlapping windows** — the Short-Time Fourier Transform. The pipeline per frame:

1. **Analysis window** — multiply the 2048-sample frame by a `sqrt(Hann)` window so the
   block edges taper to zero. Without windowing, the FFT treats the block as periodic;
   the discontinuity at the wrap smears energy across all bins ("spectral leakage").
2. **Forward FFT** → spectrum.
3. **Modify the bins** ← the entire creative surface.
4. **Inverse FFT** → time-domain frame.
5. **Synthesis window + overlap-add (OLA)** — window again, then sum overlapping frames.

### 2.3 COLA and the sqrt-Hann trick

Frames overlap by 50% (**hop = 1024**). `fresh` applies `sqrt(Hann)` at *both* analysis
and synthesis, so the effective per-frame envelope is `sqrt(Hann)² = Hann`. A Hann window
at 50% overlap satisfies the **Constant Overlap-Add (COLA)** condition: the summed
windows equal exactly 1.0 everywhere. That's why the output is seamless instead of
amplitude-modulating ("pulsing") at the hop rate. This was verified by a host probe:
identity pass-through reconstructs the input at **−137 to −149 dB residual, +0.000 dB
gain, with no normalization fudge**.

### 2.4 The latency tradeoff (fundamental)

You can't emit a sample until its entire window has arrived, so STFT inherently delays
the signal. `fresh`'s reported latency is **kFftSize + kHopSize = 3072 samples** (~64 ms
@48k): one frame of base STFT delay plus one extra hop from the CPU-spreading pipeline
(see §5). Bigger FFT → finer frequency resolution but more delay and more CPU. **This
resolution-vs-latency tradeoff is the defining knob of all FFT processing.** The offline
render trims the leading latency so printed samples aren't time-shifted; the live
audition just tolerates the delay.

---

## 3. The `fresh` pipeline in detail

Source: `src/dsp/craft/craft_refresh.{h,cpp}`. The per-frame work is split into **7
phases** (see §5 for why). Conceptually they group into: **window → forward FFT →
spectral processing (whiten, linearize, skirt) → inverse FFT → OLA**.

### 3.1 The spectral processing — three independent operations

All three operate only on **magnitude** (phase is preserved everywhere — see §6, a key
creative gap). Each is a **bipolar dial, −20…+20, 0 = neutral**, exposed as a CRAFT
plugin param.

#### (a) Whitening / tilt — `tilt` param

The core exciter behaviour. Each bin's magnitude is driven toward a flat target `T`:

```
gain[k] = exp( whiten_w · (T − logmag[k]) )
```

- `T` = **magnitude-weighted mean of the smoothed log-magnitude** across bins. Weighting
  by magnitude keeps near-silent bins from dragging the target down toward silence on
  sparse spectra.
- `logmag[k]` is a **time-smoothed** magnitude — a per-bin one-pole filter
  (`kTemporalAlpha = 0.20`, frame rate ~43–47 Hz). This is *sustain detection*: sustained
  tones get whitened; white noise (random per-frame fluctuation whose time-average is
  flat) and one-frame transients do not. This was tuned to match the original plugin's
  behaviour.
- `whiten_w` is **signed**, with asymmetric slopes:
  `+20 → ~+20 dB tilt` (`kBrightSlope = 0.0617`, full whitening toward flat),
  `−20 → ~−6 dB tilt` (`kDarkSlope = 0.0180`, "anti-whiten": emphasize peaks, cut the
  floor — a gentler dark range, chosen because going darker sounds muddy).
- Per-bin gains are clamped to **−48…+18 dB** so deep spectral valleys don't explode when
  lifted.

#### (b) Frequency Linearization — `linr` param (No / Quarter / Half / Full)

Blends the per-bin *target* between the global flat level and the input's **local spectral
envelope** (a box-average of log-magnitude over ±24 bins). `kLinBeta = {0, 0.30, 0.55,
1.0}`. At β=0 you get full flat whitening; at β=1 the target tracks the local frequency
balance, so whitening mostly fills in *local* dips rather than flattening the whole
spectrum. It controls *how aggressively the tonal balance is rewritten*.

#### (c) Skirt — `skirt` param (the actual "exciter" kernel)

A **convolution along the frequency axis** — each partial sprays energy onto its
neighbour bins:

- **skirt > 0 (add):** complex convolution-add. Each bin contributes a coherent
  (phase-matched) pedestal of inharmonic sidebands to its ±`kSkirtWidth` (= 4) neighbours
  (±~86 Hz), HF-rolled by the `hf[]` weight. At full +20 the sidebands sit ~−6 dB under
  the carrier — a very hot, gritty exciter (the original measured kernel was ~−60 dB;
  raised by author choice). This is what makes `fresh` *add* harmonics, not just re-balance
  them.
- **skirt < 0 (reduce / de-skirt):** a magnitude-domain "purify." Each bin is scaled down
  by how far it sits below its local max within ±W — a true local peak is untouched, a
  deep sideband hugging a peak is attenuated. (A *complex subtract* fails here: sideband
  phase is uncorrelated, so subtraction raises them instead. Magnitude scaling is the
  only thing that works.) This is an author design extension with no reference data.

#### (d) `roll` param — skirt frequency rolloff

Recomputes the per-bin skirt weight `hf[]`: 0 = flat, `<0` = HF rolloff (corner sweeps
20k→1k), `>0` = LF rolloff (corner sweeps 20→1k). Lets the skirt be biased bright or
dark. Computed entirely in the **log-frequency domain** against a precomputed per-bin
`log(f)` table so the recompute is just compare+subtract+multiply per bin.

### 3.2 Constant-level operation (deliberate departure from the original)

The original plugin's Intensity rides output level by ~±13 dB (its makeup/excitation gain),
which on broadband material swings ~25 dB and makes the tonal effect impossible to
audition. For sampler use this was **removed**: after whitening, each frame is
**energy-normalized so output loudness == input loudness**. The skirt stage has its own
post-normalization back to the pre-skirt energy. Net result: **Intensity = "amount of
refresh" (tone only), not loudness.** Level stays flat (±0.1 dB) at all settings; only the
dark↔bright tonal sweep moves. (The faithful level-riding version exists in git history.)

---

## 4. How it was decoded (provenance)

`fresh` is a black-box reverse-engineering of sonicWORX **Audio Refresh** (one of 25
Prosoniq plugins decoded by rendering test signals through the original in an emulator and
analyzing the outputs). Decoded facts that drove the implementation:

- **STFT geometry:** L=2048, hop=1024, 50% overlap, sqrt-Hann analysis+synthesis, COLA.
  (Fs-agnostic in samples; measured @44.1k.)
- **Makeup gain on flat/white input is linear in dB:** `makeup_dB ≈ 0.414·(Intensity −
  102)` (white renders: I70=−13.3, I100=−0.9, I130=+11.8 dB, flat across octaves). Unity
  ≈ I102. This was *removed* under constant-level operation (cancels under normalization).
- **Whitening depth** on a lone tone is gentler & ~linear (−2.8…−5.4 dB across the range).
- **The target is flat/white;** the tilt seen on pink input is a byproduct of whitening a
  non-flat spectrum.
- **The skirt kernel was extractable** from purpose-built test signals (`ar_kernA/B`):
  sidebands on every bin, flat ~−60 dB below carrier out to ±172 Hz, HF-rolled, scaling
  with Intensity. (Lesson learned the hard way: the skirt was initially mislabeled
  "irreducible" by conflating it with the genuinely-irreducible whitening gain-curve, and
  skipped — because the calibration loop only measured level + octave-tilt and was blind
  to a −60 dB skirt.)
- The genuinely **irreducible core** is the exact per-bin gain-vs-local-deviation curve
  (envelope width + gain shape + peak-cut saturation): faithfully *imitable*, not a
  bit-clone.

---

## 5. The CPU fight — 546% → 91% callback (every technique that worked)

This is an embedded target. The effect first measured at **546% of the audio-callback
budget** (i.e. 5.5× too slow to run in real time). Each step below was measured on-device
(CPU-overlay buckets), in order — "measure, don't guess":

1. **FFT re/im scratch: SDRAM → RAM_D2.** 546 → 205. *(Strided FFT access on external
   SDRAM was the single biggest monster.)*
2. **Hand-rolled radix-2 → CMSIS `arm_cfft_f32` (radix-8).** 205 → 163.
3. **newlib `log`/`exp` → branchless `FastLog_`/`FastExp_` (Mineiro approximations).**
   newlib-nano's `logf`/`expf` are ~800-cycle software calls; at 1025 bins/frame those
   loops alone spiked the callback. The approximations are ~50× cheaper, accurate enough
   for tonal whitening (host-verified: log abs-err 1e-4, exp rel-err 0.02%).
4. **CMSIS twiddle + bit-reversal tables (23 KB) copied into DTCM.** They were blowing the
   16 KB D-cache and thrashing QSPI flash. 165 → 130.
5. **Whole `RefreshState` (~64 KB/slot): SDRAM → RAM_D2** (`.ram_d2_bss`). Killed the last
   per-hop spike — `outAccum` OLA, the 12 KB accumulator memmove, `win`, and the FIFOs
   were all still on SDRAM. voice 122 → 80.
6. **`arm_cfft` → `arm_rfft_fast_f32` (real FFT) — the big FFT win.** The STFT input is
   *real*, so the real FFT is ~half the work and drops the manual re/im interleave. The
   spectral stage now operates on a one-sided spectrum `re/im[0..N/2]` (removed all
   conjugate-mirror writes; loops became `0..Nb`). **voice 56 → 39.**
7. **7-phase split of the per-frame work** (window | fwd-FFT | spectral-2a | spectral-2b |
   skirt | iFFT | OLA+slide), driven **one phase per `Process()` call** with `kPhaseGap=1`
   (so 7×2 = 14 blocks < 21 blocks per hop). No single audio block ever does the whole
   transform plus its surrounding memory shuffle. voice 80 → 56. A one-hop output delay
   (double-buffered emit + `readSel_`) gives ~21 blocks to finish a frame; a `while(phase_)`
   at hop-fill is the safety net and force-completes for the offline render.
8. **`kSkirtWidth` 8 → 4.** The skirt convolution is the top spectral phase; halving the
   neighbour loop cut it (~89% → ~53% add). Narrows sidebands ±172 → ±86 Hz (sound
   tradeoff, accepted).
9. **`hf[]` rolloff = control-rate coefficient caching.** Gate the 1025-bin `hf[]`
   recompute to only when `roll` actually changes; precompute `log(freq)` per bin so the
   recompute is compare+sub+mul (no per-bin divide, no per-bin FastLog). This killed the
   roll-scroll spike that stacked on the skirt block. (Control-rate coeff caching is the
   project's #1 CPU directive: recompute expensive coeffs only when controls move.)
10. **Window precompute off the audio thread.** `BuildTables_` ran 2048 software `cos`
    calls in `Reset()` on the audio thread on first engage = a >1-block spike that
    clicked. Now a shared window is built once off-thread; `BuildTables_` just memcpys it.

**Result: glitch-free, hardware-confirmed. Worst-case callback peak 91%; default fresh
52%; voice/FFT phase 39%.**

### 5.1 Two *audio* clicks (not CPU) and their fixes

- **Loop-seam tick:** the old output-domain gap-fade dipped to zero each loop = audible
  periodic tick, worse for a latent effect like fresh. Fix = a **source-domain
  raised-cosine fade** to zero at both loop boundaries (~5 ms), applied in the source
  domain so it covers fresh's 3072-sample latency. *An equal-power tail/head crossfade was
  tried first and failed* — overlapping two phase-misaligned signals combs (a glitch of
  its own). A fade never overlaps anything.
- **Plugin-engage click:** swapping a plugin mid-audition steps the dry→wet content. Fix =
  **deferred re-trigger**: on a plugin change (not a param edit) fade output out ~5 ms,
  apply the swap at silence, fade back in. Param-only edits stay immediate/smooth. *You
  can't pre-empt a discontinuity you learn of as it lands — defer the apply to silence.*

### 5.2 Memory layout (final)

- `RefreshState` (incl. `outAccum`/`win`/FIFOs/`logf`) → **RAM_D2** (`.ram_d2_bss`,
  3 slots × 64 KB = 192 KB; RAM_D2 ~86%).
- FFT `re`/`im` scratch → **RAM_D2**.
- CMSIS real-FFT twiddle/bit-rev tables + `cbuf` scratch → **DTCM** (~67%; built as a
  manual `arm_rfft_fast_instance_f32` so its tables live in fast on-chip RAM, not QSPI).
- Shared `sqrt(Hann)` window → SRAM.

---

## 6. Where the creative room is (open conceptual questions)

`fresh` currently manipulates **only magnitude**, and does so **per-frame, mostly
independently**. The FFT has more dimensions that are completely untapped here. These are
the most fertile directions for *new* spectral effects on the same 7-phase scaffold (only
the spectral phases change):

- **Phase manipulation (biggest gap).** Whitening multiplies `re` and `im` by the same
  real gain → magnitude changes, phase is preserved. But phase is half the data and
  enables sounds impossible in the time domain:
  - *Phase randomization* (keep magnitude, randomize phase) → turn any sound into a smooth
    noise/pad with the same spectral shape — the classic spectral-blur/freeze texture.
  - *Phase zeroing* → robotization / metallic monotone.
  - *Phase-vocoder phase tracking* (advance phase between frames) → pitch shift independent
    of time. (The project already has `signalsmith-stretch` doing this elsewhere.)
- **Cross-frame memory.** Currently only `magavg` (a per-bin one-pole). Generalize:
  - *Spectral freeze* — stop updating the spectrum, keep re-synthesizing one held frame →
    infinite sustain.
  - *Spectral gate / denoise* — learn a per-bin noise floor and subtract it (spectral
    subtraction).
  - *Spectral delay* — delay each bin (or band) by a different amount → smearing, blurred
    reverbs, frequency-dependent arrival times.
- **Cross-bin operations.** The skirt is the only one. Others:
  - *Bin shifting* = frequency shifting (inharmonic, not pitch shift).
  - *Bin stretching / resampling the magnitude array* = formant shift / spectral warp.
  - *Spectral morphing* = interpolate magnitudes between a live and a stored spectrum.
  - *Harmonic combing* = keep bins near `f, 2f, 3f…` of a detected pitch, kill the rest →
    vocoder-like purification.

The highest-leverage next step is **using phase**: every magnitude-only effect lives in
the EQ/exciter/filter family and tends to sound related, whereas the moment you touch
phase you get sounds that justify paying the FFT's latency and CPU tax in the first place.

---

## 7. Hardware / platform constraints (for grounding any new ideas)

- **MCU:** STM32H7 (Cortex-M7) @ 480 MHz, single-precision FPU (float is "free"; double
  is software-emulated and slow).
- **Audio-thread budget:** worst-case callback already at ~91%. New per-frame spectral
  work must fit the remaining headroom, or be spread across more phases / cached at
  control rate. Steady-state vs transient peaks differ — measure the worst case.
- **Memory tiers (fastest → slowest):** DTCM (zero-wait, ~64 KB, tightest) → AXI-SRAM →
  RAM_D2 (where the STFT state lives) → external SDRAM (avoid for strided/per-sample
  access; it was the original 546% killer). SRAM is ~91% full overall.
- **3 CRAFT slots** can each hold a plugin; `fresh` is one of them. Each slot owns its own
  ~64 KB `RefreshState`.
- **No note context.** CRAFT effects process/print an offline sample buffer with no MIDI
  note — any "key-tracking"-style parameter must be repurposed (e.g. `dial`'s `track`
  became `beat`).
- **Latency is acceptable for printing** (rendered samples are latency-trimmed) and
  tolerated for live audition.

---

## 8. Key constants quick-reference

| Constant | Value | Meaning |
|---|---|---|
| `kFftSize` | 2048 | FFT / window length (samples) |
| `kHopSize` | 1024 | hop (50% overlap) |
| bins | 1025 | `N/2 + 1` (one-sided real spectrum) |
| bin spacing | ~23.4 Hz | @48 kHz |
| latency | 3072 samples | `kFftSize + kHopSize` (~64 ms @48k) |
| `kTemporalAlpha` | 0.20 | per-bin magnitude smoothing (sustain detection) |
| `kBrightSlope` | 0.0617 | tilt +20 → ~+20 dB whitening |
| `kDarkSlope` | 0.0180 | tilt −20 → ~−6 dB anti-whitening |
| gain clamp | −48 … +18 dB | per-bin |
| `kSkirtWidth` | 4 | skirt convolution half-width (±~86 Hz) |
| `kEnvHalfWidth` | 24 bins | local-envelope smoothing for linearization |
| `kLinBeta` | {0, 0.30, 0.55, 1.0} | No/Quarter/Half/Full linearization |
| worst callback | ~91% | hardware-confirmed peak |

### Params (all bipolar −20…+20, 0 = neutral)

| Param | Role |
|---|---|
| `tilt` (p0) | whitening depth (signed: dark ↔ bright), level-normalized |
| `linr` (p1) | frequency linearization: No / Quarter / Half / Full |
| `skirt` (p2) | add inharmonic sidebands (exciter) ↔ de-skirt (purify) |
| `roll` (p3) | skirt frequency rolloff (HF ↔ LF bias) |

---

*Source files: `src/dsp/craft/craft_refresh.{h,cpp}`, host probe
`tests/craft_refresh_probe.cpp`. Decode notes: `~/Desktop/sonicworx_re/RESULTS.md`
(§Audio Refresh).*
