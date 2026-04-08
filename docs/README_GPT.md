# README_GPT (ADSR_V2)

## Purpose
This repo is a hardware sampler/performer. The main loop owns UI, controls, SD/filesystem, and background work; the audio callback is deterministic DSP only.

## Threading contract (short)
- AUDIO: no malloc, no file I/O, no logging, no UI calls.
- MAIN: schedules control tick + UI tick; communicates to audio via queues/shared state and block-boundary handoffs.

## 0.0 Fixed update rates + ownership
- Control tick (1 kHz target): runs from the main loop and owns all hardware scanning + debounce. It generates UI input events only.
- Control tick loop is bounded per main-loop iteration (`kMaxCtrlTicksPerLoop` in `main.cpp`).
- UI tick (~60 Hz target): runs from the main loop and owns UI state updates + drawing. It must not poll hardware.
- Ownership rule: controls read hardware; UI consumes events and draws. No random hardware polling in UI.

## 0.1 Input event plumbing
- Rule: controls produce events; UI consumes them. UI never polls hardware directly.
- Producer path: `controls.cpp` → `Controls_Tick` → `UiInput_Push` (queue in `ui_input.*`).
- Consumer path: `ui_logic.cpp` → UI tick loop → `UiInput_Pop` → `UiRouter_DispatchEvent`.
- Event types: `UiInputEvent`/`UiInputType` in `ui_input.h` (EncDelta, BtnDown, BtnUp, BtnLong).
- Where to look: `controls.cpp`, `ui_input.h/.cpp`, `ui_logic.cpp`, `ui_screens.cpp` handlers.

## 0.2 Screen router / navigation model
- Nav model: page stack (`UiNav` with `stack[]` + `top`) and active screen (`UiScreenId`).
- Nav state lives in `app_state.h` (`ui_nav`, `ui_active_screen`) and types in `ui_screens.h`.
- Event path: `ui_logic.cpp` UI tick → `UiRouter_DispatchEvent` → active screen `*_OnEvent`.
- Render path: `ui_render.cpp` → `UiRouter_Render` → active screen `*_Render`.
- Temporary views reuse the same stack: project save/load pushes a `ProjectStatus` screen and REnc click pops back to the prior screen.
- Overlay: `ui_overlay.cpp` draws on top after screen render; does not poll hardware.
- Where to look: `ui_screens.h/.cpp` (`UiNav_*`, `UiRouter_*`, `GetScreen`), `ui_logic.cpp`, `ui_render.cpp`.

## 0.3 Selection list widget
- Widget: `UiListMenu` in `ui_list_menu.*` with `UiMenuItem` entries.
- State: `cursor` (selected index), `scroll` (window offset), `rows` (visible rows), `count`.
- Input: `UiListMenu_OnEnc` consumes encoder deltas (EncDelta) to move cursor; selection handled by screen (e.g., SD BROWSE uses POD1, HUD uses EXT click).
- Render: `UiListMenu_Render` draws visible window and highlights current item with a prefix.
- Long-list behavior: cursor wraps (mod count); scroll window advances to keep cursor visible when list exceeds rows.
- Usage: SD browser list (`SdBrowse_OnEvent`/`SdBrowse_Render`) and HUD menu.

## 0.4 Value editor widget
- Widget: `UiValueEdit` in `ui_value_edit.*` (Begin/OnEnc/Commit/Cancel/Render).
- Enter/exit: screens start edit on confirm (EXT click) and commit on confirm; cancel via POD2 (`UiValueEdit_Cancel`).
- Input: EncDelta adjusts `value_i` with step/clamp; edit mode gated by `value_edit.active`.
- Safe plumbing: UI writes targets via `Params::EditTargets()` then `PublishTargets()`; AUDIO smooths in `Params::AudioBlockTick()` per block.
- Visual indicator: overlay text `EDIT:`/`VAL:` from `UiValueEdit_Render` + footer hints when active.
- Usage: FX screen fields (delay mix/LPF) and MOD screen fields use value editor.
- Where to look: `ui_value_edit.*`, `ui_screens.cpp` (Fx_OnEvent/Mod_OnEvent + Render), `params.*`.

## 0.5 Page layout conventions
- Standard layout: header (title/status), body (content rows), footer (hints) shared across screens.
- Helpers: `ui_layout.*` (`UiLayout_Default`, `UiDraw_Header`, `UiDraw_Footer`, `BuildStatus`) define regions and status string.
- Screens use helpers to draw consistent header/footer and render content in body region (HUD/SD/FX/MOD/SAMPLE EDIT).
- Footer hints change with mode (e.g., value edit shows `EXT:CHG EXT:OK P2:CANC`).
- Overlay draws after screen render and keeps header/footer stable; uses layout helper.
- Where to look: `ui_layout.*`, `ui_screens.cpp` (`Hud_Render`, `SdBrowse_Render`, `Fx_Render`, `Mod_Render`, `SampleEdit_Render`), `ui_overlay.cpp`.

## 0.6 Overlays / diagnostics
- Current toggle behavior: LShift is used for parent-preview navigation in PERFORM; overlay hotkey toggle is currently not bound in `ui_logic.cpp` (`UiOverlay_Update(..., false, ...)`).
- Code: `ui_overlay.*` (`UiOverlay_Update`, `UiOverlay_Render`), overlay state in `app_state.h`.
- Displays (OLED): `U`/`C` Hz, CPU, LATE, CLP, voices, UIQ overflow/high-water, render ms/hi, render skips, SD ok/wavs/load, SAVE state/percent.
- Counters source: `app_state.h` (shared counters), `main.cpp` (ctrl_hz), `ui_render.cpp` (ui_hz, render stats), audio callback (cycles/late/clip).
- Ordering: active screen renders first, then overlay draws on top in `UIRender::Render`.
- Where to look: `ui_overlay.cpp`, `ui_logic.cpp` (parent-preview + overlay update callsite), `ui_render.cpp`, `app_state.h`.

## 0.7 Render budget rules
- Budget constant: `kRenderBudgetMs` (and cooldown `kCooldownMs`) in `ui_render.cpp`.
- Render tick is gated to ~60 Hz and respects `app.ui_dirty` / overlay visibility.
- Enforcement: if render time exceeds budget, cooldown kicks in and `render_skips` increments.
- Partial redraw: `OledPager` pages/bands via `TickTransferOnePage` (paged transfer instead of full blocking update).
- Stats tracked: `render_ms`, `render_hi_ms`, `render_skips`, `ui_hz` (shown in overlay).
- Where to look: `ui_render.*`, `oled_pager.*`, `ui_overlay.cpp`.

## 0.8 UI-safe background work pattern
- Pattern: UI enqueues small requests → worker processes in steps → results published to shared state → UI displays status.
- Request queue: `ui_requests.*` (`UiReq`, `UiReqType`, `UiReq_Push/Pop`).
- Worker: `ui_worker.*` (`UiWorker_Tick` state machine; SD scan/load/save, normalize, loop find, project save/load).
- Publish strategy: shared AppState fields (`ui_req_*`, `sd_*`, `sd_published_*`, `sd_edit_*`, save/status flags) + block-boundary handoffs.
- Examples: SD scan (`ScanSdWavs`), load WAV (`LoadWavIndex`), save rendered WAV, save/load project.
- UI responsiveness: work runs in BG slices; HUD/overlay show SD/SAVE progress and counters stay live.
- Where to look: `ui_requests.*`, `ui_worker.*`, `sd_browser_state.*`, `app_state.h`, `ui_overlay.cpp`, `ui_screens.cpp`.

## 1.0 Event Queue (SPSC ring buffer)
- Connects MAIN → AUDIO for musical events (NoteOn/NoteOff/AllNotesOff/TestPing).
- SPSC ring buffer for determinism: lock-minimal, fixed-size, no malloc in audio.
- Queue type: `EventQueueSPSC` in `event_queue.h` (`kCapacity=256`, `Push`, `Pop`, `Overflows`).
- Event payload: `Event` / `EventType` in `event_queue.h`.
- Producer path: `main.cpp` MIDI decode pushes events via `g_evtq.Push(...)`.
- Consumer path: `AudioCallback` (main.cpp) calls `g_voice.ProcessEvents(g_evtq)` once per audio block; `VoiceEngine::ProcessEvents` pops.
- Counters: `AppState::events_pushed/events_popped/queue_overflows` updated in main + voice engine; may not be shown on HUD/overlay yet.
- Where to look: `event_queue.h`, `main.cpp` (MIDI → Push, AudioCallback), `voice_engine.cpp` (ProcessEvents), `app_state.h`.

## 1.1 Parameter Lane (safe shared params + smoothing)
- Purpose: safe MAIN → AUDIO parameter updates without zipper noise.
- Data model: targets + current + smoothing coefficient; current is what AUDIO uses.
- Threading: MAIN edits targets; MAIN publishes (copy + gen bump); AUDIO pulls published at block boundary.
- Key API: `Params::EditTargets()`, `Params::PublishTargets()`, `Params::TargetsForUI()`, `Params::AudioBlockTick(sample_rate, block_size)`.
- Where to look: `params.h/.cpp`, `main.cpp` (AudioCallback calls `g_params.AudioBlockTick(...)`), `ui_value_edit.*` + `ui_screens.cpp` FX/MOD handlers (edit + publish).
- Notes: `PublishTargets()` uses an IRQ-safe critical section; AUDIO never mallocs/logs.

## 1.2 Fixed voice pool (no malloc)
- Big idea: fixed preallocated voice array/pool (kMaxVoices = 10) for deterministic allocation.
- Audio callback does not allocate; voice state lives in preallocated storage.
- Where to look: `voice_engine.h` (`Voice` struct, `VoiceEngine::kMaxVoices`), `voice_engine.cpp` (static `g_voice_pool`, `VoiceEngine::Init` resets pool).
- Prereq for NoteOn/NoteOff handling and voice stealing milestones.

## 1.3 NoteOn/NoteOff end-to-end (test tone)
- End-to-end path: MIDI decode in `main.cpp` → `g_evtq.Push(...)` → `AudioCallback` drains via `g_voice.ProcessEvents(g_evtq)` → `VoiceEngine::ProcessEvents` → `AllocateVoice_` → voice plays current sample.
- `VoiceEngine::ProcessEvents` early-outs if `current_sample_` is null/empty, so runtime proof assumes a valid loaded sample.
- NoteOff triggers release/stop fade behavior (`VoiceEngine::NoteOff_`, `StartStopFade_`, envelope release).
- Where to look: `main.cpp` (MIDI decode + `AudioCallback` → `g_voice.ProcessEvents`), `event_queue.h` (`Event`, `EventType`), `voice_engine.cpp` (`ProcessEvents`, `AllocateVoice_`, `NoteOff_`, `AllNotesOff_`), sample load publish/apply path (`ui_worker.*`/`sd_browser_state.*` publish, `main.cpp` AudioCallback applies via `g_voice.SetSample`).

## 1.4 Voice pool + stealing (Oldest Note)
- Fixed array/pool of voices; allocate on NoteOn and return to free pool when finished.
- Stealing rule: when no free voice, steal Oldest Note (smallest `start_id`).
- Deterministic, audio-thread-safe: runs inside `ProcessEvents` on the audio thread; no malloc/locks.
- Where to look: `voice_engine.cpp` (static `g_voice_pool`, `AllocateVoice_`, `ProcessEvents`, `StartStopFade_`, `FinishStopFade_`, Oldest Note via `start_id`), `voice_engine.h` (`Voice` struct, `VoiceEngine::kMaxVoices`, `note_start_counter_`), `main.cpp` (`AudioCallback` calls `g_voice.ProcessEvents` per block).

## 1.5 Block-boundary handoff + shared-state safety
- Events: MAIN pushes; AUDIO drains once per block (deterministic).
- Params: MAIN edits targets and publishes; AUDIO consumes once per block and smooths.
- Safe multi-field publish examples: params double-buffer + published index; SD sample/edit publish uses ready/gen flags to avoid torn reads.
- Where to look: `main.cpp` (AudioCallback drains events per block; main loop runs control/UI ticks), `voice_engine.cpp` (`ProcessEvents`), `params.h/.cpp` (`PublishTargets`, `AudioBlockTick`, published buffer), `app_state.h` + `ui_worker.cpp` + `main.cpp` (`sd_published_*` / `sd_edit_*` ready+gen handoff).

## 2.0 Sample Container Stub (one embedded sample, no SD yet)
- Sample container = metadata + PCM pointer (`Sample` struct) for raw audio playback.
- Embedded PCM lives in code (single embedded sample + long embedded sample as stubs).
- Main loop selects the active embedded sample / bank for playback.
- Audio thread reads from the sample buffer during voice render.
- Runtime goal: NoteOn plays a recorded sample (current embedded samples are procedural stubs).
- Where to look: `sampler_sample.h` (`Sample`), `embedded_sample.h` / `embedded_long_sample.h` (PCM + `GetEmbedded*Sample`), `main.cpp` (`g_voice.SetSampleBank`, `g_voice.SetSample`), `voice_engine.cpp` (`ProcessEvents` selects sample, `RenderBlock` reads `sample->pcm`).

## 2.1 Basic Sample Reader (linear interpolation)
- Interpolation uses linear blend between adjacent PCM points when playback rate ≠ 1.0.
- Reader uses floating position (`pos`) + fractional part (`frac`) with per-sample `ratio`.
- Linear mix is `a + frac * (b - a)` in the sample reader.
- Where to look: `voice_engine.cpp` (`SampleAtLinear`, `SampleAtLinearRegion`, `RenderBlock` usage), `voice_engine.h` (`Voice.pos`, `Voice.ratio`), `sampler_sample.h` (`Sample::pcm`).

## 2.2 Click-Free Start/Stop (short fades)
- Anti-click strategy: per-voice fade-in on start and short stop-fade on release/stop.
- Fade-in length is ~3 ms (`kFadeInMs`) via `ComputeFadeStep` (per-sample `fade_in_step`).
- Stop-fade length is ~3 ms (`kStopFadeMs`) clamped to 1–5 ms (`kStopFadeMinMs`/`kStopFadeMaxMs`), computed in `VoiceEngine::Init`.
- Where to look: `voice_engine.cpp` (`ComputeFadeStep`, `StartVoice_` sets `fade_in_step`, `StartStopFade_`, `FinishStopFade_`, `kFadeInMs`/`kStopFadeMs`/min/max), `voice_engine.h` (`fade_in_step`, `stop_fade_samples_remaining`).

## 2.3 Pitching (MIDI note → playback rate)
- Mapping: MIDI note → playback ratio via `2^((note - root_key) / 12)`.
- Root/base note lives in sample metadata (`Sample::root_key`).
- Per-voice playback rate stored in `Voice.ratio`, applied to advance sample position during render.
- Pitched playback uses the linear interpolated reader (2.1) for non-integer positions.
- Tuning offsets (coarse/fine/cents) are not exposed yet (TBD).
- Where to look: `voice_engine.cpp` (`ComputeRatio`, `StartVoice_`, `ProcessEvents`), `voice_engine.h` (`Voice.ratio`), `sampler_sample.h` (`Sample::root_key`).

## 2.4 Per-Voice Amp Envelope (ADSR)
- Per-voice ADSR with Attack/Decay/Sustain/Release state stored on the voice.
- NoteOn initializes ADSR; NoteOff triggers Release (`SetEnvelopeRelease`) plus stop-fade (2.2).
- Envelope ticks per-sample inside `RenderBlock` (`StepEnvelope`) and multiplies the voice sample (`s *= env_level`).
- Parameters are fixed constants for now (`kEnvAttackSec`, `kEnvDecaySec`, `kEnvSustainLevel`, `kEnvReleaseSec`); UI control is TBD.
- Where to look: `voice_engine.cpp` (`InitEnvelope`, `SetEnvelopeRelease`, `StepEnvelope`, `StartVoice_`, `NoteOff_`, `RenderBlock`), `voice_engine.h` (`env_stage`, `env_level`, `env_*` fields).

## 2.5 Looping (forward loop + loop points)
- Loop metadata lives in `Sample` (loop_start/loop_end/loop_enabled) and can be overridden per-sample via `SampleEdit`.
- Loop points are in frames/samples (indices into the PCM buffer).
- Audio wrap happens in `AdvancePos`: when loop enabled and gate on, `pos >= loop_end` wraps to `loop_start` (forward mode).
- No explicit crossfade at loop boundary (uses existing 2.2 fades only at start/stop).
- Looping uses the same pitched/interpolated reader (2.3 + 2.1) because `ratio` advances position and `SampleAtLinear*` reads fractional positions.
- Safety: `SampleEdit_Clamp` enforces bounds and loop_end > loop_start.
- Where to look: `sampler_sample.h` (`loop_start`, `loop_end`, `loop_enabled`), `sample_edit.h` (`SampleEdit`, `SampleEdit_Clamp`), `voice_engine.cpp` (`AdvancePos`, `RenderBlock`, `SampleAtLinearRegion`), `voice_engine.h` (`Voice.pos`, `Voice.ratio`).

## 2.6 Per-Voice Filter
- Filter type: per-voice 1‑pole low‑pass (integrator form) applied per sample.
- Filter state lives per voice (`Voice.lpf_z`) and is updated inside the render loop.
- Cutoff comes from params lane (`Params::current.lpf_cutoff_hz`) with smoothing; optional mod sources can offset cutoff.
- Cutoff is clamped to a safe range (20–20k Hz) and coefficient `lpf_g` is clamped (0.001–0.999) for stability.
- Filter state is effectively reset on voice start (lpf_z set to 0 in `StartVoice_`); each voice has independent state.
- Where to look: `voice_engine.cpp` (cutoff calc, `lpf_g`, per-voice `lpf_z` update in `RenderBlock`, `StartVoice_` reset), `voice_engine.h` (`lpf_z`, `SetLpfCutoff`), `params.cpp` (`AudioBlockTick` smoothing of `lpf_cutoff_hz`), `ui_screens.cpp` (LPF edit UI).

## 2.7 Mixer + Gain Staging (headroom rules)
- Voice summing happens in `VoiceEngine::RenderBlock` (per‑voice samples accumulated into `outL/outR`).
- Headroom rule: final mix is scaled by `mix_scale = 0.7f / kMaxVoices` (10‑voice headroom).
- Per‑voice gain comes from velocity + edit gain (and envelope/fades) before summing.
- Clip detection: after scaling, `clip_count` increments when mix exceeds ±1.0; shown as `CLP` on overlay.
- No dedicated limiter/soft‑clip in the voice mixer; optional soft clip exists only in FX path when SAT is enabled.
- Stereo behavior: voices are dual‑mono (same signal to L/R) before FX; FX can add stereo.
- Where to look: `voice_engine.cpp` (`RenderBlock`, `mix_scale`, clip counting), `voice_engine.h` (`kMaxVoices`), `app_state.h` (`clip_count`), `ui_overlay.cpp` (CLP display), `audio_engine.cpp` (master level + SAT soft clip).

## 3.0 Keygroups / Zones
- Keygroup table maps MIDI note ranges to sample indices.
- Current mapping is hardcoded (no UI edit / persistence yet).
- Note routing: MIDI note → `Keygroups_SelectSampleIndex` → sample index placed in event → `VoiceEngine` selects from sample bank.
- Where to look: `keygroups.h/.cpp` (`Keygroup`, `Keygroups_SelectSampleIndex`), `main.cpp` (MIDI note → sample index in event), `voice_engine.cpp` (sample bank selection), `embedded_sample.h` / `embedded_long_sample.h` (two embedded samples used by the bank).

## 3.1 Velocity Layers
- Velocity is captured in NoteOn and a 2‑layer index is selected (`vel < 64 → layer 0`, else layer 1).
- Current behavior: layer affects per‑voice brightness (`vel_brightness`) and debug state, but does not select different samples yet.
- TODO: map velocity layers to distinct samples/params and persist layer rules.
- Where to look: `velocity_layers.h/.cpp` (`Velocity_SelectLayer`), `main.cpp` (NoteOn packs velocity + layer into event), `event_queue.h` (`Event.velocity`), `voice_engine.cpp` (`vel_layer` → `vel_brightness` in `ProcessEvents`/`StartVoice_`), `voice_engine.h` (`vel_layer`).

## 3.2 Modulation Sources (LFO + extra envelopes)
- Sources implemented: global LFO (sine/pulse, bipolar) and per‑voice ModEnv (attack/decay only, unipolar 0..1).
- Update rate: both LFO and ModEnv tick once per audio block (`TickBlock`), not per control tick.
- LFO rate/depth come from params lane (`lfo_rate_hz`, `lfo_depth`) and are smoothed; no tempo‑sync yet.
- ModEnv is triggered on NoteOn and scaled by `env_amount` (params lane); no MIDI modwheel/aftertouch/pitchbend sources yet.
- Sources feed the mod matrix to destinations (FilterCutoff, Pitch); pitch mod range is limited (±2 semitones via `kPitchModSemitones`).
- Where to look: `mod_sources.h/.cpp` (`GlobalLFO`, `ModEnv`), `mod_matrix.h/.cpp` (routes, sources/dests), `voice_engine.cpp` (LFO/ModEnv tick + routing to cutoff/pitch), `params.h/.cpp` (lfo/env params).

## 3.3 Modulation Routing / Matrix
- Mod matrix is a table of routes (`ModRoute`: source, destination, amount, enabled), max 4 routes.
- Safe publish: UI edits `mod_routes_ui` and calls `ModMatrix_Publish` (double‑buffer with `routes_sel`/`routes_gen`).
- Audio apply: `voice_engine.cpp` pulls the active route buffer once per block and applies amounts to filter cutoff and pitch.
- Supported sources/dests today: LFO and ModEnv → FilterCutoff or Pitch (sources are currently fixed in defaults; UI edits enable/amount/dest only).
- Where to look: `mod_matrix.h/.cpp` (`ModRoute`, `ModMatrixState`, `ModMatrix_Publish`), `ui_screens.cpp` (MOD screen edit/publish), `voice_engine.cpp` (route application in `RenderBlock`), `app_state.h` (`mod_matrix`, `mod_routes_ui`).

## 3.4 Elektron-style Parameter Locks
- Step sequencer exists (16 steps) with a per-step `StepLock` snapshot (currently only `cutoff_norm` + `enabled`).
- Control tick advances steps based on `seq_bpm` and publishes the current step lock (double‑buffered `PLocksState`).
- Audio thread applies the latest lock at block boundaries (`lock_gen` change) and uses it to override cutoff normalization.
- Priority: when `active_lock_.enabled`, cutoff uses lock value; otherwise it uses the param lane cutoff.
- No UI authoring/persistence yet; pattern is initialized with a hardcoded alternating cutoff.
- Where to look: `plocks.h/.cpp` (`StepLock`, `Pattern`, `PLocks_PublishCurrentStep`), `ui_logic.cpp` (sequencer tick + publish), `app_state.h` (`plocks`, `plock_pattern`, `seq_bpm`), `voice_engine.cpp` (apply in `RenderBlock`).

## 3.5 Performance Macros
- Two macros (A/B) with values 0..1; each macro maps to multiple targets via `MacroDef` assignments.
- Publish path: UI edits `macro_ui` → `Macros_Publish` (double‑buffer `macro_a`/`macro_b` + `macro_gen`).
- Audio apply: macros are smoothed per block and applied to cutoff, LFO depth, env amount, route0 amount; SAT drive uses macros in audio callback.
- UI: Macro screen edits the selected macro value (selection stored in `macro_ui.selected`, default A; no selection control yet).
- Routing rules: Macro A targets FilterCutoff/LfoDepth/Drive; Macro B targets FilterCutoff/Route0Amount/EnvAmount.
- Where to look: `macros.h/.cpp` (`MacroState`, `MacroDef`, `Macros_Publish`, `Macros_Smooth`, `Macros_Apply`), `ui_screens.cpp` (Macro screen), `voice_engine.cpp` (apply to cutoff/LFO/env/route0), `main.cpp` (apply to drive).

## 4.0 SD / File Browser (load WAVs)
- Browse WAVs on SD, select an entry, enqueue a load request, and publish the loaded sample to a playable slot safely.
- Threading: SD I/O runs in MAIN/worker; AUDIO only consumes prepared buffers via `sd_published_*` handoff at block boundary.
- Flow: SD BROWSE screen → `UiReqType::ScanSdWavs`/`LoadWavIndex` → `ui_worker` scan/load steps → publish to `sd_slots` + `sd_published_*` and `sd_edit_*`.
- Where to look: `ui_screens.cpp` (SdBrowse_OnEnter/OnEvent/Render), `sd_browser_state.*` (list state, status), `ui_requests.*` (UiReq queue), `ui_worker.cpp` (ParseWavHeader, StartScan/ScanStep, StartLoad/LoadStep), `sd_sample_pool.*` (sample buffers), `app_state.h` (`sd_*`, `sd_published_*`, `sd_edit_*`), `main.cpp` (AudioCallback applies published slots).
- Limitations (current code): WAV only; PCM 16‑bit mono @ 48 kHz; max files `kSdMaxFiles=32`; max frames `kSdSampleMaxFrames=240000` (~5 s); 2 sample slots; scans `/WAV` folder or SD root.

## 4.1 Background loading (audio-safe)
- UI triggers load; worker does SD I/O in time‑budgeted slices; audio keeps playing current voices.
- Threading: AUDIO never touches SD/filesystem or blocks; MAIN/worker performs heavy work in `UiWorker_Tick` with `budget_us`.
- Pattern: `UiReqType::LoadWavIndex` → `UiReq_Push/Pop` → `UiWorker_Tick` → `LoadStep` updates `sd.load_progress` and publishes `sd_published_*` when ready.
- Handoff: `sd_published_ready` + `sd_published_gen` + `sd_published_slot` gate the swap; AudioCallback applies at block boundary.
- UI visibility: SD BROWSE shows `L:%03` progress; overlay shows `SD:OK/ER W.. L..`; `ui_req_busy`/`ui_req_progress` track worker state.
- Where to look: `ui_requests.*` (queue), `ui_worker.cpp` (`UiWorker_Tick`, `LoadStep`), `sd_browser_state.*` (progress/status), `app_state.h` (`ui_req_*`, `sd_published_*`, `sd_current_slot`), `main.cpp` (AudioCallback handoff).

## 4.2 Sample edit (trim/loop/normalize)
- Sample Edit screen adjusts edit parameters (trim start/end, loop enable/start/end, gain) in UI state; audio uses published edits only.
- Instant edits: trim/loop/gain changes update `sd_edit_slots` and publish via `sd_edit_*` flags (no SD I/O).
- Background ops: Normalize / Loop Find / Save WAV are worker requests handled by `ui_worker` with progress/status updates.
- Handoff: UI writes `sd_edit_pending` + `sd_edit_slot` + `sd_edit_gen` + `sd_edit_ready`; AudioCallback applies at block boundary.
- Where to look: `ui_screens.cpp` (SampleEdit screen handlers), `sample_edit.h` (`SampleEdit`, `SampleEdit_Clamp`), `ui_requests.h` (`NormalizeCurrent`, `LoopFindCurrent`, `SaveRenderedWavCurrent`), `ui_worker.cpp` (`NormalizeStep`, `LoopFindCurrent`, `StartSave/SaveStep`), `app_state.h` (`sd_edit_*`, `sd_edit_slots`, `sd_current_slot`).

## 4.3 Presets / Programs
- Preset/Program = saved performance state that can be recalled (params + mappings), not raw audio data.
- Implemented today: Project save/load uses fixed numbered slot files (`PROJECT01.AKPRJ` ... `PROJECT08.AKPRJ`) and includes last WAV path, current SampleEdit, seq/plock flags, BPM, LFO wave, macro state, and mod routes.
- Excludes: raw WAV audio data, UI navigation, transient counters (CPU/LATE/etc.).
- Save/load flow: Button1 Settings owns `PROJECT SLOT`, `SAVE PROJECT`, and `LOAD PROJECT`; triggering save/load pushes a temporary `ProjectStatus` screen immediately; UI triggers `SaveProject`/`LoadProject` request; worker does SD I/O; UI shows slot-tagged `project_status` (`P01 SAVING` / `P01 LOADED` / `P01 EMPTY` / `P01 ERR`); audio never blocks.
- Current single-sample recall rule: `LoadProject` restores the saved WAV into explicit slot 0 / layer A instead of the normal inactive-slot path, so post-reboot project recall lands back on layer A deterministically.
- ProjectStatus screen: shows project slot, action (`SAVE`/`LOAD`), and live status/result until REnc click dismisses it.
- Note: `SavePreset` request exists but is a stub (fake work, no file I/O yet).
- Where to look: `ui_requests.h` (`SaveProject`, `LoadProject`, `SavePreset`), `ui_worker.cpp` (`SaveProject`, `LoadProject`, project_status), `project_manifest.h` (serialized fields), `macros.*` / `mod_matrix.*` / `plocks.*` (included state), `ui_screens.cpp` (Settings + ProjectStatus), `app_state.h` (`project_status`, `macro_ui`, `mod_routes_ui`).

## 4.4 Project Save/Load
- Project = full snapshot of musical state you want to return to later (loaded WAV path + edit + routing/macro state).
- Included today (per `ProjectManifest`): WAV path, `SampleEdit`, seq/plock flags, BPM, LFO wave, macro state/selection, mod routes/selection.
- Excludes: raw WAV PCM data (stored separately on SD), transient counters, UI navigation/scroll state, and most performance params (FX mix/LPF/etc. are TBD).
- Flow: user presses Button1 to open Settings, adjusts `PROJECT SLOT`, then triggers `SAVE PROJECT`/`LOAD PROJECT`; worker writes/reads the selected slot file (`PROJECTNN.AKPRJ`) via temp-file rename (`PROJECTNN.TMP` -> `PROJECTNN.AKPRJ`); project recall restores the saved WAV into slot 0 / layer A; `project_status` reports slot-tagged save/load/empty/error state; the temporary `ProjectStatus` screen stays visible until REnc click.
- Apply strategy: Load sets pending edit + starts WAV load; macro/mod state published immediately; audio applies via existing handoff once ready.
- Where to look: `ui_requests.h` (SaveProject/LoadProject), `ui_worker.cpp` (`SaveProject`, `LoadProject`, explicit-slot restore path), `project_manifest.h` (fields), `app_state.h` (`project_status`, `project_edit_pending`, `project_action_slot`), `ui_screens.cpp` (Settings + ProjectStatus), `ui_overlay.cpp` (SAVE line for WAV renders, not project).

## Repo hygiene
- Do not commit build/ or binaries.
- Clean zip export: `git archive -o ADSR_V2_clean.zip HEAD`

## Where to look in code
- main.cpp — control tick scheduling + ctrl_hz counter; ownership comments.
- ui_logic.cpp — UI tick gate (`kUiTickMs`) and UI event drain.
- ui_render.cpp — UI draw gate + `ui_hz` measurement.
- controls.cpp — `Controls_Tick` hardware scan + debounce.
- ui_input.cpp — UI input event queue (controls → UI).
- ui_screens.cpp — HUD line showing `U:.. C:....` counters.
