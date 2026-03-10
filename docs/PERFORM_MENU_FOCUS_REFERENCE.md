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
- Exited by: `kUiBtnPodEnc` pop to PerformMenu.

#### Focusable Objects
1. **Layer toggle (`perform_layer`)**
- Type: toggle action
- Purpose: switch layer context shown by screen.
- Behavior:
  - `kUiBtnPod2` toggles layer.
- Result:
  - Updates slot/published layer params.
- Notes:
  - No additional keyzone fields currently implemented.

### PerformAdsr (`UiScreenId::PerformAdsr`)
- Parent: PerformMenu
- Entered by: PerformMenu `ADSR` + `kUiBtnExtEnc`.
- Exited by: `kUiBtnPodEnc` pop to PerformMenu.

#### Focusable Objects
1. **Mode field (`engine_play_mode[layer]`)**
- Type: enum field (`ONESHOT` / `LOOP`)
- Purpose: choose playback mode.
- Behavior:
  - `kUiEncExt` toggles mode on odd-step movement.
- Result:
  - Publishes updated loop mode.
- Notes:
  - `perform_adsr_row` exists in state but current handler uses a single mode row.

2. **Layer toggle (`perform_layer`)**
- Type: toggle action
- Purpose: switch A/B layer.
- Behavior:
  - `kUiBtnPod2` toggles layer.
- Result:
  - Layer context updates.
- Notes:
  - Same PERFORM pattern.

### PerformEmphasis (`UiScreenId::PerformEmphasis`)
- Parent: PerformMenu
- Entered by: PerformMenu `EMPHASIS` + `kUiBtnExtEnc`.
- Exited by: `kUiBtnPodEnc` pop to PerformMenu.

#### Focusable Objects
1. **Row selector (`perform_emphasis_row`)**
- Type: row selector (`GAIN`, `FILT`, `RESO`)
- Purpose: choose editable emphasis field.
- Behavior:
  - `kUiEncPod` cycles 3 rows.
- Result:
  - Changes edit focus.
- Notes:
  - On screen enter, resonance is initialized to 50% for current layer.

2. **Gain field (`engine_gain_db[layer]`)**
- Type: numeric field
- Purpose: adjust gain in dB.
- Behavior:
  - `kUiEncExt` edits on `GAIN` row.
- Result:
  - Clamped `-32..+6`; params published.
- Notes:
  - Integer dB step behavior.

3. **Filter cutoff field (`engine_filter_cutoff_hz[layer]`)**
- Type: mapped continuous field
- Purpose: adjust cutoff using ADSR-style nonlinear mapping.
- Behavior:
  - `kUiEncExt` edits on `FILT` row.
- Result:
  - Params published.
- Notes:
  - Uses accelerated encoder timing.

4. **Resonance field (`engine_filter_resonance[layer]`)**
- Type: normalized field
- Purpose: adjust resonance `0..1`.
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
  - Layer context update.
- Notes:
  - Shared behavior across PERFORM screens.

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
  - Param count: SAT 4, MOD 4, DELAY 5, REVERB 5.

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
  - EXT encoder edits time, feedback, spread, freeze, mix.
- Result:
  - Updates DELAY params (`delay_*`).
- Notes:
  - Freeze is toggled by encoder movement.

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
