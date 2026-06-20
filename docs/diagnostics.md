# Diagnostics Overlay Reference

Every diagnostic parameter, organized by the overlay page it appears on.

Open the overlay with **L + R + Shift + Btn1 + Btn2**, then page through.

## Conventions

- **CPU metrics** read **`now / peak`** — left of the slash is the most recent
  block, right is the worst block since the last peak-reset. Both are **% of the
  per-block budget** (one audio block period; **100% = the deadline**, over = a
  dropped/late callback). Budget = `cpu_hz * block_seconds` (see `main.cpp`).
- **Level metrics** are **dBFS peak** (0 = full scale; floor −99.9 dB).
- **Counts** are **cumulative** since the last reset / boot.
- Source of truth for the enums and fields: `app_state_diagnostics.h`; page
  layout and labels: `src/ui/ui_overlay.cpp`.

---

## Page `cpu a` — top-level CPU

| Label | Meaning |
|---|---|
| **callback pk** | Total time for the *entire* audio callback. The headline number. `pk` > 100 = a missed deadline. |
| **pre-voice pk** | Everything before voice rendering: param tick + param push + event handling. |
| **voice pk** | Total time rendering all voices (`RenderBlock`). |
| **fx total pk** | The entire FX chain (`ProcessBlock`), with the diagnostic meter subtracted out. |
| **late count** | Cumulative callbacks that exceeded budget. **Nonzero = audible underruns.** |
| **active voices** | Voices currently sounding. |
| **per-voice** | Marginal cost of one voice (active-voice cycles ÷ active count). Shows how polyphony scales. |

## Page `cpu pre` — pre-voice breakdown

| Label | Meaning |
|---|---|
| **pre-voice pk** | Sum of the three below (same as `cpu a`'s pre-voice). |
| **params tick pk** | Smoothing/advancing parameters for the block. |
| **param push pk** | Pushing param snapshots into the voice engine (velmod lanes, keyzone, etc.). |
| **events pk** | `ProcessEvents`: note-on/off handling — allocation, voice stealing, `StartVoice_`. The note-on-storm cost (targeted by the per-block note-on cap). |

## Page `cpu fx` — per-effect cost (all inside `fx total`)

| Label | Meaning |
|---|---|
| **sat pk** | Saturator stage. |
| **eq pk** | Tilt-EQ stage. Skipped when the tilt is flat / EQ off, so this drops to ~0 when neutral. |
| **delay pk** | Delay stage. |
| **reverb pk** | Half-rate Dattorro reverb tank. Usually the largest FX bucket. |
| **master pk** | Master gain + soft-clip stage. |
| **capture pk** | Writing post-FX output to the SD render/record buffer (0 unless recording/rendering). |
| **monitor pk** | Input-monitor passthrough mix (mic/line), 0 unless monitoring. |

## Page `voice cpu` — voice-render breakdown

| Label | Meaning |
|---|---|
| **total pk** | Total voice render (same as `voice pk`). |
| **emph pk** | Emphasis-ladder time (largely inert after the one-layer pivot). |
| **active pk** | Rendering all active (non-steal) voices. |
| **steal pk** | Rendering steal-fade-out voices (the crossfade-out of stolen notes). |
| **fetch pk** | Sample fetch + interpolation. |
| **envmix pk** | Per-sample envelope stepping + per-voice mix. |
| **mix pk** | Layer-mix stage. |

## Page `voice cpu2` — fetch/seam detail

| Label | Meaning |
|---|---|
| **fetch pk** | Total fetch (same as above). |
| **seam cyc pk** | Cycles spent in the loop-seam crossfade branch of fetch. Non-seam read cost = fetch − seam. |
| **seam cnt now / pk** | How many samples this block took the seam-crossfade branch (summed across voices); now and peak. |
| **setup pk** | Per-voice block-start setup: region resolve, slot lookups, fade-threshold precompute. |
| **presim pk** | Envelope pre-simulation. ~0 now (block-rate path disabled; all voices step per-sample). |

## Page `activity`

| Label | Meaning |
|---|---|
| **events queued** | Events pushed but not yet consumed (queue depth). |
| **worker** | Background SD/worker thread status (idle/working/error). |
| **worker progress** | Progress % of the current worker op. |
| **layer a tune** | Engine tune in semitones for the layer. |
| **layer a gain** | Engine gain (dB) for the layer. |
| **voice steals** | Cumulative voice-steal count. |
| **voices active** | Current sounding voices. |

## Page `layer and mix` (levels in dBFS; layer-B fields are inert scaffolding)

| Label | Meaning |
|---|---|
| **layer b gain / mode** | Layer-B gain and loop mode — vestigial since the single-layer pivot. |
| **layer a pre / post** | Layer-A signal level before / after its gain+emphasis. |
| **layer b pre / post** | Layer-B equivalents (inert). |
| **sum before effects** | Summed dry mix level entering the FX chain. |

## Page `levels` — gain probes (dBFS) + clip counters

| Label | Meaning |
|---|---|
| **input left (line)** | Line-input level. |
| **input right (mic)** | Mic-input level. |
| **record peak** | Peak of the signal being recorded/captured. |
| **effects before master** | FX-chain output level, pre-master (`FxPreMaster` probe). |
| **final output** | Final output level post-master (`OutFinal`). **Watch this for clipping — near 0 dBFS = hitting the ceiling.** |
| **sample softclip** | Cumulative saturator soft-clip hits. |
| **master softclip** | Cumulative master soft-clip hits (only counts when the master is *boosted*). |

## Page `system`

| Label | Meaning |
|---|---|
| **user interface** | UI refresh rate (Hz). |
| **control rate** | Control-loop rate (Hz). |
| **processor load** | Overall audio CPU load (% of budget, peak-based). |
| **late callbacks** | Same overrun counter as `late count`. |
| **clip count** | Cumulative output-clip events. |
| **event overflows** | Times the event queue overflowed (dropped events — note loss under extreme MIDI rates). |
| **events handled** | Cumulative events consumed by the audio thread. |

---

## Debugging cheat-sheet

- **"Is it CPU?"** → `late count` / `late callbacks`. It increments *only* when a
  callback actually overran its deadline. Stays 0 while you hear a glitch ⇒ the
  glitch is **not** a CPU underrun (look at the signal domain instead).
- **"Am I clipping?"** → `final output` on the `levels` page. Pinned near 0 dBFS,
  or `master softclip` climbing ⇒ gain-staging / clipping, not CPU.
- **Peak vs now:** a bucket's `pk` latches *that bucket's* worst block, which may
  be a different block than the callback's worst. Don't sum bucket peaks to
  predict `callback pk` — measure `callback pk` directly under the same condition.
- The CPU peak is dominated by the **note-on transient** (attack), not
  steady-state playback. To judge steady-state, read the left-of-slash `now`
  value while a chord is held; to judge polyphony headroom, read `pk`.
