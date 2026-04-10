# START_HERE (ADSR_V2)

Use this as the first-open map for the current repo layout.

## Durable Entry Points

- App boot and scheduler: `main.cpp`
- Audio coordinator: `audio_engine.cpp`
- Voice/render split: `voice_engine.cpp`, `voice_engine_events.cpp`, `voice_engine_playback.cpp`, `voice_engine_render.cpp`, `voice_engine_voice_lifecycle.cpp`, `voice_engine_emphasis.cpp`
- UI router and screen registry: `src/ui/ui_router.cpp`, `src/ui/ui_screen_registry.cpp`
- Shared UI helpers: `ui_screens.cpp`, `src/ui/ui_draw_*.cpp`, `src/ui/ui_layout.cpp`
- Screen owners: `src/ui/ui_screen_*.cpp`
- Worker orchestration and project persistence: `src/worker/ui_worker.cpp`, `src/worker/ui_worker_project.cpp`, `src/worker/ui_worker_project_manifest.cpp`, `src/worker/ui_worker_wav.cpp`
- Shared runtime state: `app_state.h`, `app_state_*.h`

## Best Supporting Docs

- Ownership and where-to-look map: `docs/FILE_MAP.md`
- Architecture guide for operators/agents: `docs/README_GPT.md`
- Validation flow: `docs/TEST_MATRIX.md`
- Milestone and proof history: `docs/MILESTONE_STATE.md`
