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
- docs/CLEANUP_PLAN.md
- docs/CLEANUP_RULES.md