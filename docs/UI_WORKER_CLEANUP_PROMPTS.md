# UI_WORKER_CLEANUP_PROMPTS.md

Use these prompts in order. Each prompt is intentionally narrow and behavior-preserving.

---

## Prompt 1 — Stage 0 / Stage 1 start: clean up `ui_worker.cpp` helper boundaries

```text
Task: Clean up ui_worker.cpp only. This is a narrow structural pass.

Goal:
Make ui_worker.cpp easier to scan by grouping its local helpers by responsibility, without changing behavior, request flow, file I/O behavior, or worker timing semantics.

Scope:
- Edit ui_worker.cpp only
- Do not change source behavior
- Do not move helpers into new files yet
- Do not change request semantics
- Do not change SD/browser behavior
- Do not change save/load/normalize/scan semantics
- Do not change project restore behavior

What the file currently mixes:
- low-level WAV/byte helpers
- path helpers
- SD mount/cancel/finish helpers
- scan helpers
- load helpers
- normalize helpers
- loop-find helpers
- save helpers
- delete helper
- fake-work helper
- UiWorker_Tick

What to do:
1. Reorder and/or regroup local helpers so the file reads in clearer responsibility clusters
2. Add short section comments if they help scanability
3. Keep helper names stable unless a rename is extremely local and clearly beneficial
4. Do not rewrite logic in this pass
5. Do not touch ui_worker_project.cpp in this pass

Deliverables:
1. Short summary
2. Exact files changed
3. Helper clusters introduced or clarified
4. Commands run
5. Build result
6. Risk notes

Acceptance criteria:
- ui_worker.cpp is easier to scan by responsibility
- helper ordering is more deliberate
- no behavior changes
- code still builds
```

---

## Prompt 2 — shrink `UiWorker_Tick`

```text
Task: Clean up ui_worker.cpp only. This is a narrow structural pass.

Goal:
Shrink UiWorker_Tick so it reads like a high-level worker orchestrator instead of a mixed dispatcher/executor/cancellation hub.

Scope:
- Edit ui_worker.cpp only
- Do not change source behavior
- Do not move anything to new files
- Do not change request ordering, request status, cancellation behavior, or dirty-marking behavior

What UiWorker_Tick currently does:
- handles pending SD load overrides
- cancels conflicting work
- dequeues and initializes requests
- starts request-specific work
- steps active requests
- finalizes done requests
- marks UI dirty on visible worker-state changes

What to do:
1. Extract small local helpers inside ui_worker.cpp only, such as:
   - maybe-handle pending load
   - maybe-start next UI request
   - step active request
   - finalize request outcome
2. Keep UiWorker_Tick as the top-level story
3. Preserve exact semantics and status transitions
4. Do not touch ui_worker_project.cpp

Deliverables:
1. Short summary
2. Exact files changed
3. New local helpers introduced
4. Commands run
5. Build result
6. Risk notes

Acceptance criteria:
- UiWorker_Tick is materially shorter and easier to read
- request semantics are unchanged
- no behavior changes
- code still builds
```

---

## Prompt 3 — unify `StartLoad` and `StartLoadPath`

```text
Task: Clean up ui_worker.cpp only. This is a narrow structural pass.

Goal:
Reduce duplication between StartLoad and StartLoadPath by extracting one shared helper for WAV-open / validate / load-state initialization.

Scope:
- Edit ui_worker.cpp only
- Do not change source behavior
- Do not move anything to new files
- Do not change target-slot semantics
- Do not change browser status text or failure behavior

Shared flow that should be consolidated carefully:
- ensure SD mounted
- open file
- parse WAV header
- validate PCM/mono/16-bit/48k format
- validate frame count
- seek to data
- initialize s_sd load state
- initialize browser load UI state

What to do:
1. Introduce one shared helper for the duplicated load-open path
2. Keep the differences between indexed-load and explicit-path-load obvious
3. Preserve default status/error handling exactly
4. Keep the pass narrow and reversible

Deliverables:
1. Short summary
2. Exact files changed
3. New shared helper introduced
4. What duplication was removed
5. Commands run
6. Build result
7. Risk notes

Acceptance criteria:
- duplicated load-open logic is materially reduced
- StartLoad and StartLoadPath remain easy to understand
- behavior is unchanged
- code still builds
```

---

## Prompt 4 — isolate WAV / file-format helper cluster

```text
Task: Clean up ui_worker.cpp only. This is a narrow structural pass.

Goal:
Make the low-level WAV/file-format helper block clearly separate from worker control flow.

Scope:
- Edit ui_worker.cpp only
- Do not change source behavior
- Do not move helpers to new files unless the separation is extremely obvious and low-risk
- Prefer an in-file cleanup in this pass

Target helper cluster:
- ReadU16LE
- ReadU32LE
- WriteU16LE
- WriteU32LE
- ParseWavHeader
- WriteWavHeader
- IsWavName

What to do:
1. Group the low-level WAV helpers tightly
2. Add a short section marker if useful
3. Keep helper names stable
4. Do not alter file-format logic
5. Do not broaden into other worker cleanup

Deliverables:
1. Short summary
2. Exact files changed
3. WAV/helper organization changes made
4. Commands run
5. Build result
6. Risk notes

Acceptance criteria:
- low-level WAV helpers are visually isolated from higher-level worker flow
- behavior is unchanged
- code still builds
```

---

## Prompt 5 — clean up `ui_worker_project.cpp` manifest-version section

```text
Task: Clean up ui_worker_project.cpp only. This is a narrow structural pass.

Goal:
Make the manifest compatibility section easier to scan by grouping version-validity and upgrade helpers into a clearly readable block.

Scope:
- Edit ui_worker_project.cpp only
- Do not change source behavior
- Do not change manifest compatibility
- Do not change project version handling
- Do not change SaveProject or LoadProject logic in this pass

What the top of the file currently mixes:
- slot/path/status helpers
- ProjectManifestValid overloads
- ProjectManifestUpgrade overloads
- clamp helpers

What to do:
1. Group the manifest-validity helpers together
2. Group the manifest-upgrade helpers together
3. Add short section comments if they help
4. Keep the version upgrade chain explicit
5. Do not change manifest semantics

Deliverables:
1. Short summary
2. Exact files changed
3. Manifest helper sections clarified
4. Commands run
5. Build result
6. Risk notes

Acceptance criteria:
- version validity and upgrade helpers are easier to locate
- file scanability improves
- no behavior changes
- code still builds
```

---

## Prompt 6 — isolate clamp / sanitize / sync helpers in `ui_worker_project.cpp`

```text
Task: Clean up ui_worker_project.cpp only. This is a narrow structural pass.

Goal:
Separate project clamp/sanitize/UI-sync helpers from manifest compatibility and file I/O flow.

Scope:
- Edit ui_worker_project.cpp only
- Do not change source behavior
- Do not change actual clamp ranges
- Do not change PublishTargets behavior
- Do not change SaveProject or LoadProject semantics

Target helper groups:
1. clamp helpers
2. sanitize helpers
3. UI sync / publish helpers:
   - PublishProjectPerformParams
   - SyncProjectProcessVolumeUiState
   - SyncProjectProcessFxOrderUiState

What to do:
1. Group the helpers by role
2. Add short section markers if useful
3. Keep helper names stable
4. Do not rewrite logic in this pass

Deliverables:
1. Short summary
2. Exact files changed
3. Helper groups clarified
4. Commands run
5. Build result
6. Risk notes

Acceptance criteria:
- clamp/sanitize/sync helpers are visually separate from manifest compatibility and file I/O
- no behavior changes
- code still builds
```

---

## Prompt 7 — shrink `SaveProject`

```text
Task: Clean up ui_worker_project.cpp only. This is a focused structural pass.

Goal:
Shrink SaveProject so it reads like a staged serialization flow instead of one long mixed function body.

Scope:
- Edit ui_worker_project.cpp only
- Do not change source behavior
- Do not change project manifest format
- Do not change save status semantics
- Do not change temp-file / rename behavior
- Do not change which fields are serialized

What SaveProject currently does:
- resolve project slot and status
- ensure SD mounted
- collect sample-present info
- collect per-layer edit/engine/perform state
- collect global process/mod/macro state
- clamp/sanitize manifest values
- write temp file
- rename temp file into final project file
- set status

What to do:
1. Extract small local helpers inside ui_worker_project.cpp only, such as:
   - collect layer sample/edit state
   - collect global/process state
   - write temp project manifest
2. Keep SaveProject as the top-level serialization story
3. Preserve exact manifest contents and save flow
4. Keep the pass narrow

Deliverables:
1. Short summary
2. Exact files changed
3. New local helpers introduced
4. What parts of SaveProject were shortened
5. Commands run
6. Build result
7. Risk notes

Acceptance criteria:
- SaveProject is materially shorter and easier to scan
- serialized data and file flow are unchanged
- no behavior changes
- code still builds
```

---

## Prompt 8 — shrink `LoadProject`

```text
Task: Clean up ui_worker_project.cpp only. This is a focused structural pass.

Goal:
Shrink LoadProject so it reads as a clear staged restore flow:
1. open/read manifest
2. upgrade/validate manifest
3. apply sanitized state to app
4. publish/sync UI-visible params
5. queue sample restore continuation

Scope:
- Edit ui_worker_project.cpp only
- Do not change source behavior
- Do not change manifest compatibility
- Do not change restore sequencing
- Do not change project status semantics
- Do not change the pending sample-restore handoff

What LoadProject currently mixes:
- file open and size branching
- legacy manifest upgrade chain
- validation
- app-state assignment
- params publish
- UI sync
- project restore queue setup

What to do:
1. Extract small local helpers inside ui_worker_project.cpp only, such as:
   - read current-or-legacy manifest into current struct
   - apply manifest globals
   - apply per-layer perform/engine state
   - setup pending restore state
2. Keep LoadProject as the top-level restore story
3. Preserve exact compatibility behavior and restore ordering
4. Keep the pass narrow

Deliverables:
1. Short summary
2. Exact files changed
3. New local helpers introduced
4. What parts of LoadProject were shortened
5. Commands run
6. Build result
7. Risk notes

Acceptance criteria:
- LoadProject is materially shorter and easier to scan
- compatibility and restore behavior are unchanged
- no behavior changes
- code still builds
```

---

## Prompt 9 — reevaluate whether helper clusters should move into new sibling files

```text
Task: Evaluate the worker cleanup state and only perform a low-risk extraction if ownership is now obvious.

Goal:
After the earlier in-file cleanup passes, decide whether one helper cluster should move into a new sibling file without changing behavior.

Scope:
- This is optional and should only proceed if the earlier passes left a very obvious extraction boundary
- Prefer moving one helper cluster only
- Do not change source behavior
- Do not move more than one cluster in this pass
- Do not move central coordination logic just to make file sizes smaller

Best candidate directions if the boundary is truly clear:
- ui_worker_wav.cpp for low-level WAV helpers
- ui_worker_project_manifest.cpp for manifest validity/upgrade helpers

Do NOT move yet unless the ownership is obvious:
- UiWorker_Tick
- s_sd coordination logic
- project restore continuation glue
- SaveProject / LoadProject themselves

What to do:
1. Inspect the cleaned files after prior passes
2. If one helper cluster now has a clear, self-contained boundary, move only that cluster
3. Update build references if needed
4. If no boundary is obvious, do not force extraction; report that the code is cleaner but should stay in-file for now

Deliverables:
1. Short summary
2. Exact files changed
3. Whether a file split happened
4. Why that boundary was chosen or deferred
5. Commands run
6. Build result
7. Risk notes

Acceptance criteria:
- Any file move is obviously ownership-driven, not size-driven
- behavior is unchanged
- code still builds
```

---

## Recommended run order

Run them in this order:

1. Prompt 1
2. Prompt 2
3. Prompt 3
4. Prompt 4
5. Prompt 5
6. Prompt 6
7. Prompt 7
8. Prompt 8
9. Prompt 9 only if the earlier passes leave a very obvious helper-cluster boundary

---

## What success should look like

After these passes:

- `ui_worker.cpp` reads by helper cluster and has a much smaller `UiWorker_Tick`
- `ui_worker_project.cpp` reads by:
  - manifest compatibility
  - sanitation/sync
  - save serialization
  - load/restore flow
- `SaveProject` and `LoadProject` are easier to inspect without changing behavior
- any later file split is driven by ownership clarity, not just line count
