# RECORD MENU FOCUS REFERENCE

## Notes
- This file documents the current RECORD and RECORD > RENDER branches.
- Source of truth is code (`src/ui/ui_screen_record.cpp`, `src/ui/ui_screen_record_event.cpp`, `src/ui/ui_screen_main.cpp`, `src/ui/ui_screen_project.cpp`, `src/ui/ui_router.cpp`, `src/ui/ui_logic.cpp`, `app_state.h`).
- Physical input recording still uses a single state-driven `UiScreenId::Record`.
- Render bounce uses dedicated nav-stack screens plus the shared rename grid.

## Screen Inventory
- Start (Main Menu entry point)
- Record
- RecordRenderMenu
- RecordRenderExecute
- RecordRenderReview
- RenameProject (shared grid, used for render WAV naming)

## UI Tree
- Start (Main Menu)
  - Record
  - RecordRenderMenu
    - RecordRenderExecute
    - RecordRenderReview
      - RenameProject

## Screen Reference

### Start (Main Menu, RECORD path)
- Parent: root
- Entered by: boot initialization and BACK pop from child screens.
- Exited by: `kUiBtnExtEnc` on selected row.

#### Focusable Objects
1. **Main menu RECORD row (`main_menu_index == 1`)**
- Type: menu item
- Purpose: enter RECORD branch.
- Behavior:
  - `kUiEncPod` selects RECORD.
  - `kUiBtnExtEnc` pushes `UiScreenId::Record`.
- Result:
  - Opens Record screen.
- Notes:
  - Same root menu as PRESETS/PERFORM.

### Record (`UiScreenId::Record`)
- Parent: Start (Main Menu)
- Entered by: Start RECORD row + `kUiBtnExtEnc`.
- Exited by: `kUiBtnPodEnc` BACK pop when current record state does not consume BACK.

#### Focusable Objects
1. **Source selector (`record_state == SourceSelect`, `record_source_index`)**
- Type: 2-option selector
- Purpose: choose source (`LINE IN` / `MICROPHONE`).
- Behavior:
  - `kUiEncPod` toggles source.
  - `kUiBtnExtEnc` enters `Armed`.
  - `kUiBtnPodEnc` pops back to Start.
- Result:
  - Sets record source and flow state.
- Notes:
  - Reset on `Record_OnEnter`.

2. **Armed action (`record_state == Armed`)**
- Type: action state
- Purpose: ready to start countdown.
- Behavior:
  - `kUiBtnExtEnc` starts `Countdown`.
  - `kUiBtnPodEnc` returns to `SourceSelect`.
- Result:
  - State transition.
- Notes:
  - Monitor is enabled while armed.

3. **Countdown state (`record_state == Countdown`)**
- Type: timed transition state
- Purpose: 4-second pre-roll (`kRecordCountdownMs`).
- Behavior:
  - Auto transitions to `Recording` when timer expires.
  - `kUiBtnPodEnc` can back out (not consumed in this state).
- Result:
  - Starts recording or exits branch.
- Notes:
  - Transition check is in `Record_Render()`.

4. **Recording controls (`record_state == Recording`)**
- Type: action state
- Purpose: capture audio to slot.
- Behavior:
  - `kUiBtnExtEnc` requests stop (`rec_stop_req`).
  - `kUiBtnPodEnc` also requests stop.
- Result:
  - Recording stop requested, then flow moves toward review when data exists.
- Notes:
  - Max recording length enforced by recording backend.

5. **Review controls (`record_state == Review`)**
- Type: playback/decision controls
- Purpose: audition captured audio and choose next action.
- Behavior:
  - `kUiBtnPod2` hold for preview gate; release stops preview.
  - `kUiBtnExtEnc` -> `TargetSelect`.
  - `kUiBtnPodEnc` -> `BackConfirm`.
- Result:
  - Preview control and state transitions.
- Notes:
  - Displays waveform or `NO AUDIO`.

6. **Target selector (`record_state == TargetSelect`, `record_target_index`)**
- Type: 2-option selector
- Purpose: choose `SAVE` or `RECORD AGAIN`.
- Behavior:
  - `kUiEncPod` toggles option.
  - `kUiBtnExtEnc` confirms option.
  - `kUiBtnPodEnc` returns to `Review`.
- Result:
  - `SAVE` queues `SaveRenderedWavCurrent`, writes the WAV to SD root, and enters `SaveWait`.
  - `RECORD AGAIN` returns to `SourceSelect`.
- Notes:
  - Save queue failure returns to `Review`.
  - `/WAV` is not used or required for microphone saves.

7. **Back confirm (`record_state == BackConfirm`)**
- Type: confirmation state
- Purpose: confirm discard of take.
- Behavior:
  - `kUiBtnExtEnc` confirms discard and returns to `SourceSelect`.
  - `kUiBtnPodEnc` cancels and returns to `Review`.
- Result:
  - Either discard flow or return to review.
- Notes:
  - Prompt includes `REC WILL BE LOST`.

8. **Save wait (`record_state == SaveWait`)**
- Type: progress/result state
- Purpose: wait for async save completion.
- Behavior:
  - No cursor/field controls.
  - Auto transition when worker is done.
- Result:
  - Success -> `SourceSelect`; failure -> `Review`.
- Notes:
  - Reads `sd.save_in_progress`, `sd.save_status`, and request busy state.

### RecordRenderMenu (`UiScreenId::RecordRenderMenu`)
- Parent: Start (Main Menu) via `SAMPLES -> RECORD -> RENDER`.
- Purpose: configure render-note preview and launch the internal 5-second bounce.

#### Focusable Objects
1. **Note row (`record_render_focus == 0`)**
- `kUiEncExt` changes render MIDI note offset.
- `kUiBtnPod2` previews the selected note on eligible loaded layers.

2. **Hold row (`record_render_focus == 1`)**
- `kUiEncExt` changes `record_render_hold_ms`.
- This remains preview/render gate length only; it does not change capture length.

3. **Execute row (`record_render_focus == 2`)**
- `kUiBtnExtEnc` starts the internal render if recording/worker/load guards are clear.
- Guard failure leaves the user on this screen and shows `REC BUSY` or `BUSY`.

### RecordRenderExecute (`UiScreenId::RecordRenderExecute`)
- Parent: `RecordRenderMenu`
- Purpose: active internal bounce capture.
- Behavior:
  - Arms internal render capture into `SdRecordBuffer()`.
  - Sends `AllNotesOff`, then schedules NoteOn at capture start + 2 ms.
  - Schedules NoteOff at `2 ms + record_render_hold_ms`.
  - Captures post-FX left output for a fixed 5.000 seconds.
  - `kUiBtnPodEnc` BACK is ignored while capture is active.
- Result:
  - Successful capture transitions to `RecordRenderReview`.
  - Zero-length capture returns to `RecordRenderMenu` with `NO AUDIO`.

### RecordRenderReview (`UiScreenId::RecordRenderReview`)
- Parent: `RecordRenderMenu`
- Purpose: audition the temporary render and decide whether to save or discard it.

#### Focusable Objects
1. **Waveform review**
- Displays `shared.recording.rec_sample` with the shared waveform preview renderer.
- `kUiEncPod` can focus the waveform review itself.
- Focused waveform inverts as an enterable trim target.
- `kUiBtnExtEnc` on focused waveform opens the shared trim screen used by `PerformWaveEdit`.

2. **Preview trigger**
- `kUiBtnPod2` plays the captured temporary render from frame 0.
- Re-pressing `kUiBtnPod2` restarts playback.

3. **Bottom action strip**
- `save` and `rerecord` are always visible at the bottom of the screen.
- `kUiEncPod` cycles focus `waveform -> save -> rerecord`.
- `kUiBtnExtEnc` confirms the focused action.

4. **Trim editor**
- When entered from render review, the trim screen edits `shared.recording.rec_edit`.
- `kUiBtnPodEnc` cancels trim changes and returns to review.
- `kUiBtnExtEnc` commits trim changes and returns to review.
- Render WAV save uses the committed trim window only.

5. **Back / discard**
- `kUiBtnPodEnc` is consumed and does nothing on review.

### RenameProject (`UiScreenId::RenameProject`, render-save mode)
- Parent: `RecordRenderReview`
- Purpose: shared grid UI for naming the unsaved rendered WAV.
- Behavior:
  - Uses WAV stem limits, not project-name limits.
  - `kUiBtnExtEnc` on `save` queues named render save.
  - Existing visible filename match shows `NAME EXISTS` and stays on this screen.
  - `kUiBtnPodEnc` with empty draft, or `cancel`, returns to `RecordRenderReview` and keeps the temp render.
- Result:
  - Save success returns to `RecordRenderMenu` after writing the WAV to SD root.
  - Save failure after worker start returns to review with the temp render preserved.
  - Failed saves should not leave a partial WAV on the card.
  - Saved renders stay unclassified by default; sample style is managed later in `SD MANAGER` via the hidden filename suffix feature.
