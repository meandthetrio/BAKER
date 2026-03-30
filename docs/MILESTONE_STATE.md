# MILESTONE_STATE (ADSR_V2)

## UI 0.x

### 0.0 — Fixed update rates + ownership
- Status: DONE
- Proof (code)
  - main.cpp: control tick loop + `ctrl_hz` counter, ownership comments (“Control tick owns hardware input scanning”).
  - ui_logic.cpp: UI tick gate (`kUiTickMs`) + event drain.
  - ui_render.cpp: UI draw gate + `ui_hz` measurement.
  - controls.cpp: `Controls_Tick` hardware scan + debounce.
- Proof (runtime)
  - HUD shows `U:%02lu C:%04lu` (ui_screens.cpp `Hud_Render`).
  - Overlay shows `U:` and `C:` counters (ui_overlay.cpp).
  - Expect UI≈60 and CTRL≈1000 while stressing inputs/MIDI.

### 0.1 — Input event plumbing
- Status: DONE
- Proof (code)
  - controls.cpp: `Controls_Tick` → `UiInput_Push` (producer).
  - ui_input.h/.cpp: `UiInputEvent`, `UiInputType`, `UiInput_Push/Pop` (queue).
  - ui_logic.cpp: UI tick drains `UiInput_Pop` and dispatches via `UiRouter_DispatchEvent`.
  - ui_screens.cpp: screen handlers consume events (`*_OnEvent`).
- Proof (runtime)
  - HUD shows UI input queue health (`UIQO` and `H`) in `Hud_Render` (ui_screens.cpp).
  - Overlay shows queue stats (`QO` / `H`) in `UiOverlay_Render` (ui_overlay.cpp).

### 0.2 — Screen router / navigation model
- Status: DONE
- Proof (code)
  - ui_screens.h: `UiScreenId`, `UiNav` (stack + top), `UiNav_Push/Pop`.
  - app_state.h: `ui_nav`, `ui_active_screen`.
  - ui_screens.cpp: `UiRouter_DispatchEvent`, `UiRouter_Render`, `GetScreen`.
  - ui_logic.cpp: UI tick routes events via `UiRouter_DispatchEvent` and updates active screen.
  - ui_render.cpp: `UIRender::Render` calls `UiRouter_Render` then overlay.
- Proof (runtime)
  - From HUD, enter SD BROWSE or SAMPLE EDIT via menu, then back (POD2) to HUD; overlay (SHIFT) appears and returns to same screen.

### 0.3 — Selection list widget
- Status: DONE
- Proof (code)
  - ui_list_menu.h/.cpp: `UiListMenu`, `UiMenuItem`, `UiListMenu_OnEnc`, `UiListMenu_Render`.
  - Long-list behavior: cursor wraps and scroll window follows (`UiListMenu_OnEnc` updates `cursor` + `scroll`).
  - Usage: HUD menu (ui_screens.cpp `Hud_OnEvent`/`Hud_Render`) and SD browser list (`SdBrowse_OnEvent`/`SdBrowse_Render`).
- Proof (runtime)
  - SD BROWSE with many WAVs: encoder scrolls, highlight stays aligned, window scrolls to keep selection visible; wrap-around occurs at ends.

### 0.4 — Value editor widget
- Status: DONE
- Proof (code)
  - ui_value_edit.h/.cpp: `UiValueEdit_Begin`, `UiValueEdit_OnEnc`, `UiValueEdit_Commit`, `UiValueEdit_Cancel`, `UiValueEdit_Render`.
  - ui_screens.cpp: FX/MOD screens enter edit on EXT click, adjust with EncDelta, commit on click, cancel on POD2.
  - params.h/.cpp: `Params::EditTargets`, `PublishTargets`, `AudioBlockTick` (smoothing in audio thread).
- Proof (runtime)
  - In FX or MOD screen: EXT click enters edit (EDIT/VAL overlay + footer hints), turn encoder changes value, EXT click commits, POD2 cancels.
  - Audible parameter changes (e.g., LPF or delay mix) are smooth with no zipper noise.

### 0.5 — Page layout conventions
- Status: DONE
- Proof (code)
  - ui_layout.h/.cpp: `UiLayout_Default`, `UiDraw_Header`, `UiDraw_Footer`, `BuildStatus`.
  - ui_screens.cpp: `Hud_Render`, `SdBrowse_Render`, `Fx_Render`, `Mod_Render`, `SampleEdit_Render` call layout helpers.
  - ui_overlay.cpp: overlay renders using `UiLayout_Default` and `UiDraw_Footer`.
- Proof (runtime)
  - Header titles stay aligned across screens; footer hints appear in the same bottom line.
  - Entering edit mode updates footer hints (e.g., “EXT:CHG EXT:OK P2:CANC”) without shifting header/body.

### 0.6 — Overlays / diagnostics
- Status: DONE
- Proof (code)
  - ui_overlay.h/.cpp: `UiOverlay_Update`, `UiOverlay_Render`, `UiOverlayState.visible`.
  - ui_logic.cpp: SHIFT held → `UiOverlay_Update(app.overlay, ...)` (toggle path).
  - ui_render.cpp: `UIRender::Render` draws screen then overlay; checks `app.overlay.visible`.
  - Counters displayed:
    - CPU from `audio_cycles_peak`/`audio_budget_cycles` (audio callback → app_state).
    - LATE (`audio_late_count`) and CLP (`clip_count`) from audio thread.
    - UI/CTRL Hz (`ui_hz` in `ui_render.cpp`, `ctrl_hz` in `main.cpp`).
    - UIQ overflow/high-water (`ui_in_ovf`/`ui_in_hi` from ui_logic/queue).
    - Render ms/hi and skips (`render_ms`, `render_hi_ms`, `render_skips` from ui_render).
    - SD/load/save status from `sd` state (ui_worker).
- Proof (runtime)
  - Hold SHIFT on any screen: overlay appears with CPU/LATE/CLP/UIQ/Hz/SD/SAVE lines and returns to the same screen on release.

### 0.7 — Render budget rules
- Status: DONE
- Proof (code)
  - ui_render.cpp: `kRenderBudgetMs`, `kCooldownMs`, skip/early-out logic, `render_skips` update.
  - oled_pager.cpp/h: `BeginFrameTransfer` + `TickTransferOnePage` (paged transfer).
  - app_state.h: `render_ms`, `render_hi_ms`, `render_skips`, `ui_hz` counters.
  - ui_overlay.cpp: displays render stats (`R:%02u/%02u`, skips via `S` line).
- Proof (runtime)
  - Under heavy input/MIDI/SD work: UI remains responsive; `render_skips` may rise but no stalls; `ctrl_hz` remains ~1000.

### 0.8 — UI-safe background work pattern
- Status: DONE
- Proof (code)
  - ui_requests.h/.cpp: `UiReq`, `UiReqType`, `UiReq_Push/Pop` (queue).
  - ui_worker.cpp: `UiWorker_Tick` and state machine (scan/load/save/normalize/project).
  - app_state.h: `ui_req_*`, `sd_*`, `sd_published_*`, `sd_edit_*` publish/result fields.
  - ui_screens.cpp: SD BROWSE/SAMPLE EDIT/HUD trigger requests (scan/load/save/normalize).
- Proof (runtime)
  - Start SD scan/load/save: UI remains responsive; overlay shows SD/SAVE progress; counters stay live.

## Sampler 1.x

### 1.0 — Event Queue (SPSC ring buffer)
- Status: DONE
- Proof (code)
  - event_queue.h: `Event`, `EventType`, `EventQueueSPSC` (`Push`, `Pop`, `Overflows`, `kCapacity`).
  - main.cpp: MIDI decode → `g_evtq.Push(...)` and `events_pushed` / `queue_overflows` increments.
  - main.cpp: `AudioCallback` calls `g_voice.ProcessEvents(g_evtq)` once per audio block.
  - voice_engine.cpp: `VoiceEngine::ProcessEvents` drains via `q.Pop(e)` and increments `events_popped`.
  - app_state.h: `events_pushed`, `events_popped`, `queue_overflows` counters.
- Proof (runtime)
  - Event queue is wired MAIN → AUDIO (main.cpp pushes; AudioCallback drains via voice_engine).
  - Counters exist in AppState but may not be displayed on HUD/overlay.
  - Dense MIDI remains stable; LATE stays low; system recovers from extreme spam.

### 1.1 — Parameter Lane (safe shared params + smoothing)
- Status: DONE
- Proof (code)
  - params.h/.cpp: structures + `EditTargets`/`PublishTargets`/`AudioBlockTick`.
  - main.cpp: AudioCallback calls `g_params.AudioBlockTick(...)`.
  - ui_value_edit.* and ui_screens.cpp: UI edits targets then calls `PublishTargets()`.
- Proof (runtime)
  - Sweeping an audible param (LPF, delay mix) produces smooth audio with no zipper/clicks.
  - Under dense MIDI + UI stress, audio remains stable and LATE stays low.

### 1.2 — Fixed voice pool (no malloc)
- Status: DONE
- Proof (code)
  - voice_engine.h: `Voice` struct and `VoiceEngine::kMaxVoices = 10`.
  - voice_engine.cpp: static `g_voice_pool[VoiceEngine::kMaxVoices]` and `VoiceEngine::Init` resets/prepares pool.
  - main.cpp: AudioCallback only calls `g_voice.ProcessEvents`/`RenderBlock` (no allocation path).
- Proof (runtime)
  - Dense MIDI (>10 notes) stays stable with no crashes or stalls.
  - LATE stays low; system remains responsive under note spam + UI activity.

### 1.3 — NoteOn/NoteOff end-to-end (test tone)
- Status: DONE
- Proof (code)
  - main.cpp: MIDI decode → `g_evtq.Push(...)`; `AudioCallback` drains via `g_voice.ProcessEvents(g_evtq)`.
  - event_queue.h: `Event`, `EventType` for NoteOn/NoteOff payloads.
  - voice_engine.cpp: `ProcessEvents`, `AllocateVoice_`, `NoteOff_`, `AllNotesOff_`.
  - main.cpp + ui_worker.*: sample load publish/apply path (`g_voice.SetSample` in AudioCallback).
- Proof (runtime)
  - Load a sample (SD BROWSE → load), then send MIDI NoteOn/NoteOff (arp).
  - NoteOn produces audible playback; NoteOff releases/stop-fades cleanly with no stuck voices.
  - LATE stays ~0; system remains stable under dense MIDI.

### 1.4 — Voice pool + stealing (Oldest Note)
- Status: DONE
- Proof (code)
  - voice_engine.h: `Voice` struct with `start_id` and `VoiceEngine::kMaxVoices = 10`.
  - voice_engine.cpp: static `g_voice_pool[VoiceEngine::kMaxVoices]` preallocates voices.
  - voice_engine.cpp: `AllocateVoice_` prefers idle voice; otherwise steals Oldest Note (smallest `start_id`).
  - voice_engine.cpp: `note_start_counter_` increments in `ProcessEvents`; `FinishStopFade_` returns voices to `Idle`.
- Proof (runtime)
  - Send > max voices simultaneously (12–16 note chord or sustain spam).
  - Audio continues with deterministic stealing (older notes cut/ducked); no crashes or stuck voices.
  - LATE stays ~0 (or near 0) under steal pressure.

### 1.5 — Block-boundary handoff + shared-state safety
- Status: DONE
- Proof (code)
  - main.cpp: `AudioCallback` drains events each block (`g_voice.ProcessEvents`) and applies SD publish/edit handoffs (`sd_published_*`, `sd_edit_*`).
  - voice_engine.cpp: `ProcessEvents` drains the SPSC queue once per block.
  - params.cpp: double-buffer publish (`PublishTargets` with IRQ guard, `published_idx_`) and per-block smoothing in `AudioBlockTick`.
  - app_state.h + ui_worker.cpp: ready/gen publish for SD sample + edit (`sd_published_*`, `sd_edit_*`).
- Proof (runtime)
  - Stress MIDI + UI edits simultaneously (dense NoteOn/Off while sweeping params and navigating UI).
  - No audio glitches; LATE stays ~0; param changes remain smooth; UI stays responsive.

## Sampler 2.x

### 2.0 — Sample Container Stub (one embedded sample, no SD yet)
- Status: PARTIAL
- Proof (code)
  - sampler_sample.h: `Sample` struct (metadata + PCM pointer).
  - embedded_sample.h / embedded_long_sample.h: embedded PCM arrays + `GetEmbeddedSample`/`GetEmbeddedLongSample`.
  - main.cpp: selects embedded samples (`g_voice.SetSampleBank`, `g_voice.SetSample`).
  - voice_engine.cpp: `ProcessEvents` selects `current_sample_`/`sample_bank_`; `RenderBlock` reads `sample->pcm`.
- Proof (runtime)
  - TBD: NoteOn plays something that is clearly a recorded sample (current embedded samples are procedural stubs).

### 2.1 — Basic Sample Reader (linear interpolation)
- Status: DONE
- Proof (code)
  - voice_engine.cpp: `SampleAtLinear` / `SampleAtLinearRegion` compute `a + frac * (b - a)`.
  - voice_engine.cpp: `RenderBlock` reads PCM via the linear reader during playback.
  - voice_engine.h: `Voice.pos` / `Voice.ratio` (fractional position + rate).
- Proof (runtime)
  - Play the embedded sample at low and high pitches (wide MIDI interval).
  - Pitch shifts sound smooth (not obviously steppy/broken).

### 2.2 — Click-Free Start/Stop (short fades)
- Status: DONE
- Proof (code)
  - voice_engine.cpp: `ComputeFadeStep` uses `kFadeInMs` (~3 ms) to set per-voice `fade_in_step`.
  - voice_engine.cpp: `StartStopFade_` applies stop fade; `FinishStopFade_` returns voice to idle.
  - voice_engine.cpp: `VoiceEngine::Init` computes `stop_fade_samples_` from `kStopFadeMs` (~3 ms), clamped by `kStopFadeMinMs`/`kStopFadeMaxMs` (1–5 ms).
  - voice_engine.h: `fade_in_step`, `stop_fade_samples_remaining` state on `Voice`.
- Proof (runtime)
  - Rapid repeated NoteOn/NoteOff taps on a bright/transient sample produce no clicks.
  - Very short notes and fast retriggers remain smooth without start/stop pops.

### 2.3 — Pitching (MIDI note → playback rate)
- Status: DONE
- Proof (code)
  - voice_engine.cpp: `ComputeRatio` maps MIDI note + `Sample::root_key` to playback ratio.
  - voice_engine.cpp: `StartVoice_` applies `ratio` to per-voice playback; `ProcessEvents` sets it on NoteOn.
  - voice_engine.h: `Voice.ratio` (per-voice rate).
  - sampler_sample.h: `Sample::root_key` (pitch reference).
- Proof (runtime)
  - Play a root note and its octave (C then C+12): octave sounds 2x frequency.
  - Play a fifth (C then G): interval sounds ~3/2.
  - Notes retrigger cleanly with no pitch glitches.

### 2.4 — Per-Voice Amp Envelope (ADSR)
- Status: DONE
- Proof (code)
  - voice_engine.h: per-voice ADSR state (`env_stage`, `env_level`, `env_a_step`, `env_d_step`, `env_r_step`, `env_sustain`).
  - voice_engine.cpp: `InitEnvelope` (Attack/Decay/Sustain setup) and `SetEnvelopeRelease` on NoteOff.
  - voice_engine.cpp: `StepEnvelope` ticks per-sample in `RenderBlock` and multiplies the sample (`s *= env_level`).
  - voice_engine.cpp: ADSR time constants are fixed (`kEnvAttackSec`, `kEnvDecaySec`, `kEnvSustainLevel`, `kEnvReleaseSec`).
- Proof (runtime)
  - Tap a note vs long hold: audible attack/decay and a release tail on NoteOff.
  - No stuck notes; voices return to idle after release.

### 2.5 — Looping (forward loop + loop points)
- Status: DONE
- Proof (code)
  - sampler_sample.h: `loop_start`, `loop_end`, `loop_enabled` fields in `Sample`.
  - sample_edit.h: `SampleEdit` loop fields + `SampleEdit_Clamp` bounds checks.
  - voice_engine.cpp: `AdvancePos` wraps forward when loop enabled (`pos >= loop_end → pos = loop_start`).
  - voice_engine.cpp: `RenderBlock`/`SampleAtLinearRegion` use loop points during playback.
- Proof (runtime)
  - Sustained note continues beyond the original sample end when loop is enabled.
  - If sample edit UI exists, moving loop points changes the loop region audibly.

### 2.6 — Per-Voice Filter
- Status: DONE
- Proof (code)
  - voice_engine.h: per-voice filter state `lpf_z`, global cutoff `lpf_cutoff_hz_`.
  - voice_engine.cpp: cutoff → `lpf_g` computation + clamp (20–20k Hz; `lpf_g` 0.001–0.999).
  - voice_engine.cpp: per-sample update `lpf_z += lpf_g * (s - lpf_z)` in `RenderBlock`.
  - params.cpp: `AudioBlockTick` smooths `current.lpf_cutoff_hz`; `main.cpp` passes to `g_voice.SetLpfCutoff`.
- Proof (runtime)
  - Sweeping cutoff audibly changes timbre smoothly.
  - Under dense polyphony, filter remains stable (no runaway/NaNs).

### 2.7 — Mixer + Gain Staging (headroom rules)
- Status: DONE
- Proof (code)
  - voice_engine.cpp: voice summing in `RenderBlock` with `mix_scale = 0.7f / kMaxVoices`.
  - voice_engine.cpp: clip detection after scaling; increments `clip_count_` when mix exceeds ±1.0.
  - audio_engine.cpp: master level applied per sample (`p.master_level`); SAT soft clip only in FX path.
  - app_state.h + ui_overlay.cpp: `clip_count` stored and shown as `CLP`.
- Proof (runtime)
  - With 8–10 voices (max 10), mix stays clean at nominal gain; CLP remains ~0.
  - CLP increments only when intentionally driven (e.g., high level/sat).

### 2.8 — PROCESS Reverb (Phase A Baker late tank)
- Status: DONE
- Proof (code)
  - audio_engine.h/.cpp: existing PROCESS insert path is reused; only the internal reverb backend changed from the older comb/allpass design to a fixed Phase A Baker tank.
  - audio_engine.cpp: reverb now uses mono-in / stereo-out processing with:
    - one pre-delay buffer driven by existing `reverb_pre`
    - 2 lightweight input diffusers
    - 4 fixed late-tank delay lines near 23/31/43/59 ms at 48 kHz
    - a fixed matrix mix for feedback/output
    - per-line one-pole damping in feedback driven by existing `reverb_damp`
    - decay gain driven by existing `reverb_decay`
  - params.h/.cpp: existing `reverb_on`, `reverb_mix`, `reverb_pre`, `reverb_damp`, `reverb_decay`, and `reverb_reverse` publish/smoothing path is reused unchanged.
  - ui_screens.cpp: existing PROCESS / REVERB detail screen and quick wet-mix control remain the active UI.
- Proof (runtime)
  - REVERB detail screen still edits `Pre`, `Dmp`, `Dcy`, `DIR`, and `Wet` through the existing params lane.
  - `Pre` audibly offsets the tank onset, `Dmp` darkens the later tail, `Dcy` lengthens the tail without runaway, and `Wet` behaves as the normal blend control.
  - Reverb ON/OFF still uses the existing tailing behavior in `audio_engine.cpp`, so turning it off lets the current tail decay out safely.
- Deferred
  - `reverb_reverse` remains published and UI-visible, but Phase A does not assign it a new reverse-reverb DSP behavior.
  - No modulation, size parameter, dynamic tank scaling, or more expensive early-reflection/reverse plumbing is included in Phase A.

### 2.8.1 — PROCESS Reverb (Phase A.1 voicing refinement)
- Status: DONE
- Proof (code)
  - audio_engine.h/.cpp: the same existing 4-line Baker tank, existing insert order, and existing tailing behavior are reused with no new UI or param plumbing.
  - audio_engine.cpp: input diffusion/voicing is refined with slightly stronger pre-tank smoothing and softer output tap voicing so bright plucks enter the tank less directly.
  - audio_engine.cpp: `reverb_damp` now uses a shaped block-rate mapping to a feedback-loop lowpass cutoff range, keeping the same one-pole damping structure while making the sweep more evenly useful.
- Proof (runtime)
  - Bright plucky material should produce a smoother tail than the initial Phase A tuning, with no obvious new ringing.
  - `Dmp` still means tail HF damping, but the sweep now moves from brighter/livelier to softer/darker in a more musical way across the knob range.
  - `Pre`, `Dcy`, `Wet`, existing REVERB UI behavior, and the current safe placeholder `DIR` scope all remain intact.
- Deferred
  - Phase A.1 does not add modulation, more lines, a size control, new menus, or reverse-reverb DSP.

### 2.8.2 — PROCESS Reverb (Phase A.2 8-line tank)
- Status: DONE
- Proof (code)
  - audio_engine.h/.cpp: the existing PROCESS UI, param lane, smoothing path, insert order, and tailing behavior are reused unchanged.
  - audio_engine.cpp: the internal late tank is moved from 4 fixed lines to 8 fixed lines near 17/21/26/31/37/45/54/66 ms at 48 kHz, with sample-level nudges to avoid neat relationships.
  - audio_engine.cpp: feedback scattering now uses a cheap fixed 8-way Hadamard-style signed mix, while the current pre-delay, light pre-tank diffusion, per-line one-pole damping, and block-rate `reverb_damp` mapping remain in place.
- Proof (runtime)
  - Bright plucky material should expose fewer obvious internal repeats and read less like a delay than the prior 4-line build.
  - The late field should be denser/smoother while `Pre`, `Dcy`, `Dmp`, and `Wet` keep the same user-facing meanings and behavior.
  - 5-voice playback should remain stable enough for this phase with no new crackle, zipper noise, or runaway tails.
- Deferred
  - Phase A.2 still does not add modulation, reverse-reverb DSP, new controls, a size parameter, or alternate routing/plumbing.

### 3.0 — Keygroups / Zones
- Status: PARTIAL
- Proof (code)
  - keygroups.h/.cpp: `Keygroup` table and `Keygroups_SelectSampleIndex` note→sample mapping.
  - main.cpp: MIDI NoteOn uses `Keygroups_SelectSampleIndex` and packs `sample_index` into the event.
  - voice_engine.cpp: `ProcessEvents` selects from `sample_bank_` using the event’s `sample_index`.
- Proof (runtime)
  - With two embedded samples, notes below 60 trigger sample 0 and notes 60+ trigger sample 1.
  - TBD: UI editing/persistence of keygroup mappings.

### 3.1 — Velocity Layers
- Status: PARTIAL
- Proof (code)
  - velocity_layers.h/.cpp: `Velocity_SelectLayer` maps `vel < 64` → layer 0, else layer 1.
  - main.cpp: NoteOn packs velocity + layer into the event payload.
  - voice_engine.cpp: `vel_layer` drives `vel_brightness` (soft vs hard timbre); no per‑layer sample selection.
- Proof (runtime)
  - Soft vs hard velocity can change brightness (if audible with current sample).
  - TBD: different samples per velocity layer (not implemented).

### 3.2 — Modulation Sources (LFO + extra envelopes)
- Status: DONE
- Proof (code)
  - mod_sources.h/.cpp: `GlobalLFO` (sine/pulse) and `ModEnv` (attack/decay) with `TickBlock`.
  - mod_matrix.h/.cpp: routes use `ModSource::LFO` / `ModSource::ModEnv` to `FilterCutoff`/`Pitch`.
  - voice_engine.cpp: LFO/ModEnv tick per block; values routed to cutoff/pitch via mod matrix.
  - params.cpp: `lfo_rate_hz`, `lfo_depth`, `env_attack_ms`, `env_decay_ms`, `env_amount` smoothing.
- Proof (runtime)
  - Enable an LFO→filter/pitch route and vary rate/depth: audible periodic modulation.
  - ModEnv affects filter/pitch on NoteOn (decays after attack).

### 3.3 — Modulation Routing / Matrix
- Status: DONE
- Proof (code)
  - mod_matrix.h/.cpp: `ModRoute` (src/dst/amount/enabled), `kMaxModRoutes = 4`, `ModMatrix_Publish` double‑buffers routes.
  - app_state.h: `mod_matrix` + `mod_routes_ui` storage.
  - ui_screens.cpp: MOD screen edits enable/amount/dst and publishes via `ModMatrix_Publish`.
  - voice_engine.cpp: reads active route buffer and applies modulation to filter cutoff/pitch.
- Proof (runtime)
  - From MOD screen, enable a route and set AMT: audible LFO→filter or LFO→pitch modulation.
  - Route changes take effect immediately without glitches.

### 3.4 — Parameter Locks
- Status: PARTIAL
- Proof (code)
  - plocks.h/.cpp: `Pattern` (16 steps), `StepLock` (enabled + `cutoff_norm`), `PLocks_PublishCurrentStep`.
  - ui_logic.cpp: sequencer tick advances `step_index` by `seq_bpm` and publishes locks when `plock_apply_enabled`.
  - voice_engine.cpp: applies `PLocksState` at block boundary (`lock_gen`), overrides cutoff normalization when enabled.
- Proof (runtime)
  - With `seq_running` and `plock_apply_enabled`, sustained notes alternate brightness as steps advance (default pattern).
  - TBD: UI authoring/persistence of per‑step locks; only cutoff lock exists today.

### 3.5 — Performance Macros
- Status: DONE
- Proof (code)
  - macros.h/.cpp: `MacroState` (A/B values), `MacroDef` mappings, `Macros_Publish` + `Macros_Smooth` + `Macros_Apply`.
  - ui_screens.cpp: Macro screen edits `macro_ui.value[]` and publishes.
  - voice_engine.cpp: applies macros to cutoff/LFO depth/env amount/route0 amount each block.
  - main.cpp: applies macros to SAT drive in the audio callback.
- Proof (runtime)
  - Sweep Macro A: filter cutoff + LFO depth (and drive if SAT enabled) change together with smooth response.
  - No LATE spikes or glitches during macro sweeps.

## Sampler 4.x

### 4.0 — SD / File Browser (load WAVs)
- Status: DONE
- Proof (code)
  - ui_screens.cpp: SD BROWSE screen enqueues `ScanSdWavs`/`LoadWavIndex` via `UiReq_Push`.
  - ui_requests.h/.cpp: request queue (`UiReqType`, `UiReq_Push/Pop`).
  - ui_worker.cpp: `StartScan`/`ScanStep` build WAV list; `StartLoad`/`LoadStep` parse WAV and fill `sd_slots`.
  - app_state.h: `sd_slots`, `sd_published_*`, `sd_edit_*`, `sd_current_slot` publish flags/slots.
  - main.cpp: AudioCallback applies `sd_published_*` and `sd_edit_*` at block boundaries via `g_voice.SetSample/SetSampleEdit`.
- Proof (runtime)
  - SD BROWSE shows scan progress, wav count, load progress/status (SD OK/ER, W, L, MSG).
  - Selecting a file loads it and updates current slot; audio plays the loaded sample without stalls.

### 4.1 — Background loading (main loop loads, audio keeps playing)
- Status: DONE
- Proof (code)
  - ui_worker.cpp: `UiWorker_Tick` advances load in time‑budgeted steps (`LoadStep`), updates `sd.load_progress`.
  - ui_requests.h/.cpp: `LoadWavIndex` request queue; `ui_req_busy`/`ui_req_progress` track worker state.
  - app_state.h + main.cpp: `sd_published_ready`/`sd_published_gen`/`sd_published_slot` gate the audio swap at block boundary.
- Proof (runtime)
  - While loading a large WAV, UI stays responsive and LATE stays low.
  - Load progress updates; audio continues until the ready handoff swaps to the new sample.

### 4.2 — Sample edit (trim/loop/normalize/loop-find)
- Status: DONE
- Proof (code)
  - ui_screens.cpp: Sample Edit screen edits trim/loop fields and publishes `sd_edit_*`.
  - sample_edit.h: `SampleEdit` struct + `SampleEdit_Clamp`.
  - ui_requests.h: `NormalizeCurrent` / `LoopFindCurrent` / `SaveRenderedWavCurrent` request types.
  - ui_worker.cpp: `NormalizeStep` and `LoopFindCurrent` run in worker; update `sd_edit_*` and status.
  - app_state.h + main.cpp: `sd_edit_ready`/`sd_edit_gen`/`sd_edit_slot` handoff applied at block boundary.
- Proof (runtime)
  - Trim/loop changes affect playback region; loop enable/start/end audible when holding notes.
  - Normalize/Loop Find run in background with progress/status; UI/audio remain responsive.

### 4.3 — Presets / Programs
- Status: PARTIAL
- Proof (code)
  - ui_requests.h: `SaveProject` / `LoadProject` requests (plus `SavePreset` stub).
  - ui_worker.cpp: `SaveProject` writes `PROJECT1.AKPRJ`; `LoadProject` reads manifest and applies state.
  - project_manifest.h: serialized fields (wav path, edit, seq/plock flags, BPM, LFO wave, macros, mod routes).
  - app_state.h + ui_screens.cpp: `project_status` displayed in HUD.
- Proof (runtime)
  - Save Project writes status (PRJ SAVING/PRJ SAVED); Load Project restores params/mappings and triggers WAV load.
  - TBD: Preset save/load beyond project file (SavePreset is stub; no file I/O).

### 4.4 — Project Save/Load
- Status: PARTIAL
- Proof (code)
  - ui_requests.h: `SaveProject` / `LoadProject` request types reachable from HUD menu.
  - ui_worker.cpp: `SaveProject`/`LoadProject` serialize `ProjectManifest` to `PROJECT1.AKPRJ`.
  - project_manifest.h: fields persisted (wav path, edit, seq/plock flags, BPM, LFO wave, macro state, mod routes).
  - app_state.h + ui_screens.cpp: `project_status` displayed; `project_edit_pending` used to apply edit after load.
- Proof (runtime)
  - Save Project → change state → Load Project restores WAV path + edit + macros/mod routes.
  - TBD: restore of all performance params (FX/LPF/etc.) not currently in manifest.
