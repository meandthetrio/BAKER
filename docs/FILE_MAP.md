# FILE_MAP (ADSR_V2)

## Read this first
- If you are new: read README_GPT.md (if present) or README.md, then this file.
- Audio-thread rule summary: deterministic DSP only; no malloc, file I/O, logging, or UI work.
- Main-thread rule summary: UI/control ticks, SD/filesystem, background work; communicate to audio via queues/hand-offs at block boundaries.
- Milestones remain “TBD” until verified milestone-by-milestone (0.0–4.4).

## Repo map (top-level)
- /docs — project docs (this file)
- /tools — helper scripts/tools (non-firmware)
- /build — build artifacts (generated)
- / — firmware source (all *.cpp/*.h in repo root)
- Flat layout: most firmware sources live in repo root (no /src folder).

## Anchor entry points
### App / scheduling
- main.cpp
  - Purpose: App entry, hardware init, defines AudioCallback, main loop scheduling, MIDI → event mapping.
  - Thread: [MAIN]
  - Key symbols: `AudioCallback`, `g_voice`, `g_params`, `LayerEligibleForNote`, `UILogic::ControlTick`, `UILogic::UiTick`
  - Milestones: TBD

- app_state.h
  - Purpose: Single shared state container (UI flags, counters, SD state, sample/edit handoffs).
  - Thread: [SHARED]
  - Key symbols: `AppState`, `sd_published_*`, `sd_edit_*`, `ui_req_*`, `voices_active`
  - Notes: shared PERFORM ADSR UI state now also owns LOOP wave-preview focus plus per-layer seam-crossfade amount
  - Milestones: TBD

### Audio engine (callback → voices → mix)
- audio_engine.cpp / audio_engine.h
  - Purpose: FX/mix layer (delay/reverb/sat), block processing.
  - Thread: [AUDIO]
  - Key symbols: `AudioEngine::ProcessBlock`, `AudioEngine::Init`, `kDelayMaxSamples`
  - Milestones: TBD

- sampler_sample.h
  - Purpose: Sample metadata used by the voice engine.
  - Thread: [SHARED]
  - Key symbols: `Sample`, `loop_start`, `loop_end`
  - Milestones: TBD

- voice_engine.cpp / voice_engine.h
  - Purpose: Voice pool, sample playback, looping, ADSR, voice stealing, sample/edit apply.
  - Thread: [AUDIO]
  - Key symbols: `VoiceEngine::ProcessEvents`, `RenderBlock`, `SetSample`, `SetSampleEdit`, `SetLoopEnvelopeParams`, `SetLoopCrossfadeAmount`, `StealVoice_`
  - Milestones: TBD

- mod_sources.cpp / mod_sources.h
  - Purpose: Mod sources (LFO/env) used by voices.
  - Thread: [AUDIO]
  - Key symbols: `ModSources`, `LFO`, `ModEnv`
  - Milestones: TBD

- embedded_sample.h / embedded_long_sample.h
  - Purpose: Built-in sample banks (fallback/legacy).
  - Thread: [SHARED]
  - Key symbols: `g_sample_bank`, `g_long_sample_bank` (if present)
  - Milestones: TBD

### Event + parameter plumbing (queues / smoothing)
- event_queue.h
  - Purpose: SPSC event queue for MIDI/voice events.
  - Thread: [SHARED]
  - Writer: MAIN (MIDI/control), Reader: AUDIO (voice engine)
  - Key symbols: `EventQueueSPSC`, `Event::NoteOnEvent`, `Push`, `Pop`
  - Milestones: TBD

- params.cpp / params.h
  - Purpose: Parameter lane targets + smoothing for audio thread.
  - Thread: [SHARED]
  - Writer: MAIN (EditTargets/Publish), Reader: AUDIO (AudioBlockTick)
  - Key symbols: `Params::EditTargets`, `PublishTargets`, `AudioBlockTick`, `PerformParamsTargets`
  - Notes: shared PERFORM handoff now also carries per-layer LOOP ADSR runtime values and LOOP seam-crossfade amount
  - Milestones: TBD

- mod_matrix.cpp / mod_matrix.h
  - Purpose: Mod route storage + publish to audio thread.
  - Thread: [SHARED]
  - Writer: MAIN (edit UI routes), Reader: AUDIO (latched routes)
  - Key symbols: `ModMatrix_Publish`, `ModMatrixState`, `ModRoute`
  - Milestones: TBD

- plocks.cpp / plocks.h
  - Purpose: Parameter locks (pattern + publish step snapshot).
  - Thread: [SHARED]
  - Key symbols: `PLocks_PublishCurrentStep`, `PLocksState`, `Pattern`
  - Milestones: TBD

- keygroups.cpp / keygroups.h
  - Purpose: Note-range helpers plus legacy sample/keygroup selection logic used in MIDI mapping.
  - Thread: [MAIN/UI]
  - Key symbols: `KeyzoneContainsNote`, `kPerformKeyzoneMinNote`, `kPerformKeyzoneMaxNote`, `Keygroups_SelectSampleIndex`
  - Milestones: TBD

- velocity_layers.cpp / velocity_layers.h
  - Purpose: Velocity layer selection for events.
  - Thread: [MAIN/UI]
  - Key symbols: `VelocityLayer_ForVel`
  - Milestones: TBD

### UI system (router / ticks / widgets / rendering)
- ui_logic.cpp / ui_logic.h
  - Purpose: Control tick (1 kHz), UI tick (≈60 Hz), global input routing, worker tick.
  - Thread: [MAIN/UI]
  - Key symbols: `UILogic::ControlTick`, `UILogic::UiTick`, `UiOverlay_Update`
  - Milestones: TBD

- controls.cpp / controls.h
  - Purpose: Hardware scanning + debounce → UiInputEvents.
  - Thread: [MAIN/UI]
  - Key symbols: `Controls_Tick`, `ControlsState`
  - Milestones: TBD

- ui_input.cpp / ui_input.h
  - Purpose: UI input event queue (controls → UI).
  - Thread: [MAIN/UI]
  - Key symbols: `UiInputEvent`, `UiInput_Push`, `UiInput_Pop`
  - Milestones: TBD

- ui_screens.cpp / ui_screens.h
  - Purpose: Screen router + page stack; screen event handlers + rendering.
  - Thread: [MAIN/UI]
  - Key symbols: `UiNav_Push`, `UiRouter_DispatchEvent`, `Hud_Render`, `SdBrowse_OnEvent`, `SampleEdit_OnEvent`
  - Milestones: TBD

- ui_render.cpp / ui_render.h
  - Purpose: OLED render tick, guardrail timing, UI FPS counter.
  - Thread: [MAIN/UI]
  - Key symbols: `UIRender::Tick`, `kRenderBudgetMs`, `render_skips`
  - Milestones: TBD

- ui_list_menu.cpp / ui_list_menu.h
  - Purpose: List menu widget (cursor + scroll + render).
  - Thread: [MAIN/UI]
  - Key symbols: `UiListMenu_OnEnc`, `UiListMenu_Render`, `UiMenuItem`
  - Milestones: TBD

- ui_value_edit.cpp / ui_value_edit.h
  - Purpose: Value editor widget (edit mode overlay).
  - Thread: [MAIN/UI]
  - Key symbols: `UiValueEdit_Begin`, `UiValueEdit_OnEnc`, `UiValueEdit_Render`
  - Milestones: TBD

- ui_layout.cpp / ui_layout.h
  - Purpose: Standard header/body/footer layout + status string.
  - Thread: [MAIN/UI]
  - Key symbols: `UiLayout_Default`, `UiDraw_Header`, `BuildStatus`
  - Milestones: TBD

- ui_overlay.cpp / ui_overlay.h
  - Purpose: Global diagnostics overlay (SHIFT hold).
  - Thread: [MAIN/UI]
  - Key symbols: `UiOverlay_Update`, `UiOverlay_Render`
  - Milestones: TBD

- oled_pager.cpp / oled_pager.h
  - Purpose: OLED draw helper (paged rendering).
  - Thread: [MAIN/UI]
  - Key symbols: `OledPager::Tick`, `WriteString`, `SetCursor`
  - Milestones: TBD

### SD / filesystem / background loading / persistence
- ui_requests.cpp / ui_requests.h
  - Purpose: UI request queue (UI → worker).
  - Thread: [MAIN/UI]
  - Key symbols: `UiReq`, `UiReq_Push`, `UiReqType::{ScanSdWavs,LoadWavIndex,SaveRenderedWavCurrent,SaveProject,LoadProject}`
  - Milestones: TBD

- ui_worker.cpp / ui_worker.h
  - Purpose: Background worker (SD scan/load/save WAV, normalize, loop find, project save/load).
  - Thread: [BG]
  - Key symbols: `UiWorker_Tick`, `StartLoad`, `SaveStep`, `SaveProject`, `LoadProject`
  - Milestones: TBD

- sd_browser_state.cpp / sd_browser_state.h
  - Purpose: SD browser list + status + last_loaded_path.
  - Thread: [MAIN/UI]
  - Key symbols: `SdBrowserState`, `SdBrowser_RebuildMenu`, `last_loaded_path`
  - Milestones: TBD

- sd_sample_pool.cpp / sd_sample_pool.h
  - Purpose: SDRAM sample buffers + slot helpers (double buffer).
  - Thread: [SHARED]
  - Writer: BG (loader), Reader: AUDIO (current sample)
  - Key symbols: `SdSampleBuffer`, `SdSampleMaxFrames`, `kSdSampleSlots`
  - Milestones: TBD

- sample_edit.h
  - Purpose: Non-destructive edit metadata (trim/loop/gain).
  - Thread: [SHARED]
  - Writer: MAIN/BG (edit + normalize), Reader: AUDIO (apply per sample)
  - Key symbols: `SampleEdit`, `SampleEdit_Default`, `SampleEdit_Clamp`
  - Milestones: TBD

- project_manifest.h
  - Purpose: Project save/load binary manifest definition.
  - Thread: [MAIN/UI]
  - Key symbols: `ProjectManifest`, `kProjectManifestVersion`
  - Milestones: TBD

### Diagnostics / counters
- app_state.h
  - Purpose: Diagnostics counters (CPU, late, clip, queue stats, render stats).
  - Thread: [SHARED]
  - Key symbols: `audio_late_count`, `clip_count`, `ui_hz`, `ctrl_hz`, `render_hi_ms`
  - Milestones: TBD

- ui_overlay.cpp
  - Purpose: On-screen diagnostics overlay.
  - Thread: [MAIN/UI]
  - Key symbols: `UiOverlay_Render`, `CPU`, `LATE`, `UIQ`, `SAVE`
  - Milestones: TBD

## Cross-thread contracts (high value)
- EventQueueSPSC — `event_queue.h`: MAIN writes MIDI/voice events, AUDIO consumes in `VoiceEngine::ProcessEvents` each block.
- Params publish/smoothing — `params.*`: MAIN edits + publishes targets, AUDIO reads smoothed `Params::current` per block.
- UI request → worker — `ui_requests.*` + `ui_worker.*`: UI enqueues jobs, BG executes and updates AppState/SD state.
- SD publish/apply handoff — `app_state.h` + `ui_worker.cpp` + `main.cpp`: BG publishes `sd_published_*`, AUDIO applies/swaps at audio block boundaries (in AudioCallback / voice engine).
- Edit handoff — `app_state.h` + `ui_worker.cpp` + `voice_engine.cpp`: MAIN/BG publish `sd_edit_*`, AUDIO applies/swaps at audio block boundaries (in AudioCallback / voice engine).

## “Where is X?” quick index
- Control tick / hardware scanning: `controls.cpp`, `ui_logic.cpp`
- UI tick / drawing: `ui_logic.cpp`, `ui_render.cpp`, `ui_screens.cpp`
- Voice/MIDI event queue: `event_queue.h` (produced in `main.cpp`, consumed in `voice_engine.cpp`)
- UI input event queue: `ui_input.*` (produced in `controls.cpp`, consumed in `ui_logic.cpp`)
- Parameter lane / smoothing: `params.cpp`
- Voice pool: `voice_engine.cpp`
- Voice stealing: `voice_engine.cpp` (`StealVoice_`)
- Sample reader: `voice_engine.cpp`, `sampler_sample.h`
- Looping: `voice_engine.cpp`, `sample_edit.h`
- ADSR: `voice_engine.cpp`
- Filter: `voice_engine.cpp`, `mod_sources.cpp`
- Mixer/headroom/clip counter: `audio_engine.cpp`, `app_state.h`
- Parameter locks: `plocks.cpp`
- Keygroups/zones: `keygroups.cpp`, `main.cpp`, `velocity_layers.cpp`
- SD browser: `ui_screens.cpp` (SdBrowse), `sd_browser_state.cpp`
- Background load handoff: `ui_worker.cpp`, `sd_sample_pool.cpp`, `main.cpp`
- Presets/project save/load: `ui_worker.cpp`, `project_manifest.h`

## Notes
- Embedded sample banks live in `embedded_sample.h` / `embedded_long_sample.h` and are separate from SD-loaded samples.
