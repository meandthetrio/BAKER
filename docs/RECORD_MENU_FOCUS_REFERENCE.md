# RECORD MENU FOCUS REFERENCE

## Notes
- This file documents the CURRENT IMPLEMENTED RECORD branch only.
- Source of truth is code (`ui_screens.*`, `ui_logic.cpp`, `app_state.h`).
- RECORD is a single screen (`UiScreenId::Record`) with internal state-driven focus/actions.

## Screen Inventory
- Start (Main Menu entry point)
- Record

## UI Tree
- Start (Main Menu)
  - Record

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
  - `SAVE` queues `SaveRenderedWavCurrent` and enters `SaveWait`.
  - `RECORD AGAIN` returns to `SourceSelect`.
- Notes:
  - Save queue failure returns to `Review`.

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
