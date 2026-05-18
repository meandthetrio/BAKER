# FILE_MAP (ADSR_V2)

## Purpose
- This is the repo ownership and navigation map.
- Use it to answer two questions quickly:
  - Which file owns a behavior? 
  - Which thread is allowed to touch it?
- Read `START_HERE.md` first if you are new to the repo, then use `README_GPT.md` and this file for deeper navigation.
- PolyPorto runtime behavior and intended ownership are documented in `POLY_PORTO.md`.

## Architecture
- `[AUDIO]` deterministic audio callback DSP only. No malloc, file I/O, logging, or UI work.
- `[MAIN/UI]` control tick, UI tick, navigation, rendering, and request creation.
- `[BG]` SD/filesystem work, WAV/project load/save, normalize, loop-find.
- `[SHARED]` state and handoff structures touched across domains at explicit boundaries.

## AppState Policy
- `AppState` is the composition root for cross-subsystem wiring.
- Top-level orchestration files may still use `AppState` directly.
- Leaf subsystem files should prefer specific `app_state_*` bucket headers or narrow context structs.
- `app_state.h` should gradually disappear from UI, worker, and other leaf modules as dependency narrowing progresses.
- Stable whole-app boundaries in the current repo are `main.cpp`, `src/ui/ui_logic.cpp`, `src/ui/ui_render.cpp`, `src/worker/ui_worker.cpp`, and `src/worker/ui_worker_project.cpp`.
- Smaller wrappers that still take `AppState&` outside those orchestration boundaries should be treated as temporary compatibility seams, not as approved long-term patterns.

## Repo Layout
- `/` main firmware source tree; entry points, shared state, controls, and engine/shared support still live at repo root.
- `/src/ui` UI framework helpers, screen owners, router/registry glue, draw helpers, and thin project UI helpers.
- `/src/worker` worker request handlers and project persistence internals.
- `/src/storage` SD browser state and sample-pool storage helpers.
- `/docs` project docs.
- `/tools` helper scripts/tools.
- `/build` generated output only.

## Entry Points
- `main.cpp` `[MAIN]` app entry, hardware init, `AudioCallback`, main-loop scheduling, UART + USB MIDI polling, and MIDI to event mapping.
- `app_state.h` `[SHARED]` composition root for runtime state and cross-subsystem wiring; prefer narrower `app_state_*` headers away from orchestration boundaries.

## Module Map

### Audio / DSP
- `audio_engine.cpp` / `audio_engine.h` `[AUDIO]` FX and final mix layer.
- `voice_engine.h` / `voice_engine_internal.h` `[AUDIO]` shared engine state, voice state, internal contracts.
- `voice_engine.cpp` `[AUDIO]` engine init plus sample/edit and per-layer setter entry points.
- `voice_engine_poly_porto.cpp` `[AUDIO]` narrow PolyPorto owner: setters, source eligibility/selection, release-window helpers, active glide counting, and PolyPorto note start helper.
- `voice_engine_playback.cpp` `[AUDIO]` playback math, loop stepping, envelope helpers.
- `voice_engine_events.cpp` `[AUDIO]` event consumption and note-start dispatch.
- `voice_engine_voice_lifecycle.cpp` `[AUDIO]` allocation, stealing, start/stop, note-off behavior.
- `voice_engine_render.cpp` `[AUDIO]` per-block rendering and per-voice render path.
- `voice_engine_emphasis.cpp` `[AUDIO]` per-layer emphasis bus/filter processing.
- `sampler_sample.h` `[SHARED]` sample metadata consumed by the voice engine.
- `mod_sources.cpp` / `mod_sources.h` `[AUDIO]` LFO and envelope-style modulation sources.
- `embedded_sample.h` / `embedded_long_sample.h` `[SHARED]` built-in fallback sample banks.

### Shared Handoffs / Control Data
- `event_queue.h` `[SHARED]` SPSC MIDI/voice event queue written by MAIN and read by AUDIO.
- `params.cpp` / `params.h` `[SHARED]` target/current parameter lanes and block-boundary smoothing.
- `mod_matrix.cpp` / `mod_matrix.h` `[SHARED]` mod-route storage and publish path.
- `plocks.cpp` / `plocks.h` `[SHARED]` parameter-lock snapshot/publish state.
- `keygroups.cpp` / `keygroups.h` `[MAIN/UI]` keyzone helpers and legacy keygroup/sample selection helpers.
- `velocity_layers.cpp` / `velocity_layers.h` `[MAIN/UI]` velocity-layer selection.
- `sample_edit.h` `[SHARED]` non-destructive trim/loop/gain edit metadata.

### UI Framework
- `src/ui/ui_logic.cpp` / `src/ui/ui_logic.h` `[MAIN/UI]` control tick, UI tick, worker tick orchestration.
- `controls.cpp` / `controls.h` `[MAIN/UI]` hardware scan and debounce.
- `src/ui/ui_input.cpp` / `src/ui/ui_input.h` `[MAIN/UI]` UI input queue from controls into UI logic.
- `src/ui/ui_render.cpp` / `src/ui/ui_render.h` `[MAIN/UI]` OLED render tick and render-budget tracking.
- `src/ui/ui_layout.cpp` / `src/ui/ui_layout.h` `[MAIN/UI]` common header/body/footer layout helpers.
- `src/ui/ui_overlay.cpp` / `src/ui/ui_overlay.h` `[MAIN/UI]` diagnostics overlay.
- `src/ui/ui_list_menu.cpp` / `src/ui/ui_list_menu.h` `[MAIN/UI]` list-menu widget.
- `src/ui/ui_value_edit.cpp` / `src/ui/ui_value_edit.h` `[MAIN/UI]` value-edit widget.
- `src/ui/oled_pager.cpp` / `src/ui/oled_pager.h` `[MAIN/UI]` paged OLED drawing helper.
- `ui_screens.cpp` / `ui_screens.h` `[MAIN/UI]` intentional small shared UI facade: active-screen lookup plus shared waveform/name/Perform helper functions that do not belong to one screen owner.
- `src/ui/ui_router.cpp` `[MAIN/UI]` nav push/pop and central event/render dispatch.
- `src/ui/ui_screen_registry.cpp` `[MAIN/UI]` `UiScreenId` to screen-function bindings.
- `src/ui/ui_draw_text.cpp` / `src/ui/ui_draw_text.h` `[MAIN/UI]` shared text/font helpers.
- `src/ui/ui_draw_shapes.cpp` / `src/ui/ui_draw_shapes.h` `[MAIN/UI]` shared primitive/shape helpers.
- `src/ui/ui_draw_controls.cpp` / `src/ui/ui_draw_controls.h` `[MAIN/UI]` shared control-visual helpers.
- `src/ui/ui_screens_internal.h` `[MAIN/UI]` cross-screen declarations and shared helper access for split screen units while the shared facade stays intentionally small.

### UI Screens
- `src/ui/ui_screen_main.cpp` `[MAIN/UI]` Start and Presets screens.
- `src/ui/ui_screen_hud.cpp` `[MAIN/UI]` HUD screen.
- `src/ui/ui_screen_fx.cpp` `[MAIN/UI]` FX screen.
- `src/ui/ui_screen_mod.cpp` `[MAIN/UI]` MOD screen.
- `src/ui/ui_screen_macro.cpp` `[MAIN/UI]` MACRO screen.
- `src/ui/ui_screen_shift.cpp` `[MAIN/UI]` SHIFT/settings screen, including project slot UI.
- `src/ui/project_actions.cpp` / `src/ui/project_actions.h` `[MAIN/UI]` project slot wrap plus save/load request triggering.
- `src/ui/ui_screen_status.cpp` `[MAIN/UI]` project status screen.
- `src/ui/ui_screen_sd_browse.cpp` `[MAIN/UI]` SD browse screen.
- `src/ui/ui_screen_sd_delete_confirm.cpp` `[MAIN/UI]` SD delete-confirm screen.
- `src/ui/ui_screen_sample_edit.cpp` `[MAIN/UI]` sample-edit screen.
- `src/ui/ui_screen_record.cpp` `[MAIN/UI]` record rendering and visual state presentation.
- `src/ui/ui_screen_record_event.cpp` `[MAIN/UI]` record event handling and lifecycle transitions.
- `src/ui/ui_screen_perform_menu.cpp` `[MAIN/UI]` Perform menu shell.
- `src/ui/ui_screen_perform_engine.cpp` `[MAIN/UI]` Perform Engine screen.
- `src/ui/ui_screen_perform_wave_edit.cpp` `[MAIN/UI]` Perform Wave Edit screen.
- `src/ui/ui_screen_perform_keyzone.cpp` `[MAIN/UI]` Perform Keyzone screen.
- `src/ui/ui_screen_perform_adsr.cpp` `[MAIN/UI]` Perform ADSR screen.
- `src/ui/ui_screen_perform_emphasis.cpp` `[MAIN/UI]` Perform Emphasis screen.
- `src/ui/ui_screen_perform_process.cpp` `[MAIN/UI]` Perform Process routing/layout owner.
- `src/ui/ui_screen_perform_process_draw.cpp` `[MAIN/UI]` heavy draw helpers for Perform Process.

### Worker / Storage / Persistence
- `ui_requests.cpp` / `ui_requests.h` `[MAIN/UI]` UI request queue into the worker.
- `src/worker/ui_worker.cpp` / `src/worker/ui_worker.h` `[BG]` request dispatch plus worker-step orchestration.
- `src/worker/ui_worker_project.cpp` `[BG]` project save/load orchestration plus project-restore sample handoff helpers.
- `src/worker/ui_worker_project_manifest.cpp` `[BG]` versioned project manifest read/upgrade helpers.
- `src/worker/ui_worker_wav.cpp` `[BG]` WAV header/path helpers shared by scan/load/save flows.
- `src/worker/ui_worker_internal.h` `[BG]` internal worker declarations/state shared by split worker units.
- `src/storage/storage_limits.h` `[SHARED]` shared SD path/name/slot constants used by state and storage surfaces.
- `src/storage/sd_browser_state.cpp` / `src/storage/sd_browser_state.h` `[MAIN/UI]` browser list state and last-loaded metadata.
- `src/storage/sd_sample_pool.cpp` / `src/storage/sd_sample_pool.h` `[SHARED]` SDRAM sample slot buffers.
- `src/worker/project_manifest.h` `[BG]` on-disk project manifest structure.

## Cross-Thread Handoffs
- Event queue: `event_queue.h`
  - MAIN writes MIDI/voice events, AUDIO consumes in `VoiceEngine::ProcessEvents`.
  - Both UART MIDI and USB MIDI are decoded on MAIN and feed this same queue path.
- Parameter publish/smoothing: `params.*`
  - MAIN edits/publishes targets, AUDIO consumes smoothed current values once per block.
- UI request to worker: `ui_requests.*` -> `ui_worker.*`
  - UI enqueues requests, BG executes and updates `AppState`.
- Sample publish/apply: `app_state.h` + `src/worker/ui_worker.cpp` + `main.cpp`
  - BG publishes `sd_published_*`, AUDIO swaps samples at block boundaries in `AudioCallback`.
- Edit publish/apply: `app_state.h` + `src/worker/ui_worker.cpp` + `main.cpp` + `voice_engine.cpp`
  - MAIN/BG publish `sd_edit_*`, AUDIO applies `SetSampleEdit(...)` at block boundaries.

## Where To Look
- App startup / scheduler: `main.cpp`, `src/ui/ui_logic.cpp`
- Central shared state: `app_state.h`
- Audio FX/mix: `audio_engine.cpp`
- Voice allocation / stealing: `voice_engine_voice_lifecycle.cpp`
- Voice event dispatch: `voice_engine_events.cpp`
- Voice rendering: `voice_engine_render.cpp`
- Playback ratio / loop stepping / ADSR DSP: `voice_engine_playback.cpp`
- PolyPorto runtime behavior / ownership: `docs/POLY_PORTO.md`, `voice_engine_poly_porto.cpp`
- Emphasis bus/filter DSP: `voice_engine_emphasis.cpp`
- Parameter smoothing/publish: `params.cpp`
- Control scanning: `controls.cpp`
- UI dispatch / active screen: `src/ui/ui_router.cpp`, `src/ui/ui_screen_registry.cpp`, `ui_screens.cpp`
- Shared UI helpers: `ui_screens.cpp`, `src/ui/ui_draw_*.cpp`, `src/ui/ui_layout.cpp`
- Start / Presets: `src/ui/ui_screen_main.cpp`
- HUD: `src/ui/ui_screen_hud.cpp`
- FX / MOD / MACRO: `src/ui/ui_screen_fx.cpp`, `src/ui/ui_screen_mod.cpp`, `src/ui/ui_screen_macro.cpp`
- SD browse / sample edit: `src/ui/ui_screen_sd_browse.cpp`, `src/ui/ui_screen_sample_edit.cpp`, `src/storage/sd_browser_state.cpp`, `src/worker/ui_worker.cpp`
- Record flow: `src/ui/ui_screen_record.cpp`, `src/ui/ui_screen_record_event.cpp`
- Perform screens: `src/ui/ui_screen_perform_*.cpp`, plus shared helpers in `ui_screens.cpp`
- Project slot UI: `src/ui/ui_screen_shift.cpp`, `src/ui/project_actions.cpp`, `src/ui/ui_screen_status.cpp`
- Project save/load I/O: `src/worker/ui_worker_project.cpp`, `src/worker/ui_worker_project_manifest.cpp`, `src/worker/project_manifest.h`

## Known Hotspots / Transitional Owners
- `app_state.h`
  - Still the largest shared-state surface. Many UI, worker, SD, and diagnostic fields converge here.
- `ui_screens.cpp`
  - Intentionally small, but still a shared helper hotspot for waveform preview, filename helpers, and shared Perform publish/metadata helpers.
- `src/ui/ui_screens_internal.h`
  - Transitional coordination header for the split UI screen files. If ownership feels unclear across screen units, start here.
- `src/worker/ui_worker.cpp`, `src/worker/ui_worker_project.cpp`, `src/worker/ui_worker_project_manifest.cpp`, and `src/worker/ui_worker_wav.cpp`
  - Worker ownership is split by orchestration, project persistence, and WAV/path helper responsibilities.
- `voice_engine.cpp` + `voice_engine_*.cpp`
  - Engine ownership is split by concern rather than collapsed into one file. Treat the set as one module with specialized owners, not as a monolith.

## Project Save / Load
- Current slot state lives in `app_state.h` as `current_project_slot`.
- Slot UI lives in `src/ui/ui_screen_shift.cpp`.
- Save/load request triggering lives in `src/ui/project_actions.cpp`.
- Background project file I/O lives in `src/worker/ui_worker_project.cpp` and `src/worker/ui_worker_project_manifest.cpp`.
- Manifest format lives in `src/worker/project_manifest.h`.
- Project files are `PROJECT01.AKPRJ` through `PROJECT08.AKPRJ` at SD root.

## Notes
- Embedded sample banks in `embedded_sample.h` / `embedded_long_sample.h` are separate from SD-loaded samples.
