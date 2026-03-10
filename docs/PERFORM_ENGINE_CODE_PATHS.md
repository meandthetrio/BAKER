# PERFORM ENGINE CODE PATHS

## Purpose
This document maps the CURRENT IMPLEMENTED code paths for the `PerformEngine` screen.

It exists to help feature work stay surgical and reuse existing plumbing instead of introducing duplicate paths.

This is a developer implementation map, not a user-facing feature doc.

## Scope
This doc covers:

- navigation into and out of `PerformEngine`
- `AppState` fields owned or used by ENGINE
- ENGINE lifecycle, event, enter, and render functions
- the existing sample load path reused by ENGINE
- the existing param publish / audio handoff path reused by ENGINE
- closely related files that should be inspected before editing ENGINE behavior

## Source of Truth
Primary source of truth is code:

- `ui_screens.cpp`
- `app_state.h`
- `params.h`
- `params.cpp`
- `ui_requests.cpp`
- `ui_worker.cpp`
- `main.cpp`

Reference docs to read first:

- `docs/FILE_MAP.md`
- `docs/HARDWARE_CONNECTIONS.md`
- `docs/HARDWARE_NAMING.md`
- `docs/PERFORM_MENU_FOCUS_REFERENCE.md`

---

## 1. Navigation Path

### Main menu to PERFORM
Entry begins in the main menu and routes into the PERFORM branch.

Relevant path:
- `Start`
- `PerformMenu`
- `PerformEngine`

Relevant code:
- `ui_screens.cpp`
  - `MainMenu_OnEnter(...)`
  - `PerformMenu_OnEvent(...)`
  - `PerformMenu_OnEnter(...)`

Behavior:
- main menu `PERFORM` row enters `PerformMenu`
- `perform_menu_index == ENGINE` enters `UiScreenId::PerformEngine`

### PerformMenu to ENGINE
Relevant code:
- `ui_screens.cpp`
  - `PerformMenu_OnEvent(...)`
  - `PerformMenu_OnEnter(...)`

Relevant state:
- `app_state.h`
  - `perform_menu_index`

### ENGINE exits
Relevant code:
- `ui_screens.cpp`
  - `PerformEngine_OnEnter(...)`
  - normal back behavior via pod encoder button pop in shared nav flow

Behavior:
- `WAVE` row enter pushes `UiScreenId::PerformWaveEdit`
- `LOAD` row enter pushes `UiScreenId::SdBrowse`
- `TUNE` row enter does not push a new screen

---

## 2. ENGINE-Owned / ENGINE-Used AppState Fields

Defined in `app_state.h` unless otherwise noted.

### Core ENGINE screen state
- `perform_layer`
  - active layer context for ENGINE and other PERFORM screens
  - `0 = A`, `1 = B`

- `perform_engine_row`
  - row selection for ENGINE
  - current implemented rows:
    - `0 = WAVE`
    - `1 = LOAD`
    - `2 = TUNE`

### Per-layer engine values shown or edited by ENGINE
- `engine_tune_semitones[2]`
  - edited on the `TUNE` row

- `engine_gain_db[2]`
  - not edited on ENGINE screen today, but published by the same layer-param path

- `engine_play_mode[2]`
  - not edited on ENGINE screen today, but loop-mode publish shares the same path

### Per-layer loaded sample metadata shown by ENGINE
- `engine_sample_path[2]`
- `engine_sample_name[2]`

These are UI-facing metadata fields for the currently loaded sample per layer.

### Perform-load flow flags
- `engine_load_target_layer`
- `engine_load_from_perform`

These flags are how ENGINE reuses the existing SD browser + worker load plumbing.

### Applied-generation tracking
- `engine_seen_applied_gen`

Used by ENGINE to detect that a loaded sample has actually been applied on the audio side and that metadata should be refreshed.

### ENGINE UI transient state
- `engine_header_invert_until_ms`
  - short-lived render timer used for ENGINE layer-toggle header flash

### WAVE edit staging state reused by ENGINE path
- `perform_wave_edit_entry[kSdSampleSlots]`
- `perform_wave_edit_has_entry`

These hold a trim snapshot at `PerformWaveEdit` entry so BACK can cancel and restore.

### Shared sample / edit / published state reused by ENGINE
- `sd_slots[kSdSampleSlots]`
- `sd_edit_slots[kSdSampleSlots]`
- `sd_current_slot`
- `sd_published_gen`
- `sd_applied_gen`
- `sd_published_ready`
- `sd_published_slot`

These are shared sample-bank / publish / apply fields. ENGINE must reuse them rather than creating its own load/apply path.

---

## 3. Screen Registration

The ENGINE screen is registered in `ui_screens.cpp`.

Relevant code:
- `ui_screens.cpp`
  - screen table entries for:
    - `PerformEngine_OnScreenEnter`
    - `PerformEngine_OnEvent`
    - `PerformEngine_Render`
    - `PerformEngine_OnEnter`

This means most ENGINE screen behavior is centralized in `ui_screens.cpp`.

---

## 4. ENGINE Lifecycle Functions

## `PerformEngine_OnScreenEnter(UiScreenCtx& ctx)`
Relevant code:
- `ui_screens.cpp`

Current behavior:
1. calls `EngineRefreshLoadedMetadata(...)`
2. checks whether a perform-initiated load is still pending
3. if not pending, syncs `sd_current_slot` to the active `perform_layer`
4. clears perform-load flags:
   - `engine_load_from_perform = false`
   - `engine_load_target_layer = 0xFF`
5. forces ENGINE row focus to `LOAD` (`perform_engine_row = 1`)
6. calls `PublishEngineLayerParams(...)`
7. marks UI dirty

Why it matters:
- ENGINE entry is not just visual setup
- it re-syncs sample-slot context and republishes current layer params using existing plumbing

---

## 5. ENGINE Helper Functions

## `PublishEngineLayerParams(UiScreenCtx& ctx)`
Relevant code:
- `ui_screens.cpp`

Current behavior:
- reads active layer from `perform_layer`
- writes layer targets into `ctx.params->EditTargets()`:
  - `engine_tune_semitones[layer]`
  - `engine_gain_db[layer]`
  - `engine_loop_mode[layer]`
- calls `ctx.params->PublishTargets()`

Important rule:
- ENGINE should publish param changes through this existing target/publish path
- do not bypass this with direct audio-engine writes from UI code

## `EngineRefreshLoadedMetadata(AppState& app)`
Relevant code:
- `ui_screens.cpp`

Current behavior:
- compares `sd_applied_gen` against `engine_seen_applied_gen`
- when a new sample application is observed:
  - updates `engine_seen_applied_gen`
  - reads current slot from `sd_current_slot`
  - if a sample is present:
    - copies `app.sd.last_loaded_path` into `engine_sample_path[slot]`
    - extracts basename into `engine_sample_name[slot]`
- clears:
  - `engine_load_target_layer`
  - `engine_load_from_perform`
- marks UI dirty

Important rule:
- ENGINE sample metadata refresh happens after sample application is observed
- this should remain tied to the existing generation/apply flow

## `DrawWaveformPreview(...)`
Relevant code:
- `ui_screens.cpp`

Current behavior:
- draws waveform outline and amplitude preview from `Sample`
- optionally clamps and uses `SampleEdit`
- used by ENGINE render and wave-edit related UI

Important rule:
- waveform drawing already exists
- ENGINE features should reuse this helper rather than introducing a second waveform renderer

---

## 6. ENGINE Event Handling

## `PerformEngine_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)`
Relevant code:
- `ui_screens.cpp`

This is the main input handler for the screen.

### Layer toggle
Control:
- `kUiBtnPod2`

Behavior:
- toggles `perform_layer`
- updates `sd_current_slot`
- starts transient header invert flash (`engine_header_invert_until_ms = now + 250ms`)
- calls `PublishEngineLayerParams(...)`
- marks UI dirty

Meaning:
- `Pod2` is the active-layer toggle on ENGINE

### Row selection
Control:
- `kUiEncPod`

Behavior:
- increments / decrements `perform_engine_row`
- wraps across implemented ENGINE rows:
  - `WAVE`
  - `LOAD`
  - `TUNE`
- marks UI dirty

Meaning:
- pod encoder owns row focus on ENGINE

### Tune editing
Control:
- `kUiEncExt`

Behavior:
- only acts when selected row is `TUNE`
- edits `engine_tune_semitones[layer]`
- clamps to `-24 .. +24`
- calls `PublishEngineLayerParams(...)`
- marks UI dirty

Meaning:
- external encoder owns value editing for tune
- no tune edit occurs on `WAVE` or `LOAD`

---

## 7. ENGINE Row Enter Behavior

## `PerformEngine_OnEnter(UiScreenCtx& ctx)`
Relevant code:
- `ui_screens.cpp`

This handles ENTER on the ENGINE screen.

### Row = `WAVE`
Behavior:
- pushes `UiScreenId::PerformWaveEdit`

### Row = `LOAD`
Behavior:
- sets:
  - `engine_load_target_layer = perform_layer`
  - `engine_load_from_perform = true`
- pushes `UiScreenId::SdBrowse`

### Row = `TUNE`
Behavior:
- returns false
- no new screen is pushed

Important rule:
- row enter behavior is centralized here
- new ENGINE rows should follow this existing pattern instead of inventing separate navigation logic elsewhere

---

## 8. ENGINE Render Path

## `PerformEngine_Render(UiScreenCtx& ctx)`
Relevant code:
- `ui_screens.cpp`

Current render behavior:
- calls `EngineRefreshLoadedMetadata(app)`
- draws compact header tag (`engine a` / `engine b`) with invert flash support
- reads current sample from:
  - `sd_slots[layer]`
- reads current edit from:
  - `sd_edit_slots[layer]`
- shows loaded sample name (lowercased) from:
  - `engine_sample_name[layer]`
- draws waveform preview using:
  - `DrawWaveformPreview(...)`
- inverts waveform region when selected row is `WAVE`
- draws stylized footer row actions:
  - `LOAD` wordmark (inverted box when selected)
  - `TUNE` wordmark or signed semitone value (dotted box when selected)

Important details:
- the `WAVE` row is not just a text row; it is the waveform region
- waveform display already reflects current sample/edit context for the active layer

---

## 9. ENGINE to WAVE EDIT Reuse Path

### Entry path
Relevant code:
- `ui_screens.cpp`
  - `PerformEngine_OnEnter(...)`

Behavior:
- `WAVE` row enter pushes `UiScreenId::PerformWaveEdit`

### Shared state used by wave edit
Relevant code:
- `ui_screens.cpp`
  - `PerformWaveEdit_OnScreenEnter(...)`
  - `PerformWaveEdit_OnEnter(...)`
  - `PerformWaveEdit_Render(...)`
  - `PerformWaveEdit_OnEvent(...)`

Shared context:
- `perform_layer`
- `sd_current_slot`
- `sd_slots[layer]`
- `sd_edit_slots[layer]`
- `perform_wave_edit_entry[...]`
- `perform_wave_edit_has_entry`

Behavior:
- entering WAVE edit snapshots trim state for cancel/back
- encoder edits mutate staged `sd_edit_slots[layer]`
- `EXT click` commits staged edit through existing `sd_edit_pending/sd_edit_gen/sd_edit_ready` publish path, then pops
- `POD encoder click` cancels and restores snapshot, then pops

Important rule:
- ENGINE and WAVE EDIT already share sample/edit context
- if ENGINE gains more waveform-related controls, prefer extending this existing relationship instead of duplicating edit state

---

## 10. ENGINE Sample Load Reuse Path

This is the key load path that ENGINE already reuses.

### Step 1: ENGINE starts the load flow
Relevant code:
- `ui_screens.cpp`
  - `PerformEngine_OnEnter(...)`

Behavior on `LOAD` row:
- sets:
  - `engine_load_target_layer`
  - `engine_load_from_perform`
- pushes `UiScreenId::SdBrowse`

### Step 2: SD browser selects the WAV
Relevant code:
- `ui_screens.cpp`
  - `SdBrowse_OnEvent(...)`

Behavior on EXT click:
- if `engine_load_target_layer` is valid:
  - determines target layer
  - forces `sd_current_slot` to the opposite slot so the existing loader lands on the intended target
  - copies selected file path into:
    - `engine_sample_path[target]`
  - extracts basename into:
    - `engine_sample_name[target]`
- pushes:
  - `UiReqType::LoadWavIndex`
- marks SD loading status
- if `engine_load_from_perform` is true:
  - pops back out of `SdBrowse`

Important rule:
- ENGINE should keep using the existing SD browser and request system
- do not add an ENGINE-specific loader path

### Step 3: request plumbing receives load request
Relevant code:
- `ui_requests.cpp`
  - `UiReq_Push(...)`

Behavior:
- `UiReqType::LoadWavIndex` is treated specially
- request is stored as:
  - `app.sd.load_pending = true`
  - `app.sd.load_pending_index = r.a`

Important rule:
- load requests are already wired into existing request plumbing

### Step 4: worker executes the load
Relevant code:
- `ui_worker.cpp`
  - `StartLoad(...)`
  - `LoadStep(...)`
  - worker loop handling for `LoadWavIndex`

Behavior:
- worker consumes `sd.load_pending`
- starts WAV load
- progresses loading in worker-safe steps
- publishes loaded sample into shared SD slot state
- increments `sd_published_gen`
- marks `sd_published_ready`

Important rule:
- all non-real-time loading work already belongs in the worker path
- do not move WAV loading into UI/event code

### Step 5: audio thread applies the loaded sample
Relevant code:
- `main.cpp`

Behavior in audio callback:
- checks:
  - `sd_published_ready`
  - `sd_published_gen`
  - `sd_applied_gen`
- when new sample data is ready:
  - reads published slot
  - calls `g_voice.SetSample(&g_app.sd_slots[slot])`
  - updates:
    - `sd_applied_gen`
    - `sd_current_slot`
  - clears `sd_published_ready`

Important rule:
- sample application belongs to the existing audio-side handoff path
- UI code should not try to apply samples directly

### Step 6: ENGINE observes completion and refreshes metadata
Relevant code:
- `ui_screens.cpp`
  - `EngineRefreshLoadedMetadata(...)`

Behavior:
- detects new `sd_applied_gen`
- refreshes engine-facing metadata
- clears perform-load flags
- marks UI dirty

### Full load chain summary
Existing full path is:

`PerformEngine`
→ `SdBrowse`
→ `UiReqType::LoadWavIndex`
→ `ui_worker` load
→ audio callback `SetSample(...)`
→ `EngineRefreshLoadedMetadata(...)`

This is the load path future ENGINE features should reuse.

---

## 11. ENGINE Param Publish / Audio Handoff Reuse Path

This is the existing safe path for tune and related per-layer engine values.

### Step 1: ENGINE edits local AppState value
Relevant code:
- `ui_screens.cpp`
  - `PerformEngine_OnEvent(...)`

Current example:
- edits `engine_tune_semitones[layer]` on `TUNE` row

### Step 2: ENGINE publishes active-layer params
Relevant code:
- `ui_screens.cpp`
  - `PublishEngineLayerParams(...)`

Behavior:
- copies AppState values into param targets
- calls `PublishTargets()`

### Step 3: params system smooths / applies current values
Relevant code:
- `params.cpp`
  - `Params::PublishTargets()`
  - `Params::AudioBlockTick(...)`

Behavior:
- `PublishTargets()` flips the published target buffer
- `AudioBlockTick(...)` smooths:
  - `engine_tune_semitones`
  - `engine_gain_db`
- `AudioBlockTick(...)` applies loop mode as target state

### Step 4: audio callback sends current params to voice engine
Relevant code:
- `main.cpp`

Behavior:
- per block / tick, audio side calls:
  - `g_voice.SetEngineTuneSemitones(layer, g_params.current.engine_tune_semitones[layer])`
  - `g_voice.SetEngineGainDb(layer, g_params.current.engine_gain_db[layer])`

Important rule:
- ENGINE parameter changes should continue to flow:
  - UI/AppState
  - param targets
  - smoothed current params
  - voice engine
- do not bypass this by writing directly from UI into voice state

### Full param chain summary
Existing param path is:

ENGINE UI edit
→ `PublishEngineLayerParams(...)`
→ `Params::PublishTargets()`
→ `Params::AudioBlockTick(...)`
→ `g_voice.SetEngine...(...)`

This is the param path future ENGINE controls should reuse where applicable.

---

## 12. Files to Inspect Before Editing ENGINE

Highest-priority files:

1. `docs/PERFORM_MENU_FOCUS_REFERENCE.md`
2. `docs/FILE_MAP.md`
3. `app_state.h`
4. `ui_screens.cpp`
5. `params.h`
6. `params.cpp`
7. `main.cpp`
8. `ui_requests.cpp`
9. `ui_worker.cpp`
10. `sd_browser_state.h`
11. `sd_browser_state.cpp`

Secondary related files, depending on the feature:
- `voice_engine.h`
- `voice_engine.cpp`
- `sample_edit.h`
- `ui_layout.*`
- `ui_render.*`

---

## 13. Reuse Rules for Future ENGINE Work

When building ENGINE features:

- reuse existing screen navigation and row-enter patterns
- reuse `perform_layer` as the active layer selector
- reuse `PublishEngineLayerParams(...)` for engine-layer param publication where applicable
- reuse SD browser + request + worker + audio apply flow for sample loading
- reuse `DrawWaveformPreview(...)` and existing sample/edit state for waveform display
- preserve audio-thread safety:
  - no blocking
  - no filesystem work
  - no allocation
  - no logging in audio code

Avoid:
- duplicate sample loading paths
- direct UI-to-audio mutation that bypasses params or publish/apply handoff
- separate ENGINE-only sample state when shared state already exists

---

## 14. Required Doc Update Rule

When ENGINE behavior changes, update this doc if any of the following change:

- ENGINE navigation path
- ENGINE row list or row meaning
- ENGINE focusable objects
- ENGINE event/control ownership
- ENGINE load flow
- ENGINE param publish path
- related files or implementation entry points

Also update:
- `docs/PERFORM_MENU_FOCUS_REFERENCE.md`
  - whenever ENGINE menu structure, focusables, row behavior, or control behavior changes

Update other docs in `docs/` when file roles, hardware usage, or naming change.

---

## 15. Quick Reference Summary

### Screen entry points
- `PerformMenu_OnEnter(...)`
- `PerformEngine_OnScreenEnter(...)`

### Main ENGINE handlers
- `PerformEngine_OnEvent(...)`
- `PerformEngine_OnEnter(...)`
- `PerformEngine_Render(...)`

### Main ENGINE helpers
- `PublishEngineLayerParams(...)`
- `EngineRefreshLoadedMetadata(...)`
- `DrawWaveformPreview(...)`

### Load path
- `PerformEngine_OnEnter(...)`
- `SdBrowse_OnEvent(...)`
- `UiReq_Push(...)`
- `ui_worker.cpp` load handling
- `main.cpp` sample apply
- `EngineRefreshLoadedMetadata(...)`

### Param path
- `PerformEngine_OnEvent(...)`
- `PublishEngineLayerParams(...)`
- `Params::PublishTargets()`
- `Params::AudioBlockTick(...)`
- `main.cpp` engine param handoff

---

## 16. Current Implemented ENGINE Rows

Current implemented rows are:

1. `WAVE`
2. `LOAD`
3. `TUNE`

Current implemented per-row behavior:

- `WAVE`
  - selected by pod encoder
  - enter pushes `PerformWaveEdit`
  - waveform region is inverted when selected

- `LOAD`
  - selected by pod encoder
  - enter pushes `SdBrowse` with perform-load context
  - default focused row on ENGINE screen entry

- `TUNE`
  - selected by pod encoder
  - edited by external encoder
  - enter does not push a new screen

Current shared action:

- `Pod2`
  - toggles active layer `A/B`
