# PERFORM MENU FOCUS REFERENCE

## Notes
- This file documents the CURRENT IMPLEMENTED PERFORM branch only.
- Source of truth is code (`ui_screens.*`, `ui_logic.cpp`, `app_state.h`).
- Includes PROCESS in-screen FX detail submenus.
- LShift is a parent-preview modifier (not overlay toggle):
  - hold LShift to preview parent menu/process-main context
  - while held, encoders move parent focus/values
  - releasing LShift commits the preview selection

## Screen Inventory
- Start (Main Menu entry point)
- PerformMenu
- PerformEngine
- PerformWaveEdit
- PerformKeyzone
- PerformAdsr
- PerformEmphasis
- PerformProcess
- PerformProcess / SATURATION detail (in-screen mode)
- PerformProcess / MODULATION detail (in-screen mode)
- PerformProcess / DELAY detail (in-screen mode)
- PerformProcess / REVERB detail (in-screen mode)
- SdBrowse (when entered from PerformEngine LOAD)

## UI Tree
- Start (Main Menu)
  - PerformMenu
    - PerformEngine
      - PerformWaveEdit
      - SdBrowse (engine load flow)
    - PerformKeyzone
    - PerformAdsr
    - PerformEmphasis
    - PerformProcess
      - SATURATION detail (in-screen detail mode)
      - MODULATION detail (in-screen detail mode)
      - DELAY detail (in-screen detail mode)
      - REVERB detail (in-screen detail mode)

## Screen Reference

### Start (Main Menu, PERFORM path)
- Parent: root
- Entered by: boot initialization and BACK pop.
- Exited by: `kUiBtnExtEnc` on selected row.

#### Focusable Objects
1. **Main menu PERFORM row (`main_menu_index == 2`)**
- Type: menu item
- Purpose: enter PERFORM branch.
- Behavior:
  - `kUiEncPod` selects PERFORM.
  - `kUiBtnExtEnc` pushes `UiScreenId::PerformMenu`.
- Result:
  - Opens Perform menu.
- Notes:
  - Same root menu as PRESETS/RECORD.

### PerformMenu (`UiScreenId::PerformMenu`)
- Parent: Start
- Entered by: Start PERFORM row + `kUiBtnExtEnc`.
- Exited by: `kUiBtnExtEnc` pushes selected PERFORM child; `kUiBtnPodEnc` pops to Start.

#### Focusable Objects
1. **PERFORM submenu selector (`perform_menu_index`)**
- Type: 5-item selector
- Purpose: select `ENGINE`, `KEYZONE`, `ADSR`, `EMPHASIS`, `PROCESS`.
- Behavior:
  - `kUiEncPod` cycles with wrap.
  - `kUiBtnExtEnc` pushes selected submenu.
- Result:
  - Submenu navigation.
- Notes:
  - `PerformMenu_OnEnter` handles push by selected index.

### PerformEngine (`UiScreenId::PerformEngine`)
- Parent: PerformMenu
- Entered by: PerformMenu `ENGINE` + `kUiBtnExtEnc`.
- Exited by: `kUiBtnPodEnc` back to PerformMenu; `kUiBtnExtEnc` can push `PerformWaveEdit`/`SdBrowse` by row.

#### Focusable Objects
1. **Engine row selector (`perform_engine_row`)**
- Type: row selector (`WAVE`, `LOAD`, `TUNE`)
- Purpose: choose engine action row.
- Behavior:
  - `kUiEncPod` cycles rows.
  - `kUiBtnExtEnc` runs row enter action.
- Result:
  - `WAVE` pushes `PerformWaveEdit`.
  - `LOAD` pushes `SdBrowse` with perform-load context.
  - `TUNE` does not push a screen.
- Notes:
  - On screen-enter, default row is forced to `LOAD`.
  - Wave region is inverted when `WAVE` row selected.

2. **Tune field (`engine_tune_semitones[layer]`)**
- Type: numeric field
- Purpose: set layer tune in semitones.
- Behavior:
  - `kUiEncExt` edits only when row is `TUNE`.
- Result:
  - Clamped `-24..+24`, then publish params.
- Notes:
  - No effect when `WAVE` or `LOAD` row selected.

3. **Layer toggle (`perform_layer`)**
- Type: toggle action
- Purpose: switch active A/B layer context.
- Behavior:
  - `kUiBtnPod2` toggles layer.
- Result:
  - Updates active slot and published engine-layer params.
- Notes:
  - Shared across PERFORM screens.
  - Layer toggle starts a short header invert flash for visual feedback.

### PerformWaveEdit (`UiScreenId::PerformWaveEdit`)
- Parent: PerformEngine
- Entered by: PerformEngine row `WAVE` + `kUiBtnExtEnc`.
- Exited by:
  - `kUiBtnExtEnc`: commit trim edit and pop to PerformEngine.
  - `kUiBtnPodEnc`: cancel trim edit (restore entry snapshot) and pop to PerformEngine.

#### Focusable Objects
1. **Trim Start handle (`edit.start_frame`)**
- Type: waveform trim field
- Purpose: move sample start boundary.
- Behavior:
  - `kUiEncPod` adjusts start.
  - `RShift` (`ctx.rshift`) uses finer base step.
- Result:
  - Updates staged sample edit for current layer.
- Notes:
  - Active only when sample exists.
  - Staged edit is committed by `kUiBtnExtEnc`.

2. **Trim End handle (`edit.end_frame`)**
- Type: waveform trim field
- Purpose: move sample end boundary.
- Behavior:
  - `kUiEncExt` adjusts end.
  - Same clamp/min-length logic and `RShift` fine stepping.
- Result:
  - Updates staged sample edit for current layer.
- Notes:
  - Edit is clamped by `SampleEdit_Clamp`.
  - `kUiBtnPodEnc` restores the screen-entry trim snapshot.

3. **Layer toggle (`perform_layer`)**
- Type: toggle action
- Purpose: change wave-edit layer.
- Behavior:
  - `kUiBtnPod2` toggles layer.
- Result:
  - Switches edit context to other layer.
- Notes:
  - No list cursor on this screen.

### PerformKeyzone (`UiScreenId::PerformKeyzone`)
- Parent: PerformMenu
- Entered by: PerformMenu `KEYZONE` + `kUiBtnExtEnc`.
- Exited by:
  - `kUiBtnPodEnc` pop to PerformMenu.
  - KEYZONE itself ignores `LShift`; shared parent-preview behavior remains owned by `ui_logic.cpp`.

#### Focusable Objects
1. **Low note bound (`perform_keyzone_lo_note[layer]`)**
- Type: numeric note-range field
- Purpose: set the active layer's low MIDI note.
- Behavior:
  - `kUiEncPod` edits the active layer low note.
- Result:
  - Clamped to `A0..current hi note`.
  - Published through the shared PERFORM params path.
- Notes:
  - Left encoder owns low-note editing on this screen.

2. **High note bound (`perform_keyzone_hi_note[layer]`)**
- Type: numeric note-range field
- Purpose: set the active layer's high MIDI note.
- Behavior:
  - `kUiEncExt` edits the active layer high note.
- Result:
  - Clamped to `current lo note..C8`.
  - Published through the shared PERFORM params path.
- Notes:
  - Right encoder owns high-note editing on this screen.

3. **Alternate split move (`perform_keyzone_hi_note[0]`, `perform_keyzone_lo_note[1]`)**
- Type: modifier edit
- Purpose: move the split boundary between layers without breaking either layer's bounds.
- Behavior:
  - `RShift + kUiEncExt` moves layer A high note and layer B low note together by one note per encoder gesture.
- Result:
  - Boundary motion is clamped so:
    - layer A never collapses below its low note
    - layer B never collapses below its high note
    - split stays inside `A0..C8`
- Notes:
  - This reuses the repo's existing `RShift` alternate-action pattern instead of creating a new modifier system.

4. **Alternate split/full-range toggle**
- Type: modifier action
- Purpose: flip between full-range-on-both-layers and a default split layout.
- Behavior:
  - `RShift + kUiBtnExtEnc` toggles:
    - full range both layers: `A0..C8` on A and B
    - split layout: layer A `A0..B4`, layer B `C5..C8`
- Result:
  - Both layers' keyzone bounds update and publish together.
- Notes:
  - Uses the existing right-side modifier + click pattern already used elsewhere in PERFORM.

5. **Layer toggle (`perform_layer`)**
- Type: toggle action
- Purpose: switch layer context shown by screen.
- Behavior:
  - `kUiBtnPod2` toggles layer.
- Result:
  - Updates slot/published layer params.
  - Starts the same short header-flash timer used by ENGINE.
- Notes:
  - KEYZONE reuses the shared PERFORM layer ownership pattern from ENGINE.
  - Render mirrors the sim structure closely:
    - upper-right `kyzn a/b` micro header box
    - `Lo:` / `Hi:` top row with animated dotted outlines
    - two horizontal layer range boxes with repeated layer letters
    - active layer solid, inactive layer dotted

### PerformAdsr (`UiScreenId::PerformAdsr`)
- Parent: PerformMenu
- Entered by: PerformMenu `ADSR` + `kUiBtnExtEnc`.
- Exited by: `kUiBtnPodEnc` pop to PerformMenu.

#### Focusable Objects
1. **Playback type field (`perform_adsr_row[layer]`, `engine_play_mode[layer]`)**
- Type: enum field (`1SHOT` / `LOOP` / `ADSR`)
- Purpose: choose the screen’s playback-type row exactly like the simulator.
- Behavior:
  - `LEnc` cycles focus across:
    - `TYPE -> WAVE -> A -> D -> S -> R` when playback type is `LOOP`
    - `TYPE -> A -> D -> S -> R` for non-`LOOP` rows
  - `REnc` edits the focused TYPE value when TYPE is focused.
  - `1SHOT` and `LOOP` sync the existing `engine_play_mode[layer]` field through the shared ENGINE publish path.
  - `LOOP` also publishes the existing per-layer LOOP `A/D/S/R` values through the shared params path.
  - `ADSR` remains UI-only and does not add a second runtime path.
- Result:
  - Focus and row behavior match the simulator.
- Notes:
  - Disabled stages are skipped in `1SHOT`.
  - LOOP runtime semantics are:
    - `A` = attack ms
    - `D` = decay ms
    - `S` = sustain level
    - `R` = release ms
  - LOOP playback reuses the loaded sample and ENGINE-selected trim region for forward looping, with note-off entering release.

2. **Wave preview field (`perform_adsr_wave_focus`, `perform_adsr_loop_crossfade[layer]`)**
- Type: LOOP-only focusable waveform control
- Purpose: edit the active layer’s symmetric LOOP seam crossfade without leaving the ADSR page.
- Behavior:
  - Only focusable when playback type is `LOOP`.
  - `REnc` increases/decreases the active layer crossfade amount while focused.
  - `RShift + REnc` increases/decreases the active layer crossfade shape while focused.
  - Focused preview uses a dotted outer border.
  - Focused preview overlays the left/right crossfade regions with grid shading while leaving the middle region normal.
  - While `RShift` is held, the two crossfade boxes temporarily render the fade-shape curves instead:
    - left box shows fade-out
    - right box shows fade-in
    - waveform stays faintly visible underneath
    - each curve is 2 px thick with a 2 px negative-space outline
  - Two vertical bars mark the current start/end crossfade bounds and move inward/outward with `REnc`.
- Result:
  - Active-layer LOOP seam crossfade length and shape can be edited directly from the reused waveform preview.
- Notes:
  - Crossfade amount is per-layer.
  - Crossfade shape is also per-layer and defaults to linear on new sample load.
  - `Pod2` layer toggle switches which layer-owned value the preview edits.
  - Non-`LOOP` rows do not expose this focus target.

3. **Stage fields (`perform_adsr_stage_focus` + per-layer ADSR UI state)**
- Type: focusable stage editors
- Purpose: edit the currently focused stage for the active layer.
- Behavior:
  - In `LOOP` row:
    - `REnc` edits `A`, `D`, `S`, or `R` numeric values.
    - value ranges are `A/R = 1..1000 ms`, `D = 1..100`, `S = 0..100`.
  - In `ADSR` row:
    - `REnc` edits graph points.
    - `A`, `D`, and `R` move their x positions with the simulator’s minimum-gap constraints.
    - `S` edits sustain level `0..100`.
  - In `1SHOT` row:
    - `D` and `S` are disabled and rendered crossed out, matching the simulator.
- Result:
  - Bottom-strip focus, boxed values, and graph editing match the simulator.

4. **Layer toggle (`perform_layer`)**
- Type: toggle action
- Purpose: switch A/B layer.
- Behavior:
  - `kUiBtnPod2` toggles layer.
- Result:
  - Layer context updates, slot context syncs, and the shared header-flash timer is reused.
- Notes:
  - Same PERFORM pattern as ENGINE and KEYZONE.

#### Modifier Behavior
- `LShift` / `RShift`:
  - `LShift` continues to have no ADSR-specific behavior.
  - `RShift` only changes behavior when the LOOP wave preview is focused:
    - `RShift + REnc` edits per-layer seam crossfade shape
    - holding `RShift` temporarily swaps the focused LOOP preview overlay from shaded seam regions to the curve-display view
  - ADSR still returns early on generic `shift` (`LShift`) rather than adding a second modifier system.

#### Entry Behavior
- On screen enter:
  - `perform_adsr_stage_focus = A`
  - `perform_adsr_type_focus = false`
  - `perform_adsr_wave_focus = false`
  - active layer row initializes from existing `engine_play_mode[layer]`:
    - `0 -> 1SHOT`
    - `1 -> LOOP`
  - startup/default playback type comes from `engine_play_mode[layer]`, which now defaults to `LOOP`
  - focus is normalized so disabled stages are skipped

#### Render Notes
- Render mirrors the simulator layout closely:
  - upper-right micro header `adsr a/b`
  - centered `playback type` label / focus box
  - waveform box reused as the main ADSR canvas
  - bottom strip with `a d s r`
  - loop-mode numeric stage boxes
  - adsr-mode envelope graph, vertical stage guides, and stage-specific highlight treatment

### PerformEmphasis (`UiScreenId::PerformEmphasis`)
- Parent: PerformMenu
- Entered by: PerformMenu `EMPHASIS` + `kUiBtnExtEnc`.
- Exited by: `kUiBtnPodEnc` pop to PerformMenu.

#### Focusable Objects
1. **Row selector (`perform_emphasis_row`)**
- Type: row selector (`DRIVE`, `CUTOFF`, `RESO`)
- Purpose: choose editable emphasis field.
- Behavior:
  - `kUiEncPod` cycles 3 rows.
- Result:
  - Changes edit focus.
- Notes:
  - Row order matches the simulator knob order left-to-right.

2. **Gain field (`engine_gain_db[layer]`)**
- Type: numeric field
- Purpose: adjust drive in tenths of dB.
- Behavior:
  - `kUiEncExt` edits drive amount on `DRIVE` row.
  - while `DRIVE` is focused and `RShift` is held:
    - the knob label temporarily shows `odd` or `even`
    - `kUiEncExt` changes the active layer's drive mode instead of the drive amount
- Result:
  - Drive amount remains clamped `0..60` and drive mode remains clamped `odd/even`; both publish through the shared ENGINE layer param path.
- Notes:
  - Stored as tenths of dB (`0.0..6.0 dB`) to match the simulator display and knob sweep.
  - DSP now applies a stronger nonlinear taper internally so low settings stay subtle and the top of the knob reaches obvious saturation.
  - `odd` = symmetric saturation, `even` = asymmetric saturation.

3. **Filter cutoff field (`engine_filter_cutoff_hz[layer]`)**
- Type: mapped continuous field
- Purpose: adjust the lowpass cutoff for the active layer's summed output bus using ADSR-style nonlinear mapping.
- Behavior:
  - `kUiEncExt` edits on `CUTOFF` row.
- Result:
  - Params published.
- Notes:
  - Uses accelerated encoder timing.
  - Default startup value is fully open at `20 kHz` for both layers.

4. **Resonance field (`engine_filter_resonance[layer]`)**
- Type: normalized field
- Purpose: adjust resonance `0..1` for the active layer's summed output bus.
- Behavior:
  - `kUiEncExt` edits on `RESO` row.
- Result:
  - Params published.
- Notes:
  - Rendered as percentage-like value.

5. **Layer toggle (`perform_layer`)**
- Type: toggle action
- Purpose: switch active layer.
- Behavior:
  - `kUiBtnPod2` toggles layer.
- Result:
  - Layer context update, shared slot sync, and shared header invert flash.
- Notes:
  - Reuses the same `perform_layer` ownership and POD2 behavior as ENGINE, KEYZONE, and ADSR.

#### Screen Enter
- `PerformEmphasis_OnScreenEnter(...)` only marks the screen dirty.
- No EMPHASIS-specific reset or new runtime handoff is introduced on entry.
- Existing published values remain the source of truth when the page opens, matching the simulator behavior.

#### Render Notes
- Render matches the simulator layout intent:
  - upper-right micro header `emph a/b`
  - three centered knobs in `drive`, `cutoff`, `reso` order
  - value text above `drive` and `cutoff`
  - while `DRIVE` is focused and `RShift` is held, the left knob label swaps from `drive` to `odd` or `even`
  - focus styling by row:
    - `drive` uses solid label box
    - `cutoff` and `reso` use dotted label boxes

### PerformProcess (`UiScreenId::PerformProcess`)
- Parent: PerformMenu
- Entered by: PerformMenu `PROCESS` + `kUiBtnExtEnc`.
- Exited by:
  - `kUiBtnPodEnc` pops to PerformMenu when not in detail mode.
  - `kUiBtnPodEnc` (or `kUiBtnPod1`) exits detail mode when detail is active.

#### Focusable Objects
1. **Main cursor (`perform_process_main_cursor`)**
- Type: 6-position selector
- Purpose: choose `VOL A`, `VOL B`, or one of 4 FX slots.
- Behavior:
  - `kUiEncPod` cycles main cursor in non-detail mode.
- Result:
  - Selects volume block or FX lane.
- Notes:
  - FX lanes map through `perform_process_fx_order`.

2. **Volume block controls (VOL A / VOL B)**
- Type: level + mute controls
- Purpose: edit layer master level and mute state.
- Behavior:
  - `kUiEncExt` adjusts selected layer level `0..2` (accelerated).
  - `kUiBtnExtEnc` toggles mute/unmute.
  - `RShift + kUiBtnExtEnc` snaps to unity.
- Result:
  - Updates `engine_layer_master_level[layer]` and local mute/unmuted bookkeeping.
- Notes:
  - Encoder motion while muted is consumed but does not unmute.
  - Main-screen render is now simulator-style:
    - stacked left-side `A` / `B` knobs instead of placeholder blocks
    - dB-formatted value text above each knob
    - focused knob shows the existing PROCESS main cursor selection only; edit ownership is unchanged

3. **FX quick controls (non-detail mode)**
- Type: quick edit + entry action
- Purpose: quick-adjust selected FX primary parameter; enter detail.
- Behavior:
  - `kUiEncExt` edits quick parameter for selected lane (`sat_drive`/`lfo_depth`/`delay_mix`/`reverb_mix`).
  - `kUiBtnExtEnc` enters detail mode for selected lane.
  - `RShift + kUiEncExt` reorders FX lanes.
- Result:
  - Publishes FX values/order.
- Notes:
  - `perform_process_fx_cursor` tracks selected lane when main cursor is on FX.
  - Reorder is adjacent-swap and clamps at edges (no wrap).

4. **Detail mode parameter cursor (`perform_process_detail_param[cursor]`)**
- Type: per-FX parameter selector
- Purpose: choose parameter inside detail screen.
- Behavior:
  - `kUiEncPod` cycles selected parameter index.
- Result:
  - Moves parameter focus.
- Notes:
  - Param count: SAT 4, MOD 4, DELAY 5 (TIM/FBK/SPRD/MID/MIX), REVERB 5.

5. **Detail mode parameter edit (`kUiEncExt`)**
- Type: in-screen submenu edit
- Purpose: edit selected detail parameter.
- Behavior:
  - `kUiEncExt` edits selected detail field.
  - `kUiBtnPodEnc` or `kUiBtnPod1` returns to main PROCESS view.
- Result:
  - Param updates and publish.
- Notes:
  - Toggle/discrete states are encoder-driven in detail mode.

6. **FX detail submenu: SATURATION**
- Type: in-screen submenu
- Purpose: edit SAT-specific params.
- Behavior:
  - POD encoder selects param index.
  - EXT encoder edits drive/reso, bump/smpl, mix, mode.
- Result:
  - Updates SAT params (`sat_*`).
- Notes:
  - Bit mode uses 3-state reso selector (`CRUSH`, `STATIC`, `HISS`).

7. **FX detail submenu: MODULATION**
- Type: in-screen submenu
- Purpose: edit MOD-specific params.
- Behavior:
  - POD encoder selects param index.
  - EXT encoder edits depth/wow, speed/rate, mix, mode.
- Result:
  - Updates MOD params (`mod_*`, `lfo_depth`, `mod_rate_hz`, `tape_rate`).
- Notes:
  - Dual algorithm behavior (`mod_mode`).

8. **FX detail submenu: DELAY**
- Type: in-screen submenu
- Purpose: edit DELAY-specific params.
- Behavior:
  - POD encoder selects param index.
  - EXT encoder edits time, feedback, spread, MID (band-limit on delay tap for wet + feedback), mix.
- Result:
  - Updates DELAY params (`delay_*`).
- Notes:
  - MID sweeps HPF/LPF corners on the delayed signal only (wet path and feedback); 0% ≈ 20 Hz HPF / 20 kHz LPF, 100% ≈ 400 Hz HPF / 800 Hz LPF (12 dB/oct each).

9. **FX detail submenu: REVERB**
- Type: in-screen submenu
- Purpose: edit REVERB-specific params.
- Behavior:
  - POD encoder selects param index.
  - EXT encoder edits pre, damp, decay, direction, mix.
- Result:
  - Updates REVERB params (`reverb_*`).
- Notes:
  - Direction toggles `reverb_reverse`.
  - Phase A Baker reverb keeps the existing 5-field REVERB detail screen unchanged:
    - `Pre` = pre-delay amount into the tank
    - `Dmp` = high-frequency damping / tail softening inside the feedback loop
    - `Dcy` = overall tank decay / feedback amount
    - `Wet` = reverb wet mix
  - The current Baker backend is denser internally than the original 4-line version, but the visible REVERB controls and their meanings stay unchanged.
  - Phase A.1 refines `Dmp` so the full sweep is more evenly useful on bright material:
    - low `Dmp` stays brighter and livelier
    - high `Dmp` gets softer/darker without collapsing into a blunt treble cut
  - `DIR` remains published and visible for UI continuity, but Phase A does not add a reverse-reverb DSP path; toggling it is currently a safe placeholder/pass-through control.

#### Render Notes
- Main PROCESS view now matches the simulator layout intent:
  - top-right micro header `process`
  - left-side stacked A/B volume knobs with knob-hand render and side-letter focus labels
  - dB value display above the A/B knobs
  - existing right-side FX lane and in-screen detail rendering remain in place
- Navigation and behavior are unchanged:
  - same main cursor order: `VOL A -> VOL B -> FX lanes`
  - same quick FX edit, FX reorder, detail entry/exit, and parent-preview behavior
  - same existing Daisy PROCESS publish path and parameter ownership

10. **Layer toggle (`perform_layer`)**
- Type: toggle action
- Purpose: switch active layer context from PROCESS page.
- Behavior:
  - `kUiBtnPod2` toggles layer.
- Result:
  - Updates current slot and published engine-layer params.
- Notes:
  - Consistent PERFORM-wide behavior.

### SdBrowse (PerformEngine load path)
- Parent: PerformEngine
- Entered by: PerformEngine row `LOAD` + `kUiBtnExtEnc`.
- Exited by:
  - `kUiBtnPodEnc` back to PerformEngine.
  - After successful load request in perform-load mode, code pops browser automatically.

#### Focusable Objects
1. **WAV list cursor (`sd.menu.cursor`)**
- Type: dynamic file-browser row
- Purpose: pick WAV to load into target perform layer.
- Behavior:
  - `kUiEncPod` scrolls list.
  - `kUiBtnExtEnc` queues `LoadWavIndex` and sets loading status.
- Result:
  - Load request queued and perform flow returns toward Engine.
- Notes:
  - When in engine load flow, target layer metadata is staged before queueing load.
