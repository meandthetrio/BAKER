# AGENTS.md

## Repo purpose
ADSR_V2 is an embedded Daisy firmware repo with a strict split between deterministic audio-thread DSP and main-thread UI/storage/orchestration.

## Hard architecture rules
- Audio callback is deterministic DSP only.
- No dynamic allocation in the audio path.
- Do not add blocking file I/O, SD access, logging, or heap allocation to the audio callback.
- Preserve the existing queue / shared-parameter / block-boundary handoff model.
- Main loop owns UI, storage, and worker orchestration.

## Cleanup-task rules
- Prefer narrow diffs.
- Do not change behavior unless the prompt explicitly requests behavior change.
- Preserve existing status text, screen flow, and worker flow unless the task explicitly targets them.
- Do not rewrite code when code movement is sufficient.
- Keep names stable unless renaming is the point of the task.
- Update docs when module boundaries move.

## AppState cleanup rules
- For `app_state.h` extraction or cleanup work, use `docs/AppState_Plan.md` as the source of truth for the next prompt order and scope.
- Execute AppState prompts one at a time unless the user explicitly requests bounded multi-step continuation.
- Run `make -j4` after each AppState prompt.
- Work deliberately and thoroughly on every AppState prompt. Do not rush. Favor careful inspection, conservative edits, and narrow reversible structural changes over speed.
- Keep AppState passes structural-only unless the selected prompt explicitly allows more.

## UI worker cleanup rules
- For `ui_worker.cpp` / `ui_worker_project.cpp` cleanup work, use `docs/UI_WORKER_CLEANUP_PLAN.md` as the source of truth for next prompt order, scope, and writable checklist state.
- Use `docs/UI_WORKER_CLEANUP_PROMPTS.md` as the prompt reference for the current worker-cleanup stage text.
- Execute UI worker cleanup prompts one at a time unless the user explicitly requests bounded multi-step continuation or continue-until-done execution.
- In continue-until-done mode, complete one stage at a time, update `docs/UI_WORKER_CLEANUP_PLAN.md` after each completed stage, then re-read it before selecting the next stage.
- Run `make -j4` after each UI worker cleanup prompt.
- Work deliberately and thoroughly on every UI worker cleanup prompt. Favor careful inspection, conservative edits, and narrow reversible structural changes over speed.
- Keep UI worker cleanup passes structural-only unless the selected prompt explicitly allows more.

## File-scope guardrails
- Do not modify audio-thread files during UI-only cleanup tasks.
- Do not modify ui_worker.cpp unless the task is worker-specific.
- Do not modify unrelated screens when splitting one screen/domain.

## Validation
Before finishing, run:
- `make -j4`

Always report:
- short summary
- exact files changed
- commands run
- build result
- risk notes
- docs updated yes/no

## Source of truth docs
- docs/FILE_MAP.md
- docs/DOC2_CHECKLIST.md
- docs/AppState_Plan.md
- docs/UI_WORKER_CLEANUP_PLAN.md
- docs/UI_WORKER_CLEANUP_PROMPTS.md
