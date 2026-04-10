# README_GPT (ADSR_V2)

## Purpose
Use this document as a short working guide for repo analysis. It is intentionally narrower than `FILE_MAP.md`, `MILESTONE_STATE.md`, and `TEST_MATRIX.md`.

## What this doc covers
- Repo purpose and architecture mental model.
- Thread ownership rules and cross-thread handoff patterns.
- High-level guidance for where to start reading.

## What this doc does not cover
- Detailed file ownership maps. Use `FILE_MAP.md`.
- Milestone-by-milestone implementation proof. Use `MILESTONE_STATE.md`.
- Detailed validation procedures. Use `TEST_MATRIX.md`.
- Deep menu focus/navigation detail. Use the menu reference docs in `docs/`.
- Current source-of-truth ownership always lives in `FILE_MAP.md`, not in this file.
- For first-open entry points, start with `START_HERE.md`.

## Repo purpose
ADSR_V2 is an embedded Daisy sampler/performer firmware repo. The codebase is organized around a strict split between deterministic audio-thread DSP and main-thread UI, storage, and orchestration.

## Architecture mental model
- `[AUDIO]` renders voices, playback, envelopes, filtering, and final audio processing in deterministic block-based code.
- `[MAIN/UI]` owns control scanning, UI event handling, screen state, rendering, navigation, and request creation.
- `[BG]` owns SD/filesystem and other slower work such as scan/load/save/normalize/project operations.
- `[SHARED]` owns explicit handoff state used across those domains at block boundaries or queue/publish boundaries.

## Thread ownership rules
- Audio callback is DSP only: no malloc, no file I/O, no logging, no UI work.
- Controls scan hardware; UI consumes queued input events and draws.
- Worker-style background tasks must stay out of the audio path.
- Cross-thread communication should happen through the existing queue, publish, and block-boundary handoff patterns rather than ad hoc shared writes.

## Main runtime patterns

### Controls and UI
- Control tick runs on the main loop and owns hardware scanning and debounce.
- UI tick runs on the main loop, consumes queued UI input, updates screen state, and renders.
- UI should not poll hardware directly.
- Current screen ownership is split across `src/ui/`; `ui_screens.cpp` now holds only shared UI support/helpers rather than the full screen tree.

### Audio handoff
- Musical events move from main-thread code into audio through the event queue.
- Parameter changes move from UI/main-thread code into audio through the params publish/smoothing path.
- Sample and edit changes are published by non-audio code and applied by audio at safe block boundaries.
- Audio ownership is also split by concern: treat `voice_engine.cpp` plus `voice_engine_*.cpp` as one module with specialized owners.

### Background work
- UI enqueues requests.
- Worker code performs SD/filesystem and other longer-running tasks.
- UI and overlay surfaces report progress/status through shared state.

## Where to start reading
- `docs/FILE_MAP.md`
  Use for current module ownership, thread ownership, and navigation.
- `main.cpp`
  Start here for app startup, scheduler flow, and audio callback integration.
- `app_state.h`
  Start here when trying to understand shared runtime state and handoff surfaces.
- `src/ui/ui_logic.cpp`, `src/ui/ui_render.cpp`, `controls.cpp`
  Read these for control tick, UI tick, and render/update flow.
- `src/ui/ui_router.cpp`, `src/ui/ui_screen_registry.cpp`, `src/ui/`
  Read these for current router/registry flow and split screen owners.
- `ui_screens.cpp`
  Read this for the remaining shared UI helpers, not as the primary screen owner.
- `voice_engine.cpp`, `voice_engine_*.cpp`, `audio_engine.cpp`
  Read these for audio-thread playback, voice lifecycle, render, and mix behavior.
- `src/worker/ui_worker.cpp`, `src/worker/ui_worker_project.cpp`, `src/worker/ui_worker_project_manifest.cpp`
  Read these for background SD/filesystem/project work.

## Docs guide
- `FILE_MAP.md`
  Source-of-truth ownership and "where to look" map.
- `MILESTONE_STATE.md`
  Milestone/status ledger with proof and implementation state.
- `TEST_MATRIX.md`
  Validation and on-device/build test guidance.
- `PERFORM_MENU_FOCUS_REFERENCE.md`
  Perform menu structure and interaction reference.
- `RECORD_MENU_FOCUS_REFERENCE.md`
  Record menu interaction reference.
- `PRESETS_MENU_FOCUS_REFERENCE.md`
  Presets menu interaction reference.
- `HARDWARE_CONNECTIONS.md`, `HARDWARE_NAMING.md`
  Hardware assumptions and naming conventions.

## Repo guidance for analysis work
- Prefer narrow diffs.
- Preserve the existing audio-vs-main-thread boundary.
- Avoid treating transitional hotspots as monoliths; check `FILE_MAP.md` first.
- Validate with the repo-required build command before claiming completion.
