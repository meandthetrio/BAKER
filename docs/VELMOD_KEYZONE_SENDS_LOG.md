# Velmod Keyzone Sends — Development Log

Per-voice, velocity-driven sends from the keyzone velmod lanes into the global
reverb / delay / sat effects. This log records what was built, what we learned,
and which fix resolved which problem.

---

## What we accomplished

A keyzone velmod lane can target **reverb**, **delay**, or **sat** (in addition
to volume/attack/sustain/release). Each sounding voice contributes a
velocity-scaled, **pre-emphasis** copy of itself into that effect — so harder
hits throw more of the voice into the reverb tail / echo / saturation, fully
**independent of the effect's own fader** (the effect can even be globally off).

Supporting changes that made sends behave predictably:
- **Removed inherent velocity→volume** (and the dead `vel_brightness`): velocity
  now does nothing unless a velmod lane says so, so a send's velocity response
  isn't muddied by the voice also changing loudness.
- **Sends max at +6 dB** (`kSendMaxGain = 1.995`); **default shape = gate**.
- **Velmod config is saved in projects** (manifest v20, with v19→v20 migration).
- **Knee velocity curve**: sends use a `1.5` power curve (more top-end contrast);
  volume/ADSR stay linear.

### Architecture (one paragraph)
Per-voice `send_level[3]` is frozen at note-on (`StartVoice_`). In the render
loop, each voice's post-env / pre-emphasis sample is tapped into one of three
mono send buses (`send_bus[k] += voice_sample * send_level[k]`), gated by a
per-voice `send_active` flag (idle = zero cost). The buses cross the object
boundary into `AudioEngine::ProcessBlock`, which feeds each into the matching
effect's **wet generator at full level**, bypassing the global dry/wet fader.
Full detail in the `velmod-send-architecture` memory note.

---

## Problems → diagnosis → fix (chronological)

### 1. Reverb send was inaudible when the reverb fader was down
- **Why:** the original routing (Option B) injected the send into the dry bus
  *before* the effect, so the effect's own input/output mix scaling killed it.
  In SEND mode (the default) the reverb fader scales the tank input, so a
  fader-down send got multiplied by ~0.
- **Lesson:** these effects default to **send mode**, not insert — a send wants
  an always-wet return, not to ride through the insert's dry/wet.
- **Fix:** **true wet-send** — feed the send into each effect's wet generator at
  full level. Reverb: into the tank input, wet returns at unity. Sat: through
  the saturator, added at unity. (Delay handled later — see #4–6.)

### 2. Reverb send tail cut off the instant the note ended
- **Why:** with reverb globally off, the reverb stage only ran *while a voice was
  sending*. When the voice finished (right after note-off), the send stopped, the
  stage stopped, and the tank's tail was never rung out.
- **Fix (attempt):** wire the send into the reverb tail state machine so it tails
  out like a global reverb toggle-off (`p.reverb_on || rev_send_present`).

### 3. That tail fix appeared to do nothing
- **Why:** two writers fought over `reverb_active_`. The top of the block set it
  `true` (send present); the **bottom-of-block tail bookkeeping reset it to
  `false` every block** (because reverb was globally off). So the falling-edge
  tail trigger never saw `reverb_active_ == true` when the send stopped.
- **Lesson:** when adding a new "wants on" source to a gated effect, audit
  **every** writer of the active flag, not just the obvious transition.
- **Fix:** don't clear the active flag while a send is feeding it
  (`!p.reverb_on && !rev_send_present`). Applied the same to delay.

### 4. Reverb send didn't scale enough across velocity 64→127 (knee)
- **Why:** linear knee (`scale = vel/127`) gives only **+6 dB** across the top
  half of the velocity range; as a diffuse send under a constant-volume dry
  voice, that reads as "flat."
- **Fix:** power-curve the knee for **send targets only** (`1.5`), so harder hits
  scale disproportionately; volume/ADSR stay linear (direct, already audible).

### 5. Delay send was still fader-dependent (unlike reverb)
- **Why:** reverb's fader scales the tank **input** (wet returns at unity), but
  delay's fader scaled the **output** — so a send shared that one output level.
- **Fix:** **restructured delay to mirror reverb** — in SEND mode the fader now
  scales what goes *into* the line, and echoes return at **unity**. The velmod
  send feeds at full → fully fader-independent. (Global delay's perceived level
  is unchanged; only *where* the fader applies moved from output to input.)

### 6. Delay send produced no effect at all
- **Why (delay-specific, reverb can't hit it):** a delay has a **gap** between
  writing a signal and hearing its echo (up to the delay time). The tail's
  quiet-detector watches the **output**; after ~100 ms of pre-echo silence it
  declared the tail done and **cleared the buffer**, wiping the send before it
  could echo.
- **Fix:** don't arm the quiet-stop until an echo has actually been heard
  (`delay_tail_heard_`), and raise the delay tail cap so long delays fit.

### 7. Delay feedback gave only one echo regardless of fdbk
- **Why:** same trap one level deeper — feedback echoes are spaced by the **delay
  time** (up to 1000 ms), but the quiet window was 100 ms. After echo #1, the gap
  before echo #2 tripped the quiet-stop and cleared the fed-back signal.
- **Lesson:** a delay tail isn't "done" when the output is momentarily silent —
  the line still holds in-flight feedback. The quiet window must **exceed the
  delay time**.
- **Fix:** gave the delay its own quiet window
  (`kDelayQuietBlocksToStop = 1200` ≈ 1.2 s, vs reverb's 100), so each echo
  resets the counter and it only fires once the train has truly decayed;
  `kDelayTailMaxBlocks = 3600` caps runaway feedback.

---

## Key takeaways
- **Sends vs inserts:** reverb/delay default to *send* mode; treat the per-voice
  send as just another source into the effect's input bus, returning wet at unity
  — that's what makes it fader-independent and matches user intuition.
- **Shared effect state is the source of complexity.** Reverb/delay share one
  tank/line between the global effect and the per-voice send, so "independent"
  behavior requires feeding at full + returning at unity (reverb/delay) rather
  than per-source output levels (which one shared buffer can't provide).
- **Effect tails watch output, not state.** Any effect with a gap between input
  and output (delay) needs a quiet window longer than that gap, and must not
  clear its buffer while signal is in flight.
- **CPU was never the issue.** The per-voice tap is a few MACs/voice/sample; the
  effects already run. All the hard problems were state-machine / tail logic.

## Tuning knobs (audio_engine.cpp / voice_engine_voice_lifecycle.cpp / voice_engine.cpp)
- `kSendMaxGain` (1.995 = +6 dB) — per-voice send ceiling.
- `kSendFeedScale` (0.7) — how hot the sends feed the effects.
- `kVelModSendKneeCurve` (1.5) — send velocity-response curve.
- `kDelayQuietBlocksToStop` (1200) / `kDelayTailMaxBlocks` (3600) — delay tail.
