# START_HERE (ADSR_V2)

Use this as the first-open map for the current repo layout.

## Layout policy (this repo)

**Canonical choice (today):** the voice engine and other root-listed runtime translation units stay **at repo root** and are intentionally built from the root `Makefile` (`CPP_SOURCES` + `-I.`). A future move of the voice cluster under e.g. `src/voice/` would be a **separate, bounded** migration (one coherent cluster, update `Makefile` and includes only)—not implied by this doc until that work lands.

- **Root** holds the Daisy app glue (`main.cpp`), global `app_state_*` headers, `voice_engine*.cpp`, `audio_engine.*`, and other core runtime that predates deeper subfolders.
- **`src/ui/`** owns screens, router, drawing, and layout.
- **`src/worker/`** owns SD, project load/save, and manifest upgrade (`ui_worker_sd_scan.cpp` is SD WAV directory scan only).
- **`src/storage/`** owns SD browser/sample pool types shared with UI and worker.
- **`Effects/`** is third-party or large DSP leaves (e.g. reverb).
- Prefer adding new feature code under the closest subtree above instead of growing `main.cpp` or random new root files.

## Durable Entry Points

- App boot and scheduler: `main.cpp`
- Audio coordinator: `audio_engine.cpp`
- Voice/render split: `voice_engine.cpp`, `voice_engine_events.cpp`, `voice_engine_playback.cpp`, `voice_engine_render.cpp`, `voice_engine_voice_lifecycle.cpp`, `voice_engine_emphasis.cpp`
- UI router and screen registry: `src/ui/ui_router.cpp`, `src/ui/ui_screen_registry.cpp`
- Shared UI helpers: `ui_screens.cpp`, `src/ui/ui_draw_*.cpp`, `src/ui/ui_layout.cpp`
- Screen owners: `src/ui/ui_screen_*.cpp`
- Worker orchestration and project persistence: `src/worker/ui_worker.cpp`, `src/worker/ui_worker_sd_scan.cpp`, `src/worker/ui_worker_project.cpp`, `src/worker/ui_worker_project_manifest.cpp`, `src/worker/ui_worker_wav.cpp`
- Shared runtime state: `app_state.h`, `app_state_*.h`

## Best Supporting Docs

- Ownership and where-to-look map: `docs/FILE_MAP.md`
- Architecture guide for operators/agents: `docs/README_GPT.md`
- Validation flow: `docs/TEST_MATRIX.md`
- Milestone and proof history: `docs/MILESTONE_STATE.md`
