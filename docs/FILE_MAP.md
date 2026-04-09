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
  - Key symbols: `AppState`, `WorkerState`, `worker.ui_req_*`, `sd_published_*`, `sd_edit_*`, `voices_active`
  - Notes: worker request queue/progress fields now live under `AppState::worker`; shared PERFORM ADSR UI state now also owns LOOP wave-preview focus plus per-layer seam-crossfade amount and shape; shared PERFORM engine state also owns the per-layer EMPHASIS drive mode selector (`odd`/`even`)
  - Milestones: TBD

### Audio engine (callback → voices → mix)
- audio_engine.cpp / audio_engine.h
  - Purpose: FX/mix layer (stereo dual delay / reverb / sat), block processing.
  - Thread: [AUDIO]
  - Key symbols: `AudioEngine::ProcessBlock`, `AudioEngine::Init`, `kDelayMaxSamples`
  - Milestones: TBD

- sampler_sample.h
  - Purpose: Sample metadata used by the voice engine.
  - Thread: [SHARED]
  - Key symbols: `Sample`, `loop_start`, `loop_end`
  - Milestones: TBD

- voice_engine.cpp / voice_engine.h
  - Purpose: Voice pool, sample playback, looping, ADSR, voice stealing, sample/edit apply, and per-layer EMPHASIS bus DSP.
  - Thread: [AUDIO]
  - Key symbols: `VoiceEngine::ProcessEvents`, `RenderBlock`, `SetSample`, `SetSampleEdit`, `SetLoopEnvelopeParams`, `SetLoopCrossfadeAmount`, `SetLoopCrossfadeShape`, `SetEngineDriveMode`, `ProcessLayerBusSample_`
  - Notes: EMPHASIS cutoff/resonance now process each summed layer bus in `RenderBlock(...)` instead of the old per-voice filter stage
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
  - Notes: shared PERFORM handoff now also carries per-layer LOOP ADSR runtime values plus LOOP seam-crossfade amount and shape, and the per-layer EMPHASIS drive mode
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
  - Purpose: Screen router + page stack plus shared/main-menu, record, perform, HUD, FX/MOD/MACRO, and settings-screen logic.
  - Thread: [MAIN/UI]
  - Key symbols: `UiNav_Push`, `UiRouter_DispatchEvent`, `Hud_Render`, `ShiftMenu_OnEvent`, `PerformProcess_Render`
  - Notes: Project save/load UI still lives in the Button1 Settings screen, but request/status workflow triggering now routes through `project_actions.cpp`; triggering save/load pushes a temporary `ProjectStatus` screen and REnc click pops it back to Settings.
  - Milestones: TBD

- project_actions.cpp / project_actions.h
  - Purpose: Thin project workflow helpers used by UI screens for project slot wrap and save/load request triggering.
  - Thread: [MAIN/UI]
  - Key symbols: `ProjectActions_WrapSlot`, `ProjectActions_TriggerRequest`
  - Milestones: TBD

- ui_screen_status.cpp
  - Purpose: Extracted status-domain screens from `ui_screens.cpp`, currently `ProjectStatus`.
  - Thread: [MAIN/UI]
  - Key symbols: `ProjectStatus_OnEvent`, `ProjectStatus_Render`
  - Milestones: TBD

- ui_screen_browser.cpp
  - Purpose: Extracted browse/edit-domain screens from `ui_screens.cpp`, currently `SdBrowse`, `SdDeleteConfirm`, and `SampleEdit`.
  - Thread: [MAIN/UI]
  - Key symbols: `SdBrowse_OnEvent`, `SdDeleteConfirm_OnEnter`, `SampleEdit_OnEvent`
  - Milestones: TBD

- ui_screens_internal.h
  - Purpose: Internal declarations/helpers shared across the split UI screen translation units.
  - Thread: [MAIN/UI]
  - Key symbols: `DrawScaledText6x8`, `DrawTinyString`, `TinyStringWidth`, `ExtractBaseName`
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
  - Purpose: Background worker loop, request dispatch, SD scan/load/save WAV, normalize, and loop find.
  - Thread: [BG]
  - Key symbols: `UiWorker_Tick`, `StartLoad`, `SaveStep`
  - Milestones: TBD

- ui_worker_project.cpp
  - Purpose: Worker-owned project save/load operations and project-restore sample handoff helpers.
  - Thread: [BG]
  - Key symbols: `SaveProject`, `LoadProject`, `RequestedProjectSlot`, `SetProjectSlotStatus`
  - Notes: Project save/load stays worker-owned and writes fixed numbered manifests `PROJECT01.AKPRJ` ... `PROJECT08.AKPRJ` using temp-file rename (`PROJECTNN.TMP` -> `PROJECTNN.AKPRJ`); project manifests now store per-layer sample paths/edits plus per-layer ENGINE tune, per-layer KEYZONE note bounds, per-layer ADSR submenu state (row/playback mode, loop A/D/S/R, loop crossfade/shape, ADSR graph points), per-layer EMPHASIS state (drive amount/mode, filter cutoff, resonance), per-layer PROCESS layer master levels, PROCESS FX lane order, PROCESS SAT state, and PROCESS EQ state, and project recall restores each saved layer into its explicit slot (`A->0`, `B->1`) instead of the normal inactive-slot loader path.
  - Milestones: TBD

- ui_worker_internal.h
  - Purpose: Internal shared worker declarations/state for the split worker translation units.
  - Thread: [BG]
  - Key symbols: `SdWorkerState`, `LoaderState`, `s_sd`
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
  - Notes: Manifest stores recall metadata/state only (sample path/edit, selected per-layer musical params, seq/macros/mod routes), not WAV PCM.
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
- UI tick / drawing: `ui_logic.cpp`, `ui_render.cpp`, `ui_screens.cpp`, `ui_screen_status.cpp`, `ui_screen_browser.cpp`
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
- SD browser: `ui_screen_browser.cpp` (SdBrowse), `sd_browser_state.cpp`
- Background load handoff: `ui_worker.cpp`, `sd_sample_pool.cpp`, `main.cpp`
- Presets/project save/load: `ui_worker_project.cpp`, `project_manifest.h`

## Project slots
- Slot ownership: `app_state.h` holds `current_project_slot` (0-based internally, 1-based in UI).
- Slot selection UI: `ui_screens.cpp` Button1 Settings screen owns `PROJECT SLOT`.
- Operation UI: Button1 Settings screen owns `SAVE PROJECT` / `LOAD PROJECT`; `project_actions.cpp` performs slot wrapping and request/status triggering, and the resulting temporary project-status screen shows slot, action, and live status until REnc click dismisses it.
- Slot filenames: `PROJECT01.AKPRJ` ... `PROJECT08.AKPRJ` at SD root.
- Save/load flow: Settings enqueues `UiReqType::{SaveProject,LoadProject}`; `ui_worker.cpp` dispatches the requests and `ui_worker_project.cpp` performs the project file I/O off the audio thread.
- Recall target: project recall stores explicit layer assignments and restores saved WAVs back into their original slots/layers deterministically (`A->0`, `B->1`).
- ENGINE tune: project manifests also persist `app.engine_tune_semitones[0/1]` and `ui_worker_project.cpp` republishes them through `Params` on load so restored tune is both visible and audible.
- KEYZONE: project manifests also persist `perform_keyzone_lo_note[0/1]` + `perform_keyzone_hi_note[0/1]` and `ui_worker_project.cpp` republishes them through `Params` on load so restored note gating matches the KEYZONE screen.
- ADSR: project manifests also persist `perform_adsr_row[0/1]`, `engine_play_mode[0/1]`, per-layer loop A/D/S/R, per-layer loop crossfade/shape, and per-layer ADSR graph/control points; `ui_worker_project.cpp` republishes the loop/playback subset through `Params` on load, skips the normal manual-load crossfade default reset when a layer load is part of project recall, and `ui_screens.cpp` preserves the restored ADSR row on screen entry instead of overwriting it from `engine_play_mode`.
- EMPHASIS: project manifests also persist `engine_gain_db[0/1]`, `engine_drive_mode[0/1]`, and per-layer filter cutoff/resonance; `ui_worker_project.cpp` captures cutoff/resonance from `Params::TargetsForUI()` on save and republishes all per-layer EMPHASIS values through `Params` on load so the EMPHASIS screen and audible drive/filter state match after recall.
- PROCESS layer master level: project manifests also persist canonical `engine_layer_master_level[0/1]` from `Params::TargetsForUI()` and `ui_worker_project.cpp` republishes those values through `Params` on load so restored layer balance is audible; `perform_process_vol_muted`, `perform_process_vol_unmuted_level`, and `perform_process_vol_pct` remain runtime UI helpers, are not serialized, and are re-derived from the restored canonical levels on load.
- PROCESS FX order: project manifests also persist canonical `fx_order[4]` from `Params::TargetsForUI()` and `ui_worker_project.cpp` republishes that order through `Params` on load so restored insert routing is audible; `perform_process_fx_order` is only the PROCESS screen’s mirrored UI copy, and cursor/detail fields such as `perform_process_fx_cursor`, `perform_process_main_cursor`, `perform_process_detail_active`, `perform_process_eq_graph_active`, and `perform_process_detail_param` are not serialized.
- PROCESS SAT: project manifests also persist canonical `sat_on`, `sat_mode`, `sat_mix`, `sat_drive`, `sat_bump`, `sat_bit_reso`, and `sat_bit_smpl` from `Params::TargetsForUI()` and `ui_worker_project.cpp` republishes that SAT block through `Params` on load so restored saturation mode/tone is audible; PROCESS cursor/detail helper state is not serialized.
- PROCESS EQ: project manifests also persist canonical `eq_on`, `eq_mix`, `eq_center_norm`, `eq_tilt_db`, and `eq_q` from `Params::TargetsForUI()` and `ui_worker_project.cpp` republishes that EQ block through `Params` on load so restored EQ tone is audible; PROCESS graph/detail helper state is not serialized.
- Empty slot behavior: loading a missing slot fails safely with visible `PNN EMPTY` status.

## Notes
- Embedded sample banks live in `embedded_sample.h` / `embedded_long_sample.h` and are separate from SD-loaded samples.
