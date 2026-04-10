# PERFORM ENGINE CODE PATHS

## Purpose
This document maps the CURRENT IMPLEMENTED code paths for the `PerformEngine` screen and the shared PERFORM plumbing that `PerformKeyzone`, `PerformAdsr`, and `PerformEmphasis` reuse.

It exists to help feature work stay surgical and reuse existing plumbing instead of introducing duplicate paths.

This is a developer implementation map, not a user-facing feature doc.

## Scope
This doc covers:

- navigation into and out of `PerformEngine`
- navigation into and out of `PerformKeyzone` where it reuses ENGINE-owned/shared PERFORM behavior
- navigation into and out of `PerformAdsr` where it reuses the same shared PERFORM behavior
- navigation into and out of `PerformEmphasis` where it reuses the same shared PERFORM behavior
- `AppState` fields owned or used by ENGINE
- `AppState` / params fields reused by KEYZONE
- `AppState` fields reused by ADSR UI
- `AppState` / params fields reused by EMPHASIS UI
- ENGINE lifecycle, event, enter, and render functions
- ADSR lifecycle, event, and render functions
- EMPHASIS lifecycle, event, and render functions
- the existing sample load path reused by ENGINE
- the existing param publish / runtime handoff paths reused by ENGINE, KEYZONE, ADSR-loop, and EMPHASIS
- closely related files that should be inspected before editing ENGINE behavior

## Source of Truth
Primary source of truth is code:

- `ui_screens.cpp`
- `app_state.h`
- `params.h`
- `params.cpp`
- `voice_engine.h`
- `voice_engine.cpp`
- `voice_engine_playback.cpp`
- `voice_engine_emphasis.cpp`
- `voice_engine_voice_lifecycle.cpp`
- `voice_engine_events.cpp`
- `voice_engine_render.cpp`
- `ui_requests.cpp`
- `ui_worker.cpp`
- `main.cpp`
- `sample_edit.h`

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

### PerformMenu to KEYZONE
Relevant code:
- `ui_screens.cpp`
  - `PerformMenu_OnEvent(...)`
  - `PerformMenu_OnEnter(...)`

Relevant state:
- `app_state.h`
  - `perform_menu_index`

Behavior:
- `perform_menu_index == KEYZONE` enters `UiScreenId::PerformKeyzone`

### PerformMenu to ADSR
Relevant code:
- `ui_screens.cpp`
  - `PerformMenu_OnEvent(...)`
  - `PerformMenu_OnEnter(...)`

Relevant state:
- `app_state.h`
  - `perform_menu_index`

Behavior:
- `perform_menu_index == ADSR` enters `UiScreenId::PerformAdsr`

### PerformMenu to EMPHASIS
Relevant code:
- `ui_screens.cpp`
  - `PerformMenu_OnEvent(...)`
  - `PerformMenu_OnEnter(...)`

Relevant state:
- `app_state.h`
  - `perform_menu_index`

Behavior:
- `perform_menu_index == EMPHASIS` enters `UiScreenId::PerformEmphasis`

### ENGINE exits
Relevant code:
- `ui_screens.cpp`
  - `PerformEngine_OnEnter(...)`
  - normal back behavior via pod encoder button pop in shared nav flow

Behavior:
- `WAVE` row enter pushes `UiScreenId::PerformWaveEdit`
- `LOAD` row enter pushes `UiScreenId::SdBrowse`
- `TUNE` row enter does not push a new screen

### KEYZONE exits
Relevant code:
- `ui_screens.cpp`
  - normal back behavior via pod encoder button pop in shared nav flow

Behavior:
- `kUiBtnPodEnc` pops back to `UiScreenId::PerformMenu`
- KEYZONE screen code itself ignores `LShift` (`ctx.lshift`)
- shared parent-preview can still route encoder input back to `PerformMenu` above the screen handler

### ADSR exits
Relevant code:
- `ui_screens.cpp`
  - `PerformAdsr_OnScreenEnter(...)`
  - normal back behavior via pod encoder button pop in shared nav flow

Behavior:
- `kUiBtnPodEnc` pops back to `UiScreenId::PerformMenu`
- `PerformAdsr_OnScreenEnter(...)` resets ADSR focus to stage `A` and syncs the active layer row from `engine_play_mode[layer]`

### EMPHASIS exits
Relevant code:
- `ui_screens.cpp`
  - `PerformEmphasis_OnScreenEnter(...)`
  - normal back behavior via pod encoder button pop in shared nav flow

Behavior:
- `kUiBtnPodEnc` pops back to `UiScreenId::PerformMenu`
- `PerformEmphasis_OnScreenEnter(...)` does not inject new defaults; it only invalidates the screen so the existing shared state renders immediately

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
  - reused by EMPHASIS as drive in tenths of dB (`0..60 = 0.0..6.0 dB`)
  - published by the same shared layer-param path as ENGINE

- `engine_drive_mode[2]`
  - reused by EMPHASIS as the per-layer drive saturation selector
  - values are `0 = odd`, `1 = even`
  - published through the same shared layer-param path instead of a side channel

- `engine_play_mode[2]`
  - not edited on ENGINE screen today, but loop-mode publish shares the same path
  - default-initialized in `AppState` to `LOOP` for both layers, and reused as the startup playback-type source

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
  - also reused by KEYZONE for the same active-layer flash behavior

### KEYZONE per-layer note bounds
- `perform_keyzone_lo_note[2]`
- `perform_keyzone_hi_note[2]`

These are the per-layer MIDI note bounds edited by `PerformKeyzone`.
They are UI-owned state that now publishes through the same shared params path used by ENGINE-layer values.

### ADSR per-layer UI state
- `perform_adsr_row[2]`
- `perform_adsr_type_focus`
- `perform_adsr_wave_focus`
- `perform_adsr_stage_focus`
- `perform_adsr_loop_attack[2]`
- `perform_adsr_loop_decay[2]`
- `perform_adsr_loop_sustain[2]`
- `perform_adsr_loop_release[2]`
- `perform_adsr_loop_crossfade[2]`
- `perform_adsr_loop_crossfade_shape[2]`
- `perform_adsr_env_a_x[2]`
- `perform_adsr_env_d_x[2]`
- `perform_adsr_env_r_x[2]`
- `perform_adsr_env_s_level[2]`

These are simulator-matched UI-owned ADSR fields. The LOOP seam-crossfade amount remains layer-owned in this existing UI state and publishes through the same shared params handoff instead of adding a second owner.

- `perform_adsr_loop_attack[2]` and `perform_adsr_loop_release[2]` are the existing LOOP timing fields in milliseconds and now store `1..1000 ms`.
- `perform_adsr_loop_decay[2]` and `perform_adsr_loop_sustain[2]` keep their existing UI ranges.

### EMPHASIS UI state
- `perform_emphasis_row`

This is the simulator-matched EMPHASIS focus row owner in `AppState`.
It stays in the existing shared PERFORM state container instead of adding a parallel EMPHASIS state object.

### KEYZONE render helpers reused in `ui_screens.cpp`
- `DrawMicroString(...)`
- `MicroStringWidth(...)`
- `DrawTinyStringCaseSensitive(...)`
- `TinyStringWidthCaseSensitiveTightColons(...)`
- `DrawTinyStringClipped(...)`

These are the draw helpers used to reproduce the simulator KEYZONE composition on the real 128x64 OLED without introducing a second render stack.

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
- also writes the new per-layer EMPHASIS drive mode for both layers from existing shared UI state:
  - `engine_drive_mode[0..1]`
- also writes LOOP runtime ADSR targets for both layers from existing ADSR UI state:
  - `engine_loop_attack_ms[0..1]`
  - `engine_loop_decay_ms[0..1]`
  - `engine_loop_sustain_level[0..1]`
  - `engine_loop_release_ms[0..1]`
  - `engine_loop_crossfade_amount[0..1]`
  - `engine_loop_crossfade_shape[0..1]`
- also copies both KEYZONE note-bound arrays:
  - `perform_keyzone_lo_note[0..1]`
  - `perform_keyzone_hi_note[0..1]`
- calls `ctx.params->PublishTargets()`

Important rule:
- ENGINE and KEYZONE should publish shared PERFORM state changes through this existing target/publish path
- ADSR LOOP and EMPHASIS should continue to reuse this same path for their existing handoffs
- do not bypass this with direct audio-engine writes from UI code

## `PerformAdsr_OnScreenEnter(UiScreenCtx& ctx)`
Relevant code:
- `ui_screens.cpp`

Current behavior mirrored from the simulator:
- resets focus to stage `A`
- clears `perform_adsr_type_focus`
- clears `perform_adsr_wave_focus`
- initializes `perform_adsr_row[active_layer]` from existing `engine_play_mode[layer]`
  - `0 -> 1SHOT`
  - `1 -> LOOP`
- normalizes focus with `PerformAdsrEnsureValidFocus(...)`
- marks UI dirty

Important rule:
- ADSR entry reuses shared PERFORM layer ownership and existing loop-mode state instead of adding a new owner for playback type

## `PerformEmphasis_OnScreenEnter(UiScreenCtx& ctx)`
Relevant code:
- `ui_screens.cpp`

Current behavior mirrored from the simulator:
- does not overwrite filter or drive values on entry
- only marks UI dirty so the existing shared targets/current values render immediately

Important rule:
- this page is a UI-parity pass over existing handoff reuse
- do not add an EMPHASIS-specific runtime init path unless the repo later proves it is required

## `PerformEmphasis_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)`
Relevant code:
- `ui_screens.cpp`

Current behavior mirrored from the simulator:
- ignores global shift-modified routing (`ctx.shift`)
- `kUiEncPod` cycles the 3 EMPHASIS rows with wrap
- `kUiEncExt` edits the focused control:
  - `DRIVE` updates `engine_gain_db[layer]` in tenths of dB and republishes through `PublishEngineLayerParams(...)`
  - `RShift + DRIVE` reuses that same handler and publish path, but edits `engine_drive_mode[layer]` instead and temporarily swaps the knob label to `odd` / `even`
  - `CUTOFF` updates `engine_filter_cutoff_hz[layer]` in `ctx.params->EditTargets()` using the existing nonlinear cutoff mapping, then calls `PublishTargets()`
  - `RESO` updates `engine_filter_resonance[layer]` in `ctx.params->EditTargets()`, then calls `PublishTargets()`
- `kUiBtnPod2` toggles `perform_layer`, syncs `sd_current_slot`, flashes `engine_header_invert_until_ms`, and republishes shared layer params

Important rule:
- EMPHASIS reuses the existing ENGINE shared layer owner and publish path
- this pass adds only the minimal new shared state needed for drive mode and does not add duplicate nav, state, or audio-thread plumbing

## `PerformKeyzone_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)`
Relevant code:
- `ui_screens.cpp`

Current behavior mirrored from the simulator:
- returns immediately when `ctx.lshift` is held
- `kUiBtnPod2` toggles `perform_layer`, syncs `sd_current_slot`, flashes `engine_header_invert_until_ms`, republishes shared PERFORM params
- `RShift + kUiBtnExtEnc` toggles:
  - full range both layers: `A0..C8`, `A0..C8`
  - split range: `A0..B4`, `C5..C8`
- `kUiEncPod` edits `perform_keyzone_lo_note[active_layer]`
- `kUiEncExt` edits `perform_keyzone_hi_note[active_layer]`
- `RShift + kUiEncExt` moves the split boundary by changing:
  - layer A high note
  - layer B low note

Important rule:
- keep KEYZONE behavior matched to the simulator handler unless existing repo plumbing requires adaptation around it

## `PerformKeyzone_Render(UiScreenCtx& ctx)`
Relevant code:
- `ui_screens.cpp`

Current render behavior mirrored from the simulator:
- upper-right micro header box labeled `kyzn a` or `kyzn b`
- top `Lo:` / `Hi:` row with animated dotted boxes
- `RShift` changes the `Lo:` / `Hi:` row to the sim’s filled highlight treatment
- section 1 draws layer A range box
- section 2 draws layer B range box
- inactive layer interior uses the sim-style dotted fill
- repeated layer letters are clipped inside each layer box
- touching split boundaries draw the same small bridge markers as the sim

## `PerformAdsr_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)`
Relevant code:
- `ui_screens.cpp`

Current behavior mirrored from the simulator:
- returns immediately when generic `ctx.shift` is held
- `kUiBtnPod2` toggles `perform_layer`, syncs `sd_current_slot`, reuses `engine_header_invert_until_ms`, normalizes focus, and republishes shared ENGINE layer params
- `kUiEncPod` cycles focus through:
  - `TYPE -> WAVE -> A -> D -> S -> R` when the active layer row is `LOOP`
  - `TYPE -> A -> D -> S -> R` otherwise
  - disabled stages are still skipped in `1SHOT`
- `kUiEncExt` edits the currently focused item:
  - TYPE focus cycles `1SHOT / LOOP / ADSR`
  - TYPE changes to `1SHOT` or `LOOP` reuse existing `engine_play_mode[layer]` and `PublishEngineLayerParams(...)`
  - LOOP wave-preview focus edits `perform_adsr_loop_crossfade[active_layer]`
  - `RShift + kUiEncExt` on LOOP wave-preview focus edits `perform_adsr_loop_crossfade_shape[active_layer]`
  - LOOP stage focus edits numeric `A/D/S/R` values
  - ADSR stage focus edits graph control points or sustain level

Important rule:
- the simulator’s UI-only ADSR state stays in `AppState`
- only the already-existing playback-type hookup is reused
- LOOP runtime now reuses the existing shared `Params` publish path instead of adding a second handoff
- `ADSR` row remains UI-only in this pass

## `PerformAdsr_Render(UiScreenCtx& ctx)`
Relevant code:
- `ui_screens.cpp`

Current render behavior mirrored from the simulator:
- upper-right micro header box labeled `adsr a` or `adsr b`
- centered `playback type` label area that changes to a boxed `1shot` / `loop` / `adsr` value when TYPE is focused
- main waveform box reused as the ADSR canvas via `DrawWaveformPreview(...)`
  - solid border when unfocused
  - dotted border when LOOP wave-preview focus is active
  - focused LOOP preview overlays left/right crossfade regions with grid shading and vertical boundary bars
  - while `RShift` is held on focused LOOP preview, those same left/right regions temporarily render the per-layer seam fade-out / fade-in curves instead, with the waveform still visible underneath
- ADSR mode draws the simulator envelope graph plus vertical guide lines
- bottom strip draws `a d s r`
  - LOOP mode shows dotted numeric edit boxes
  - ADSR mode shows stage letters positioned under the graph
  - focused sustain in ADSR mode uses the simulator’s filled highlight while the other ADSR boxes use dotted outlines
  - disabled `1SHOT` stages are crossed out

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
- optionally swaps the outer border to dotted when a caller wants focus styling
- used by ENGINE render and wave-edit related UI

Important rule:
- waveform drawing already exists
- ENGINE, KEYZONE, and ADSR features should reuse this helper rather than introducing a second waveform renderer

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

## 9. LOOP Runtime Path

This section documents the CURRENT IMPLEMENTED runtime path for playback type `LOOP`.

### Playback type check
Relevant code:
- `main.cpp`
  - `AudioCallback(...)`
- `voice_engine_voice_lifecycle.cpp`
  - `StartVoice_(...)`
  - `NoteOff_(...)`
- `voice_engine_render.cpp`
  - `RenderBlock(...)`

Behavior:
- UI publishes `engine_loop_mode[layer]` through `PublishEngineLayerParams(...)`
- audio thread reads the published/smoothed value from `g_params.current.engine_loop_mode[layer]`
- `AudioCallback(...)` forwards it to `VoiceEngine::SetEngineLoopEnabled(layer, ...)`
- `StartVoice_(...)` snapshots that state into the per-voice `loop_voice` flag

Important rule:
- LOOP runtime reuses the existing ENGINE playback-type path
- no second playback dispatch system was added

### Selected loop region source
Relevant code:
- `sample_edit.h`
  - `SampleEdit`
  - `SampleEdit_Clamp(...)`
- `main.cpp`
  - audio callback edit apply handoff via `SetSampleEdit(...)`
- `voice_engine_render.cpp`
  - `RenderBlock(...)`

Behavior:
- selected region ownership remains in `SampleEdit`:
  - `start_frame`
  - `end_frame`
- the audio callback reuses the existing `sd_edit_pending -> SetSampleEdit(...)` handoff
- when `loop_voice` is active, `RenderBlock(...)` forces the loop span to the selected edit region:
  - `loop_start = start_frame`
  - `loop_end = end_frame`
- existing sample-edit loop points are not used for PERFORM `LOOP`; the ENGINE-selected region is reused directly

### LOOP seam crossfade amount
Relevant code:
- `app_state.h`
  - `perform_adsr_loop_crossfade[2]`
  - `perform_adsr_loop_crossfade_shape[2]`
- `ui_worker.cpp`
  - load completion path assigns the default `1/16` amount and linear shape for the target layer
- `ui_screens.cpp`
  - `PublishEngineLayerParams(...)`
- `params.h`
  - `engine_loop_crossfade_amount[2]`
  - `engine_loop_crossfade_shape[2]`
- `main.cpp`
  - `AudioCallback(...)` -> `SetLoopCrossfadeAmount(...)`
  - `AudioCallback(...)` -> `SetLoopCrossfadeShape(...)`
- `voice_engine_playback.cpp`
  - `ComputeLoopSeamCrossfadeFrames(...)`
  - `SampleAtLoopSeamCrossfade(...)`
- `voice_engine_render.cpp`
  - `RenderBlock(...)`

Behavior:
- the amount is owned per layer in existing shared PERFORM UI state
- the shape is owned beside it in the same existing shared PERFORM UI state
- sample load completion reuses the existing load-finish/default-state path to reset that layer to `1/16` and linear shape
- `PublishEngineLayerParams(...)` republishes both layers' amount and shape through the existing params handoff
- `AudioCallback(...)` forwards the published amount and shape into `VoiceEngine`
- `RenderBlock(...)` consumes them only for `loop_voice` playback and crossfades the selected-region tail into the selected-region head at wrap
- the amount is clamped to `0..1/2` of the selected region so the two seam bounds never cross
- the shape is clamped to `0..1` and remaps the seam blend weights from linear toward equal-power-like without changing the existing seam length or handoff rules

### LOOP note-on / note-off handling
Relevant code:
- `main.cpp`
  - `LayerEligibleForNote(...)`
  - MIDI `NoteOn` / `NoteOff` event push
- `voice_engine_events.cpp`
  - `ProcessEvents(...)`
- `voice_engine_voice_lifecycle.cpp`
  - `StartVoice_(...)`
  - `NoteOff_(...)`

Behavior:
- main thread still owns KEYZONE gating through `LayerEligibleForNote(...)`
- eligible layers push the existing `Event::NoteOnEvent(...)` with the layer encoded in `evt.value`
- `ProcessEvents(...)` reuses the existing fixed voice pool, layer-aware allocation, and stealing path
- `StartVoice_(...)` starts forward looping playback for LOOP voices
- `NoteOff_(...)` now sends LOOP voices into release without the old stop-fade short-circuit, so `R` controls the release time
- non-LOOP voices keep the prior note-off path

### C4 transposition
Relevant code:
- `ui_worker.cpp`
  - loaded/recorded `Sample.root_key = 60`
- `voice_engine_playback.cpp`
  - `ComputeRatio(...)`
- `voice_engine_voice_lifecycle.cpp`
  - `StartVoice_(...)`
- `voice_engine_events.cpp`
  - `ProcessEvents(...)` steal-xfade new-head setup

Behavior:
- loaded samples still use temporary root note `C4` by storing `root_key = 60`
- playback rate is derived by `ComputeRatio(note, sample->root_key)`
- LOOP reuses the existing sample-rate / transpose path rather than adding a parallel pitch system

### LOOP ADSR envelope
Relevant code:
- `params.h`
  - `engine_loop_attack_ms`
  - `engine_loop_decay_ms`
  - `engine_loop_sustain_level`
  - `engine_loop_release_ms`
- `params.cpp`
  - `AudioBlockTick(...)`
- `main.cpp`
  - `AudioCallback(...)` -> `SetLoopEnvelopeParams(...)`
- `voice_engine_playback.cpp`
  - `InitEnvelope(...)`
  - `SetEnvelopeRelease(...)`
- `voice_engine_voice_lifecycle.cpp`
  - `StartVoice_(...)`
  - `NoteOff_(...)`
- `voice_engine_events.cpp`
  - `ProcessEvents(...)` steal-xfade new-head setup

Behavior:
- LOOP ADSR values are published through the existing params handoff
- `StartVoice_(...)` uses per-layer LOOP values only when `loop_voice` is active:
  - `A` = attack ms
  - `D` = decay ms
  - `S` = sustain level (`0 = silence`, `1 = 0 dB`)
  - `R` = release ms
- `NoteOff_(...)` applies the per-layer LOOP release time for LOOP voices
- `1SHOT` and `ADSR` stay on the prior fixed envelope path

### Fixed 1 ms edge fades
Relevant code:
- `voice_engine_playback.cpp`
  - `ComputeLoopBoundaryFade(...)`
  - `ComputeFadeStepMs(...)`
- `voice_engine_render.cpp`
  - `RenderBlock(...)`
- `voice_engine_voice_lifecycle.cpp`
  - `StartVoice_(...)`
- `voice_engine_events.cpp`
  - `ProcessEvents(...)` steal-xfade new-head setup

Behavior:
- LOOP voices get a fixed 1 ms start fade on note-on via the existing fade-in field
- LOOP voices also get a fixed 1 ms gain trim near the selected region start/end in `RenderBlock(...)`
- this edge fade is separate from the LOOP ADSR envelope
- non-LOOP voices keep the prior fade behavior

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

This is the existing safe path for tune and related per-layer engine values, including EMPHASIS.

### Step 1: ENGINE edits local AppState value
Relevant code:
- `ui_screens.cpp`
  - `PerformEngine_OnEvent(...)`

Current examples:
- `PerformEngine` edits `engine_tune_semitones[layer]` on `TUNE`
- `PerformEmphasis` edits `engine_gain_db[layer]` / `engine_drive_mode[layer]` on `DRIVE`
- `PerformEmphasis` edits `engine_filter_cutoff_hz[layer]` / `engine_filter_resonance[layer]` on `CUTOFF` / `RESO`

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
  - `engine_filter_cutoff_hz`
  - `engine_filter_resonance`
- `AudioBlockTick(...)` snapshots:
  - `engine_drive_mode`
- `AudioBlockTick(...)` applies loop mode as target state
- `AudioBlockTick(...)` also snapshots the published KEYZONE note bounds into `Params::current`

### Step 4: audio callback sends current params to voice engine
Relevant code:
- `main.cpp`

Behavior:
- per block / tick, audio side calls:
  - `g_voice.SetEngineTuneSemitones(layer, g_params.current.engine_tune_semitones[layer])`
  - `g_voice.SetEngineGainDb(layer, g_params.current.engine_gain_db[layer])`
  - `g_voice.SetEngineDriveMode(layer, g_params.current.engine_drive_mode[layer])`
  - `g_voice.SetEngineFilterCutoffHz(layer, g_params.current.engine_filter_cutoff_hz[layer])`
  - `g_voice.SetEngineFilterResonance(layer, g_params.current.engine_filter_resonance[layer])`

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

## 11B. EMPHASIS DSP Application Point

This section documents the corrected runtime application point for EMPHASIS.

### Previous path
- the old implementation applied EMPHASIS cutoff/resonance inside the per-voice render loop in `VoiceEngine::RenderBlock(...)`
- each voice carried its own temporary filter state for that path

### Current path
Relevant code:
- `main.cpp`
  - `AudioCallback(...)`
- `voice_engine.h`
  - per-layer `LayerBusState`
- `voice_engine.cpp`
  - `SetEngineGainDb(...)`
  - `SetEngineDriveMode(...)`
  - `SetEngineFilterCutoffHz(...)`
  - `SetEngineFilterResonance(...)`
- `voice_engine_emphasis.cpp`
  - `ProcessLayerBusSample_(...)`
- `voice_engine_render.cpp`
  - `RenderBlock(...)`

Behavior:
- voices still render and sum by layer inside `VoiceEngine::RenderBlock(...)`
- `RenderBlock(...)` now uses the output buffers as temporary layer buses:
  - `outL` = layer A summed bus
  - `outR` = layer B summed bus
- after voice summing, each layer bus is processed once through:
  - layer drive using the existing `engine_gain_db[layer]`
  - the new per-layer `engine_drive_mode[layer]`
  - a compact 4-pole lowpass stage with shared layer resonance/cutoff state
- the visible `0.0..6.0 dB` DRIVE control is still reused as-is, but the bus DSP now applies a stronger nonlinear internal gain taper so:
  - low values stay mostly clean
  - mid values warm/thicken audibly
  - high values reach obvious saturation
- EVEN mode keeps the asymmetric shaper path but now normalizes and compensates its output more closely against ODD
- only after that bus-stage processing are layer A and layer B summed to the final output

Important rule:
- EMPHASIS cutoff/resonance no longer use the old per-voice application path
- there is now a single DSP state per layer bus inside `VoiceEngine`, not one filter state per voice
- the UI/control ownership and the UI -> `Params` -> `PublishTargets()` -> audio callback handoff are reused unchanged
- startup defaults for `engine_filter_cutoff_hz[layer]` are now `20 kHz` in the shared targets/current path and the audio-side `VoiceEngine` state

---

## 11A. KEYZONE Publish / Runtime Gate Reuse Path

This is the existing safe shared path that `PerformKeyzone` now reuses.

### Step 1: KEYZONE edits local AppState value
Relevant code:
- `ui_screens.cpp`
  - `PerformKeyzone_OnEvent(...)`

Current edits:
- `perform_keyzone_lo_note[layer]`
- `perform_keyzone_hi_note[layer]`
- `RShift + kUiEncExt` shared split-boundary move
- `RShift + kUiBtnExtEnc` full-range/split toggle

### Step 2: KEYZONE republishes through the shared PERFORM helper
Relevant code:
- `ui_screens.cpp`
  - `PublishEngineLayerParams(...)`

Behavior:
- copies both layer note-bound arrays into `ctx.params->EditTargets()`
- calls `PublishTargets()`

### Step 3: runtime note gating reads the published snapshot
Relevant code:
- `main.cpp`
  - `LayerEligibleForNote(...)`
  - MIDI `NoteOn` loop

Behavior:
- `LayerEligibleForNote(...)` reads `g_params.TargetsForUI()`
- compares incoming MIDI note against:
  - `perform_keyzone_lo_note[layer]`
  - `perform_keyzone_hi_note[layer]`
- only eligible layers enqueue `Event::NoteOnEvent(...)`

Important detail:
- KEYZONE gating happens on the existing main-thread MIDI routing path before events are pushed to audio
- no second router or KEYZONE-specific event queue was added

### Step 4: audio thread still receives the same per-layer event stream
Relevant code:
- `voice_engine_events.cpp`
  - `VoiceEngine::ProcessEvents(...)`

Behavior:
- audio side remains unchanged
- only the eligible layer note events reach the existing voice-engine event path

### Full KEYZONE chain summary
KEYZONE UI edit
→ `PublishEngineLayerParams(...)`
→ `Params::PublishTargets()`
→ `main.cpp::LayerEligibleForNote(...)`
→ `Event::NoteOnEvent(...)`
→ `VoiceEngine::ProcessEvents(...)`

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
- `voice_engine_playback.cpp`
- `voice_engine_emphasis.cpp`
- `voice_engine_voice_lifecycle.cpp`
- `voice_engine_events.cpp`
- `voice_engine_render.cpp`
- `sample_edit.h`
- `ui_layout.*`
- `ui_render.*`

---

## 13. Reuse Rules for Future ENGINE Work

When building ENGINE features:

- reuse existing screen navigation and row-enter patterns
- reuse `perform_layer` as the active layer selector
- reuse `PublishEngineLayerParams(...)` for shared PERFORM layer/keyzone publication where applicable
- reuse SD browser + request + worker + audio apply flow for sample loading
- reuse `DrawWaveformPreview(...)` and existing sample/edit state for waveform display
- reuse `LayerEligibleForNote(...)` as the runtime note gate for per-layer note eligibility work
- preserve audio-thread safety:
  - no blocking
  - no filesystem work
  - no allocation
  - no logging in audio code

Avoid:
- duplicate sample loading paths
- direct UI-to-audio mutation that bypasses params or publish/apply handoff
- separate ENGINE-only sample state when shared state already exists
- separate KEYZONE-only MIDI routing when the existing NoteOn path already owns layer eligibility

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

### Shared KEYZONE/runtime helpers
- `PerformKeyzone_OnEvent(...)`
- `PerformKeyzone_Render(...)`
- `LayerEligibleForNote(...)`

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

### KEYZONE runtime gate path
- `PerformKeyzone_OnEvent(...)`
- `PublishEngineLayerParams(...)`
- `main.cpp` `LayerEligibleForNote(...)`
- MIDI `NoteOn` enqueue loop

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
