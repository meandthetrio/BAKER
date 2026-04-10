# APP_STATE_HEADER_SPLIT_CHECKLIST.md

## Purpose

This checklist defines a narrow, extraction-only cleanup pass to break `app_state.h` into domain state headers so the repo is easier for humans, ChatGPT, and Codex to read and troubleshoot.

The goal is to improve ownership clarity and bug-tracing without changing runtime behavior, thread ownership, project flow, or UI behavior.

---

## Scope rules

- This pass is extraction-only.
- Do not change behavior.
- Do not change thread ownership.
- Do not redesign the runtime model.
- Do not rename fields unless required for compile hygiene.
- Prefer moving existing fields into better ownership containers over rewriting logic.
- Build after every step.
- If a verification pass is not clean, do another pass on that same step before moving on.

---

## Success criteria

At the end of this checklist:

- `app_state.h` is small and mostly structural.
- Domain state is grouped by ownership and reason-to-change.
- Shared / cross-thread state is clearly isolated.
- Diagnostics are clearly separated from functional state.
- Project / worker / recording flows are easier to trace.
- A reader no longer has to scan one giant mixed header to understand state ownership.

---

## Status legend

- `[x]` done
- `[>]` next
- `[ ]` pending

---

## Step order

- `[x]` Step 0 — Freeze the split rules
- `[x]` Step 1 — Inventory all AppState fields
- `[x]` Step 2 — Mark dangerous clusters before splitting
- `[x]` Step 3 — Define target domain headers
- `[x]` Step 4 — Create empty domain header shells
- `[x]` Step 5 — Convert app_state.h into an aggregator shape
- `[x]` Step 6 — Move diagnostics first
- `[x]` Step 7 — Move pure UI state
- `[x]` Step 8 — Move project state as a coherent unit
- `[x]` Step 9 — Move recording state as a coherent unit
- `[x]` Step 10 — Move engine/editor state
- `[x]` Step 11 — Move worker coordination state carefully
- `[x]` Step 12 — Move shared / cross-thread handoff fields last
- `[x]` Step 13 — Clean includes and reduce header weight
- `[x]` Step 14 — Re-comment the split by ownership
- `[x]` Step 15 — Make app_state.h intentionally boring

---

## Step 0 — Freeze the split rules

### Goal
Establish the cleanup rules before code motion begins.

### Tasks
- Add this checklist to `docs/`.
- Treat this pass as extraction-only.
- Use ownership-based grouping only:
  - UI
  - project
  - worker
  - recording
  - engine/editor
  - diagnostics
  - shared / cross-thread
- Do not begin field moves until the target split is defined.

### Done when
- The cleanup rules exist in-repo.
- The split categories are agreed and written down.
- The pass is explicitly constrained to no behavior change.

### Verification
- Confirm the checklist is in the repo.
- Confirm the split categories are written exactly once and are unambiguous.
- Confirm there are no code changes yet beyond doc/setup changes.

**If verification is not approved for cleanliness, do another pass on Step 0 before moving on.**

---

## Step 1 — Inventory all AppState fields

### Goal
Create a clean ownership inventory for every field currently in `app_state.h`.

### Tasks
- Read through `app_state.h` top to bottom.
- For each field, assign:
  - subsystem
  - primary owner
  - likely write locations
  - likely read locations
  - whether it is UI-only, worker-related, project-related, recording-related, diagnostics, engine/editor, or shared/cross-thread
- Mark fields that look ambiguous.

### Done when
- Every `AppState` field has an ownership label.
- Ambiguous fields are explicitly marked instead of guessed.
- The inventory can be used to decide header placement.

### Verification
- Spot check random fields from different areas and confirm each has an ownership label.
- Confirm there are no unlabeled `AppState` fields left.
- Confirm ambiguous fields are called out rather than silently mis-grouped.

**If verification is not approved for cleanliness, do another pass on Step 1 before moving on.**

---

## Step 2 — Mark dangerous clusters before splitting

### Goal
Identify the field groups that must move as coherent units or be handled carefully.

### Tasks
- Mark audio-visible / shared handoff fields.
- Mark project restore / project status / project request chain fields.
- Mark worker coordination fields.
- Mark recording lifecycle fields.
- Mark diagnostics / counters / overlay state.
- Mark engine/editor state that is app-owned but not audio-owned.

### Done when
- The dangerous clusters are explicitly grouped before any extraction begins.
- There is a clear note on which clusters must move together.

### Verification
- Confirm the shared/handoff fields are explicitly marked as sensitive.
- Confirm project load/save related fields are treated as a chain, not scattered.
- Confirm recording state is recognized as its own lifecycle group.

**If verification is not approved for cleanliness, do another pass on Step 2 before moving on.**

---

## Step 3 — Define target domain headers

### Goal
Create the intended destination structure before moving fields.

### Tasks
Create header targets for:
- `app_state_ui.h`
- `app_state_project.h`
- `app_state_worker.h`
- `app_state_recording.h`
- `app_state_engine.h`
- `app_state_diagnostics.h`
- `app_state_shared.h`

Each header should have a short ownership comment describing what belongs there.

### Done when
- The destination headers are decided.
- Each target header has a crisp ownership definition.
- There is no overlap in intent between headers.

### Verification
- Confirm each header has a distinct role.
- Confirm there is no “misc” bucket.
- Confirm `shared` is reserved for sensitive cross-thread or handoff state.

**If verification is not approved for cleanliness, do another pass on Step 3 before moving on.**

---

## Step 4 — Create empty domain header shells

### Goal
Create the header structure first, before moving real fields.

### Tasks
- Add each new domain header.
- Define one struct per header:
  - `AppUiState`
  - `AppProjectState`
  - `AppWorkerState`
  - `AppRecordingState`
  - `AppEngineState`
  - `AppDiagnosticsState`
  - `AppSharedState`
- Add ownership comments at the top of each struct.
- Keep the structs empty or nearly empty at first.

### Done when
- The domain headers exist.
- Each header has a minimal struct and ownership comment.
- No risky field motion has happened yet.

### Verification
- Confirm each new header is lightweight and readable.
- Confirm the ownership comments are clear.
- Confirm there is no accidental logic change in this step.

**If verification is not approved for cleanliness, do another pass on Step 4 before moving on.**

---

## Step 5 — Convert app_state.h into an aggregator shape

### Goal
Make `app_state.h` the top-level composition point instead of the giant storage dump.

### Tasks
- Include the new domain headers in `app_state.h`.
- Change `AppState` so it contains named sub-struct members:
  - `ui`
  - `project`
  - `worker`
  - `recording`
  - `engine`
  - `diag`
  - `shared`
- Keep behavior unchanged.

### Done when
- `AppState` has the new domain member structure.
- `app_state.h` clearly reads like an aggregator.
- The repo still builds or is close enough for the next field-move step to complete compile repair.

### Verification
- Confirm `app_state.h` now reads structurally instead of as one giant mixed list.
- Confirm the domain names are clear and not overloaded.
- Confirm no field ownership was silently changed.

**If verification is not approved for cleanliness, do another pass on Step 5 before moving on.**

---

## Step 6 — Move diagnostics first

### Goal
Move the safest, lowest-risk fields first to prove the pattern.

### Tasks
- Move counters, debug values, overlay instrumentation, and non-functional diagnostics into `AppDiagnosticsState`.
- Update call sites from flat access to `app.diag.*`.
- Build after the move.

### Done when
- Diagnostics are no longer stored flat in `AppState`.
- Diagnostic access uses `app.diag.*`.
- Build passes.

### Verification
- Confirm only diagnostics moved in this step.
- Confirm no functional state was mixed into `diag`.
- Confirm `make -j4` passes.

**If verification is not approved for cleanliness, do another pass on Step 6 before moving on.**

---

## Step 7 — Move pure UI state

### Goal
Isolate screen/router/focus/editor navigation state.

### Tasks
- Move clearly UI-only fields into `AppUiState`.
- Update call sites to `app.ui.*`.
- Avoid moving fields that participate in project, worker, or shared ownership unless they are truly UI-only.
- Build after the move.

### Done when
- UI-only state is grouped under `app.ui`.
- `app_state.h` is visibly smaller.
- Build passes.

### Verification
- Confirm moved fields are truly UI-only.
- Confirm no worker/project/shared coordination fields were incorrectly swept into `ui`.
- Confirm the UI state header reads like screen/editor/navigation state only.

**If verification is not approved for cleanliness, do another pass on Step 7 before moving on.**

---

## Step 8 — Move project state as a coherent unit

### Goal
Keep project save/load flow traceable in one place.

### Tasks
- Move project slot selection, project request state, project status/result state, restore-related app-owned state, and related project coordination fields into `AppProjectState`.
- Update call sites to `app.project.*`.
- Build after the move.

### Done when
- Project-related app state is grouped under `app.project`.
- Save/load flow is easier to read from one ownership area.
- Build passes.

### Verification
- Confirm the project slot, request, and status chain is not scattered across multiple headers.
- Confirm project state was not split arbitrarily between `project` and `worker`.
- Confirm `make -j4` passes.

**If verification is not approved for cleanliness, do another pass on Step 8 before moving on.**

---

## Step 9 — Move recording state as a coherent unit

### Goal
Give recording its own readable ownership boundary.

### Tasks
- Move app-owned recording lifecycle fields into `AppRecordingState`.
- Update call sites to `app.recording.*`.
- Build after the move.

### Done when
- Recording state is grouped under `app.recording`.
- Recording lifecycle is easier to trace.
- Build passes.

### Verification
- Confirm recording request / active / result state is together.
- Confirm unrelated worker or project state did not get mixed into `recording`.
- Confirm `make -j4` passes.

**If verification is not approved for cleanliness, do another pass on Step 9 before moving on.**

---

## Step 10 — Move engine/editor state

### Goal
Separate app-owned editor state from UI routing and from real-time shared state.

### Tasks
- Move app-owned layer/editor/engine edit state into `AppEngineState`.
- Update call sites to `app.engine.*`.
- Do not move audio-thread-owned runtime data into this bucket.
- Build after the move.

### Done when
- User-facing engine/editor state is grouped under `app.engine`.
- Build passes.

### Verification
- Confirm this bucket contains editor/app-owned engine state, not true audio-owned runtime state.
- Confirm the header reads like a user-edit / perform-edit state owner.
- Confirm `make -j4` passes.

**If verification is not approved for cleanliness, do another pass on Step 10 before moving on.**

---

## Step 11 — Move worker coordination state carefully

### Goal
Isolate app-side worker coordination without mixing in everything the worker system touches.

### Tasks
- Move app-owned worker request/progress/job coordination state into `AppWorkerState`.
- Update call sites to `app.worker.*`.
- Keep project state in `project` unless it is truly worker-owned app coordination.
- Build after the move.

### Done when
- Worker coordination fields are grouped under `app.worker`.
- Project and worker ownership are more obvious.
- Build passes.

### Verification
- Confirm `worker` does not become a dumping ground.
- Confirm project state was not yanked out just because worker code touches it.
- Confirm `make -j4` passes.

**If verification is not approved for cleanliness, do another pass on Step 11 before moving on.**

---

## Step 12 — Move shared / cross-thread handoff fields last

### Goal
Make the most sensitive ownership boundary explicit.

### Tasks
- Move cross-thread, atomic, publish/apply, and handoff-sensitive fields into `AppSharedState`.
- Update call sites to `app.shared.*`.
- Build after the move.

### Done when
- Sensitive shared state is clearly grouped and visibly separate from normal UI/app state.
- Build passes.

### Verification
- Confirm this header contains the truly sensitive shared or cross-thread fields.
- Confirm the ownership comment clearly warns readers what this struct is for.
- Confirm `make -j4` passes.

**If verification is not approved for cleanliness, do another pass on Step 12 before moving on.**

---

## Step 13 — Clean includes and reduce header weight

### Goal
Keep the new split from becoming a multi-header mess.

### Tasks
- Remove unnecessary includes from the new domain headers.
- Forward declare where practical.
- Keep each header as lightweight as possible.
- Build after include cleanup.

### Done when
- The headers are not dragging unrelated dependencies everywhere.
- Include relationships are cleaner.
- Build passes.

### Verification
- Confirm each header includes only what it truly needs.
- Confirm there is no accidental circular dependency.
- Confirm `make -j4` passes.

**If verification is not approved for cleanliness, do another pass on Step 13 before moving on.**

---

## Step 14 — Re-comment the split by ownership

### Goal
Make the new boundaries self-explanatory for future readers and AI tools.

### Tasks
- Add short ownership comments to each domain struct.
- Make it obvious which fields are:
  - main-thread UI-only
  - project coordination
  - worker coordination
  - recording lifecycle
  - engine/editor state
  - diagnostics
  - shared/cross-thread
- Remove stale comments that imply the old flat layout.

### Done when
- The new headers explain themselves.
- The comments match current ownership.
- There are no stale “historical” comments that fight the new layout.

### Verification
- Confirm each domain header starts with a strong ownership statement.
- Confirm no old comments imply flat ownership that no longer exists.
- Confirm the split can be understood quickly just from the comments.

**If verification is not approved for cleanliness, do another pass on Step 14 before moving on.**

---

## Step 15 — Make app_state.h intentionally boring

### Goal
Finish with a small top-level state header that is structural, not overwhelming.

### Tasks
- Remove leftover mixed-field clutter from `app_state.h`.
- Keep it focused on:
  - includes
  - top-level `AppState`
  - domain member composition
- Build after cleanup.

### Done when
- `app_state.h` is small, readable, and mostly structural.
- The heavy state detail lives in the domain headers.
- Build passes.

### Verification
- Confirm `app_state.h` no longer reads like the giant convergence file.
- Confirm the domain members are all clearly named and complete.
- Confirm `make -j4` passes.

**If verification is not approved for cleanliness, do another pass on Step 15 before moving on.**

---

## Final verification — Cleanliness gate

### Goal
Do not accept the split just because it compiles.

### Tasks
Perform these trace tests:

#### Trace test 1 — UI bug readability
- Can a reader mostly trace a UI navigation/focus issue through `app_state_ui.h`?

#### Trace test 2 — Project bug readability
- Can a reader mostly trace project save/load state through `app_state_project.h`, `app_state_worker.h`, and `app_state_shared.h`?

#### Trace test 3 — Recording bug readability
- Can a reader mostly trace recording lifecycle state through `app_state_recording.h`?

#### Trace test 4 — Diagnostics readability
- Can a reader quickly find counters/debug instrumentation in `app_state_diagnostics.h` without scanning functional state?

#### Trace test 5 — Shared safety readability
- Is it visually obvious which fields are sensitive shared/handoff state?

### Done when
- The split improves bug tracing, not just file count.
- The new headers feel cleaner than the original single file.
- A reader no longer has to begin every investigation in one giant mixed state header.

### Verification
- Approve only if the split is clearly cleaner, not merely “different.”
- If any domain still feels muddy or overloaded, do another pass on that domain before calling the checklist done.

---

## Prompt footer for every step

Add this footer to every cleanup prompt for this checklist:

Follow:
- `docs/APP_STATE_HEADER_SPLIT_CHECKLIST.md`

This pass is extraction-only.
Do not change behavior.
Do not change thread ownership.
Complete only the named checklist step.

At the end of the step, run a verification pass for cleanliness:
- confirm the moved fields match the intended ownership for this step
- confirm unrelated state was not swept into the new domain
- confirm the result is cleaner than before, not just different
- if cleanliness is not clearly approved, do one more cleanup pass on the same step before stopping

Required report:
- short summary
- exact files changed
- build command(s) run
- build result
- verification result
- cleanliness approval or reason for additional same-step pass
