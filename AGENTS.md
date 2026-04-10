# AGENTS.md

## Current task
Cleanup-loop preparation and execution only. Focus exclusively on the repo cleanup workflow driven by `docs/ADSR_V2_Cleanup_Concepts_Renumbered.md`.

## Hard rules
- Audio callback is deterministic DSP only.
- No dynamic allocation in the audio path.
- Do not add blocking file I/O, SD access, logging, or heap allocation to the audio callback.
- Preserve the existing queue / shared-parameter / block-boundary handoff model.
- Main loop owns UI, storage, and worker orchestration.
- Prefer narrow diffs.
- Do not change behavior, thread ownership, runtime model, project flow, or UI behavior unless the selected checklist step explicitly requires a structural change.
- Treat cleanup steps as boundary/ownership/layout work first, not as invitations for opportunistic rewrites.
- Prefer moving existing code and ownership surfaces over rewriting logic.

## Cleanup checklist loop rules
- Use `docs/ADSR_V2_Cleanup_Concepts_Renumbered.md` as the source of truth for prompt order, scope, and writable checklist state.
- Treat each top-level numbered item in that document as one checklist step. Treat the numbered substeps under each concept as scope guidance for that single step.
- Unless the user explicitly requests bounded multi-step continuation or continue-until-done execution, execute exactly one top-level checklist step per run.
- The default active step is the lowest-numbered top-level item that is not marked done in `docs/ADSR_V2_Cleanup_Concepts_Renumbered.md`.
- Record checklist progress in `docs/ADSR_V2_Cleanup_Concepts_Renumbered.md` by appending a trailing status marker to the top-level step heading: `[in progress]` or `[done]`.
- In continue-until-done mode, complete one top-level checklist step at a time, update `docs/ADSR_V2_Cleanup_Concepts_Renumbered.md` after each completed step, then re-read it before selecting the next step.
- Run `make -j4` after each cleanup prompt.
- If cleanliness verification does not pass after a prompt is executed, do another pass on that same step until cleanliness is approved.
- When confronted with an ambiguity/risk boundary, stop and think through the correct, smart way to proceed before editing further. Prefer pausing or asking over guessing.
- Update any docs named by the selected checklist step when the code change makes them stale.
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
