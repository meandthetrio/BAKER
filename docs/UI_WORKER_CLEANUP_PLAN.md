# UI_WORKER_CLEANUP_PLAN.md

## Purpose

This checklist defines a safe cleanup sequence for the worker-side SD / WAV / project code, centered on:

- `ui_worker.cpp`
- `ui_worker_project.cpp`

The goal is to improve navigability, ownership clarity, and troubleshooting speed without changing behavior, worker timing semantics, file I/O behavior, project compatibility, UI request flow, or audio-thread safety.

Treat this as a **boundary and responsibility cleanup**, not a rewrite.

---

## Why this area is the next cleanup target

These two files are still among the largest and most analysis-hostile files in the repo:

- `ui_worker.cpp` — request loop, SD mount, scan/load/save/normalize/loop-find, cancellation, fake work, project-restore continuation
- `ui_worker_project.cpp` — project slot/path helpers, manifest version validation, manifest upgrade chain, clamp/sanitize helpers, save/load, restore handoff, UI sync

The main readability problems are:

1. **Too many domains per file**
   - `ui_worker.cpp` mixes low-level WAV parsing, SD file state, request dispatch, long-running step logic, and request completion.
   - `ui_worker_project.cpp` mixes manifest versioning, data sanitation, UI sync, save serialization, load deserialization, and restore orchestration.

2. **Repeated “open / validate / status / fail” patterns**
   - especially in `StartLoad`, `StartLoadPath`, save/load error paths, and project manifest loading

3. **Large function bodies**
   - `UiWorker_Tick`
   - `SaveProject`
   - `LoadProject`

4. **Hidden state coordination**
   - `s_sd`
   - `app.worker`
   - `app.project`
   - `app.sd`
   - `app.engine`
   - `app.perform`

5. **The code is cleanup-ready but not yet clearly layered**
   - there are already helper functions
   - but the next step is to group helpers by responsibility and shrink the “main story” functions

---

## Cleanup strategy

Do **not** start by changing behavior or inventing new abstractions.

Use this order:

1. clarify file responsibilities
2. extract low-risk helper clusters inside the same file
3. shrink the “main story” functions
4. only then consider moving helpers into new sibling files if ownership is obvious

This should stay **extraction-first** and **behavior-preserving**.

---

## Status legend

- `[x]` done
- `[>]` next
- `[ ]` pending
- `[~]` blocked

---

## Loop rules

- Execute exactly one checklist step per iteration unless the user explicitly requests a bounded multi-step pass.
- Use the single `[>]` step as the active step.
- After a step completes, mark it `[x]` and promote the next appropriate `[ ]` step to `[>]`.
- Keep every pass narrow and reversible.
- Prefer moving existing code over rewriting working logic.
- Compile after every step if the environment allows.
- If a step exposes unclear ownership, stop and report it rather than forcing a cleanup.

---

## Step order

- `[x]` Stage 0 — freeze scope and document ownership goals
- `[x]` Stage 1 — clean up `ui_worker.cpp` helper boundaries
- `[x]` Stage 2 — shrink `UiWorker_Tick`
- `[x]` Stage 3 — unify `StartLoad` / `StartLoadPath` around one shared load-open helper
- `[x]` Stage 4 — isolate WAV / file-format helper cluster
- `[x]` Stage 5 — clean up `ui_worker_project.cpp` manifest-version section
- `[x]` Stage 6 — isolate project clamp / sanitize / sync helpers
- `[x]` Stage 7 — shrink `SaveProject`
- `[x]` Stage 8 — shrink `LoadProject`
- `[x]` Stage 9 — reevaluate whether helper clusters should move into new sibling files

---

## [x] Stage 0 — freeze scope and document ownership goals

### Goal
Make the cleanup intent explicit before changing code shape.

### Tasks
- Treat this as an extraction / grouping pass only.
- Do not change project manifest format.
- Do not change project compatibility behavior.
- Do not change worker request sequencing.
- Do not change save/load/normalize/scan semantics.
- Do not change the `s_sd` ownership model in this stage.
- Do not change thread ownership semantics.

### Done when
- The prompt explicitly says this is structural-only.
- No behavior work is mixed into the first pass.

---

## [x] Stage 1 — clean up `ui_worker.cpp` helper boundaries

### Goal
Make `ui_worker.cpp` readable by responsibility before shrinking the request loop.

### What this file currently contains
1. byte / WAV header helpers
2. path helpers
3. SD mount / cancel / finish helpers
4. scan start/step
5. load start/path/step
6. normalize start/step
7. loop-find helpers
8. save start/step
9. delete helper
10. fake-work helper
11. `UiWorker_Tick`

### Tasks
- Reorder or regroup local helpers so the file reads in a cleaner order.
- Add short section comments for helper clusters if useful.
- Keep helper names stable unless a rename is extremely local and clearly beneficial.
- Do not rewrite helper logic yet.

### Done when
- A reader can scan `ui_worker.cpp` by subsystem.
- Helper clusters are visually grouped.
- No behavior changes.

---

## [x] Stage 2 — shrink `UiWorker_Tick`

### Goal
Turn `UiWorker_Tick` into a clearer orchestrator instead of a mixed dispatcher / executor / cancellation hub.

### Problems to fix
`UiWorker_Tick` currently handles:
- pending SD-load override logic
- request dequeue / request initialization
- per-request start dispatch
- per-request step dispatch
- final status / dirty marking

### Tasks
Extract small helpers inside `ui_worker.cpp` only, such as:
- maybe-handle pending load
- maybe-start next UI request
- step active request
- finalize request state transitions

### Constraints
- Keep all helpers in `ui_worker.cpp`
- No request-type behavior changes
- Preserve exact request start/finish semantics

### Done when
- `UiWorker_Tick` reads like a high-level worker loop.
- Request-specific details move into small local helpers.

---

## [x] Stage 3 — unify `StartLoad` / `StartLoadPath` around one shared load-open helper

### Goal
Remove duplicated WAV-open / validate / initialize logic.

### Why
`StartLoad` and `StartLoadPath` share nearly the same flow:
- ensure SD mounted
- open file
- parse WAV
- validate format
- validate frame count
- seek to data
- initialize `s_sd`
- initialize browser load UI

### Tasks
- Extract one shared helper for “open WAV and initialize load state”
- Keep target-slot selection differences explicit
- Preserve all status text and failure behavior

### Constraints
- Do not change load behavior
- Do not merge things so aggressively that slot/index semantics become harder to see

### Done when
- duplicated load-open logic is materially reduced
- both start paths remain easy to understand

---

## [x] Stage 4 — isolate WAV / file-format helper cluster

### Goal
Make the binary/WAV helpers clearly separate from worker flow.

### Candidate helpers
- `ReadU16LE`
- `ReadU32LE`
- `WriteU16LE`
- `WriteU32LE`
- `ParseWavHeader`
- `WriteWavHeader`
- `IsWavName`

### Tasks
- Group these tightly inside `ui_worker.cpp`
- Optionally move them to a worker-local helper block or a new worker-local sibling file only if the ownership is obvious and the pass stays narrow

### Constraints
- No behavior changes
- Avoid premature file-splitting unless the helper cluster is cleanly isolated

### Done when
- low-level WAV helpers stop distracting from worker flow

---

## [x] Stage 5 — clean up `ui_worker_project.cpp` manifest-version section

### Goal
Make the manifest versioning / upgrade portion readable as one coherent subsystem.

### Problems to fix
The top half of `ui_worker_project.cpp` currently mixes:
- slot/path helpers
- manifest validity per version
- long upgrade chain across versions
- clamp helpers

### Tasks
- Group manifest validity and upgrade helpers together
- Add short section markers
- Keep version-upgrade chain intact and explicit
- Do not change manifest compatibility

### Done when
- a reader can quickly find:
  - version validity
  - version upgrade helpers
  - current manifest target

---

## [x] Stage 6 — isolate project clamp / sanitize / sync helpers

### Goal
Separate “sanitize and publish project data” from file I/O.

### Candidate helper groups
1. clamp helpers
   - tune / MIDI / ADSR / gain / cutoff / etc.

2. sanitize helpers
   - FX order
   - sat/eq state

3. UI sync / publish helpers
   - `PublishProjectPerformParams`
   - `SyncProjectProcessVolumeUiState`
   - `SyncProjectProcessFxOrderUiState`

### Tasks
- group these helpers by role
- keep them in-file for now
- do not change how values are clamped or published

### Done when
- project data hygiene logic is visually separate from save/load file operations

---

## [x] Stage 7 — shrink `SaveProject`

### Goal
Make `SaveProject` read as a serialization flow instead of one long mixed body.

### Candidate extraction targets
- collect layer/sample-present info
- serialize per-layer state
- serialize global project state
- write temp file and rename
- set success/failure status

### Constraints
- no manifest format changes
- no status wording changes unless truly local and harmless
- no behavior changes

### Done when
- `SaveProject` is materially shorter
- helper names make the serialization phases obvious

---

## [x] Stage 8 — shrink `LoadProject`

### Goal
Make `LoadProject` readable as:
1. open/read manifest
2. upgrade/validate manifest
3. apply sanitized state to app
4. publish params / sync UI
5. queue sample restore continuation

### Problems to fix
`LoadProject` is currently doing too much in one body:
- file open
- version-size branching
- legacy upgrade chain
- manifest validation
- app-state application
- publish/sync
- sample restore queue setup

### Candidate extraction targets
- read current-or-legacy manifest into current struct
- apply manifest globals
- apply per-layer UI state
- setup pending sample restore state

### Constraints
- preserve compatibility behavior exactly
- preserve restore sequencing exactly
- preserve project status semantics exactly

### Done when
- `LoadProject` reads like a clear staged restore flow

---

## [x] Stage 9 — reevaluate whether helper clusters should move into new sibling files

### Goal
Only after the in-file cleanup is stable, decide whether cross-file extraction is warranted.

### Possible future split directions
Only consider these after earlier stages succeed:
- `ui_worker_wav.cpp` for WAV/file-format helpers
- `ui_worker_load.cpp` for load/open/step helpers
- `ui_worker_project_manifest.cpp` for manifest validity/upgrade helpers

### What should probably stay centralized longer
- `UiWorker_Tick`
- `s_sd` state coordination
- project restore continuation glue between worker and project code

### Done when
- there is a clear case for file movement
- helper clusters have obvious ownership
- no extraction is done “just because the file is big”

---

## What success looks like

After the first several stages:

- `ui_worker.cpp` reads by helper cluster instead of historical accretion
- `UiWorker_Tick` becomes a high-level orchestrator
- duplicated load-open logic is reduced
- `ui_worker_project.cpp` becomes easier to scan by:
  - manifest compatibility
  - sanitation
  - serialization
  - restore application
- `SaveProject` and `LoadProject` become shorter without changing behavior

---

## Immediate recommendation

Start with **Stage 1 only**:

**clean up `ui_worker.cpp` helper boundaries without changing behavior**
