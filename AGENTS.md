# AGENTS.md

## Current task
AppState header split only. Focus exclusively on the `app_state.h` extraction workflow driven by `docs/APP_STATE_HEADER_SPLIT_CHECKLIST.md`.

## Hard rules
- Audio callback is deterministic DSP only.
- No dynamic allocation in the audio path.
- Do not add blocking file I/O, SD access, logging, or heap allocation to the audio callback.
- Preserve the existing queue / shared-parameter / block-boundary handoff model.
- Main loop owns UI, storage, and worker orchestration.
- Prefer narrow diffs.
- Do not change behavior, thread ownership, runtime model, project flow, or UI behavior.
- Keep this pass extraction-only unless the selected checklist step explicitly allows more.
- Prefer moving existing code and fields over rewriting logic.

## AppState checklist loop rules
- Use `docs/APP_STATE_HEADER_SPLIT_CHECKLIST.md` as the source of truth for next prompt order, scope, and writable checklist state.
- Execute one checklist step at a time unless the user explicitly requests bounded multi-step continuation or continue-until-done execution.
- In continue-until-done mode, complete one checklist step at a time, update `docs/APP_STATE_HEADER_SPLIT_CHECKLIST.md` after each completed step, then re-read it before selecting the next step.
- Run `make -j4` after each AppState prompt.
- If cleanliness verification does not pass after a prompt is executed, do another pass on that same prompt until cleanliness is approved.
- Work deliberately and thoroughly. Favor careful inspection, conservative edits, and narrow reversible structural changes over speed.

## Validation
- Before finishing, run `make -j4`.

## Always report
- short summary
- exact files changed
- commands run
- build result
- risk notes
- docs updated yes/no
