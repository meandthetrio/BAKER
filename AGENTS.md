# AGENTS.md

## Current task
AppState final cleanup loop only. Focus exclusively on the iterative dependency-narrowing workflow driven by `docs/AppStateFinalCleanupPlan.md`.

## Core goal
The goal is not to make `app_state.h` smaller.

The goal is to make this true:
- most subsystem code no longer takes `AppState&` or `AppState*`
- most leaf files no longer include `app_state.h`

Treat `AppState` as the composition root, not a normal subsystem parameter type.
Top-level orchestration boundaries may still use it. Leaf subsystem files should prefer specific state buckets, narrow context structs, and narrower helper signatures.

## Success looks like
- Most UI files no longer include `app_state.h`
- Most UI code no longer accesses `ctx.app`
- `AppSharedState` is no longer one flat mixed bag
- Restore scratch no longer lives in `AppProjectState`
- Most worker helpers no longer take `AppState&`
- `AppState` is mostly restricted to orchestration boundaries

## Stable Exceptions
- Acceptable whole-app boundaries are top-level orchestration files such as `main.cpp`, `src/ui/ui_logic.cpp`, `src/ui/ui_render.cpp`, `src/worker/ui_worker.cpp`, and `src/worker/ui_worker_project.cpp`.
- Treat smaller wrappers or helper-level functions that still take `AppState&` outside those files as temporary compatibility seams, not as stable exceptions.

## Hard rules
- Audio callback is deterministic DSP only.
- No dynamic allocation in the audio path.
- Do not add blocking file I/O, SD access, logging, or heap allocation to the audio callback.
- Preserve the existing queue / shared-parameter / block-boundary handoff model.
- Main loop owns UI, storage, and worker orchestration.
- This loop is dependency narrowing only.
- Do not change behavior.
- Do not change threading ownership.
- Do not change save/load semantics.
- Do not rewrite working logic just to "clean it up".
- Prefer changing function signatures, context shape, field ownership, and includes over changing logic.
- Prefer narrow diffs.
- If a helper needs several state buckets temporarily, that is allowed. Do not pass the whole `AppState` when narrower ownership is viable.
- Do not let this turn into another field-shuffling pass, rename-only pass, comment-only pass, or "split header but still pass AppState everywhere" pass.
- If a step feels ambiguous or risky, think first, choose the smallest safe slice that honestly advances the current step, and continue. Do not stop the loop merely because the step needs careful scoping.

## AppState loop rules
- Use `docs/AppStateFinalCleanupPlan.md` as the source of truth for prompt order, scope, goals, and success criteria.
- Treat each explicit `Step X.Y` section in that document as one checklist step.
- Treat surrounding stage text as scope and intent guidance, not as permission to combine multiple steps.
- Unless the user explicitly requests bounded multi-step continuation or continue-until-done execution, execute exactly one `Step X.Y` per run.
- The default active step is the first `Step X.Y` in plan order that is not marked `[done]` in `docs/AppStateFinalCleanupPlan.md`.
- Record progress directly in `docs/AppStateFinalCleanupPlan.md` by appending a trailing status marker to each `Step X.Y` heading: `[in progress]` or `[done]`.
- In continue-until-done mode, complete one `Step X.Y` at a time, update `docs/AppStateFinalCleanupPlan.md`, then re-read it before selecting the next step.
- Follow the plan's documented pass order unless the user explicitly overrides it.
- Documentation-only steps must stay documentation-only. Do not change code during those steps.
- Code steps must stay narrow. Do not skip ahead to later conceptual cleanup just because touched files are nearby.
- When confronted with ambiguity or a risk boundary, stop and think through the correct, smart way to proceed before editing further. Prefer pausing or asking over guessing.
- If a step is too large, complete the smallest meaningful slice that still advances the named step honestly, then record the state conservatively.
- Update any docs named by the selected step when the code or ownership story makes them stale.
- Work deliberately and thoroughly. Favor careful inspection, conservative edits, and narrow reversible structural changes over speed.

## Validation
- Run `make -j4` after each AppState cleanup prompt.
- If validation fails, stay on the same step until the repo is clean again.
- Before finishing, run `make -j4`.
- No behavior changes are intended in any pass.

## Always report
- short summary
- exact files changed
- commands run
- build result
- no behavior changes
- remaining `AppState` usage in touched files
- risk notes
- docs updated yes/no
