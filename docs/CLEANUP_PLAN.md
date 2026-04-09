# CLEANUP_PLAN.md

## Purpose

This document defines the cleanup sequence for ADSR_V2 so refactors stay narrow, reversible, and behavior-safe.

The goal is to improve navigability, file boundaries, and maintainability without changing the repo’s threading contract, runtime behavior, or UI flow unless a specific task explicitly calls for it.

---

## Cleanup principles

- Prefer small, commit-sized tasks.
- Preserve behavior unless a task explicitly allows behavior change.
- Preserve the audio-thread vs main-thread ownership model.
- Prefer moving existing code over rewriting working code.
- Compile after each cleanup task.
- Update docs whenever module boundaries move.

---

## Stage 0 — Baseline discipline

### Goals
- Establish a stable cleanup workflow before structural changes begin.

### Tasks
- Ensure `AGENTS.md` exists and reflects repo rules.
- Ensure `docs/FILE_MAP.md` is current enough to serve as a module map.
- Create checkpoint commit before each cleanup pass.
- Require each cleanup task to report:
  - short summary
  - exact files changed
  - commands run
  - build result
  - risk notes
  - docs updated yes/no

### Done criteria
- Codex can complete a narrow cleanup task with a small diff and successful build.
- Cleanup prompts and output format are stable and repeatable.

---

## Stage 1 — Repo hygiene and packaging cleanup

### Goals
- Remove clutter that makes the repo harder to share, diff, and reason about.

### Tasks
- Confirm build artifacts are ignored and not treated as source.
- Remove accidental archive clutter from shared repo snapshots:
  - `build/`
  - `__MACOSX/`
  - `.DS_Store`
  - other generated/export-only files
- Review `.gitignore` for correctness.
- Ensure shared snapshots are source-focused and do not include generated binaries unless intentionally needed.

### Done criteria
- Shared repo snapshots no longer include packaging junk.
- Build outputs are clearly treated as generated files.
- Repo root is less noisy.

---

## Stage 2 — Split `ui_screens.cpp` by screen/domain

### Goals
- Break up the largest UI monolith without changing behavior.

### Rationale
`ui_screens.cpp` is currently too large and carries too many responsibilities in one file. This makes narrow edits risky and slows navigation.

### Tasks
Split screen implementations by domain while preserving router behavior and existing screen flow.

Suggested target files:
- `ui_screen_settings.cpp`
- `ui_screen_perform.cpp`
- `ui_screen_record.cpp`
- `ui_screen_project.cpp`
- `ui_screen_browser.cpp`
- `ui_screen_status.cpp`

Keep shared declarations and common helpers only where they genuinely belong.

### Rules for this stage
- Do not redesign navigation.
- Do not rename public UI concepts unless required.
- Do not alter status text or focus behavior unless explicitly requested.
- Keep the existing router behavior intact.

### Done criteria
- `ui_screens.cpp` is significantly smaller or reduced to router/shared glue.
- Screen/domain code is split into clear files.
- Build passes.
- On-device behavior is unchanged.

---

## Stage 3 — Split `ui_worker.cpp` by operation type

### Goals
- Separate storage/background responsibilities into clearer modules.

### Rationale
`ui_worker.cpp` appears to contain multiple different job families:
- project save/load
- sample load/apply
- SD browsing/scan
- shared file handling
- worker status updates

These should be easier to reason about independently.

### Tasks
Split worker implementation into operation-focused files while preserving the existing worker loop and request model.

Suggested target files:
- `ui_worker_core.cpp`
- `ui_worker_project.cpp`
- `ui_worker_sample_load.cpp`
- `ui_worker_sd_scan.cpp`
- `ui_worker_status.cpp`

### Rules for this stage
- Do not change worker sequencing unless required to preserve behavior.
- Do not change request semantics.
- Do not change user-visible project/sample status text unless explicitly requested.
- Preserve current ownership of SD/file operations.

### Done criteria
- Worker code is grouped by responsibility.
- Existing request flow remains intact.
- Build passes.
- No project/sample behavior regression is introduced.

---

## Stage 4 — Group `AppState` into clearer sub-structures

### Goals
- Reduce “god object” pressure while preserving existing behavior and field meaning.

### Rationale
A large shared state object becomes hard to navigate and easy to misuse when unrelated concerns live side by side.

### Tasks
Group related fields into nested structs such as:
- `UiState`
- `ProjectState`
- `WorkerState`
- `OverlayState`
- `DiagnosticsState`
- `PerformState`

Do this gradually and only where grouping improves readability without causing churn.

### Rules for this stage
- Preserve field semantics.
- Avoid broad renaming unless necessary.
- Prefer mechanical grouping over behavioral redesign.
- Do not change ownership rules between threads.

### Done criteria
- `AppState` is easier to scan by domain.
- Related fields are visibly grouped.
- Build passes.
- Behavior is unchanged.

---

## Stage 5 — Reduce orchestration weight in `main.cpp`

### Goals
- Leave `main.cpp` focused on initialization and top-level scheduling.

### Rationale
`main.cpp` should be easy to read as the system entry point, not the place where large amounts of business logic accumulate.

### Tasks
Extract orchestration helpers as needed, such as:
- scheduler tick helpers
- MIDI routing helpers
- initialization helpers
- callback bridge helpers

### Rules for this stage
- Keep control flow obvious.
- Preserve initialization order.
- Preserve timing behavior.
- Do not move logic just for the sake of movement.

### Done criteria
- `main.cpp` reads as entry-point orchestration.
- Lower-level details live in more specific modules.
- Build passes.
- Timing and behavior remain unchanged.

---

## Stage 6 — Separate UI workflow actions from screen rendering/input logic

### Goals
- Reduce coupling between UI event handlers and operational workflows.

### Rationale
Some actions triggered from screens are not purely “UI”; they are app workflows such as project load/save and sample operations.

### Tasks
Where useful, introduce thin action/helper layers such as:
- `project_actions.*`
- `sample_actions.*`
- `ui_actions.*`

Screen code should invoke actions rather than directly own all workflow logic.

### Rules for this stage
- Do not add abstraction unless it clearly improves separation.
- Keep action layers thin.
- Preserve current user-visible behavior.

### Done criteria
- Screen files become more declarative and easier to read.
- Workflow logic is easier to find and test.
- Build passes.

---

## Stage 7 — Optional physical directory cleanup

### Goals
- Make the repo layout match the conceptual architecture.

### Rationale
The repo appears conceptually organized better than it is physically organized. Folder structure should help navigation.

### Tasks
Consider moving files into subsystem folders such as:
- `src/app/`
- `src/audio/`
- `src/ui/`
- `src/storage/`
- `src/common/`

Do this only after the main monolith splits are complete.

### Rules for this stage
- Avoid combining file moves with behavioral edits.
- Update include paths carefully.
- Update `docs/FILE_MAP.md`.

### Done criteria
- File placement reflects subsystem boundaries.
- Include structure remains clean.
- Build passes.

---

## Suggested task order

Recommended order of execution:

1. Repo hygiene
2. Split `ui_screens.cpp`
3. Split `ui_worker.cpp`
4. Group `AppState`
5. Reduce `main.cpp`
6. Introduce thin action layers where helpful
7. Move files into subsystem folders

---

## Per-task checklist

For every cleanup task:

- Make a checkpoint commit first.
- Keep the task narrowly scoped.
- Build with `make -j4`.
- Report:
  - short summary
  - exact files changed
  - commands run
  - build result
  - risk notes
  - docs updated yes/no
- Update `docs/FILE_MAP.md` if boundaries moved.

---

## Explicit non-goals

The cleanup plan does **not** aim to:

- redesign the firmware architecture
- change the audio-thread contract
- change UI behavior unless explicitly requested
- rewrite stable code for style reasons alone
- introduce new abstractions without a concrete payoff