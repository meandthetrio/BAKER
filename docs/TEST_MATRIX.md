# TEST_MATRIX (ADSR_V2)

## 0.0 Scheduling stability / ownership
- Setup
  - Boot device with MIDI connected (optional but preferred).
  - Open HUD screen so `U:` and `C:` counters are visible.
- Actions
  - Mash buttons, spin encoders rapidly, send dense MIDI notes/CC.
- Expected results
  - UI Hz stays stable around target: ~60 (acceptable 30–60 if OLED load is heavy, but should not stall).
  - CTRL Hz stays near 1000 (should not drop to very low values or freeze).
  - No visible UI freezes or jitter bursts; controls feel consistent.
- Observe on OLED
  - HUD line: `U:.. C:....` (ui_screens.cpp `Hud_Render`).
  - Overlay line: `U:` / `C:` (ui_overlay.cpp).
- Fail conditions
  - C drops near 0 for sustained periods under load.
  - UI freezes/stalls for > ~0.5s during input stress.
  - Controls feel laggy/inconsistent despite stable counters.

## 0.1 Input event plumbing
- Setup
  - Open a screen where cursor moves or values change via encoder (HUD list, FX/MOD fields, or Sample Edit).
- Actions
  - Spin encoders slowly/quickly; tap/hold buttons; alternate rapidly; mash multiple inputs.
- Expected results
  - UI responds consistently (no missed steps, no double triggers).
  - Long-press behavior (if implemented) triggers reliably.
  - Under stress + MIDI, UI input still feels solid (no freezes).
  - UI input queue overflow stays at 0 (HUD `UIQO`, overlay `QO`), high-water may rise but should not climb indefinitely.
- Fail conditions
  - Missed inputs, stuck button state, repeated events.
  - UI stalls/freeze during heavy input.
  - UI input queue overflow increments (UIQO/QO > 0).

## 0.2 Screen router / navigation model
- Setup
  - Start on HUD (home) screen.
- Actions
  - Navigate into at least two screens (e.g., SD BROWSE → back; SAMPLE EDIT → back).
  - Toggle overlay (hold SHIFT) while on different screens.
- Expected results
  - Inputs affect only the active screen (no “ghost inputs” elsewhere).
  - Push/pop transitions are consistent and never strand the UI.
  - Overlay does not break navigation (returns cleanly to the same screen).
- Fail conditions
  - Back does nothing or returns to wrong screen.
  - Input affects non-active screens.
  - UI gets stuck on a blank screen or cannot return.

## 0.3 Selection list widget
- Setup
  - Open a list screen (SD BROWSE preferred with many WAVs; HUD menu also works).
- Actions
  - Scroll slowly and quickly through a long list.
  - Attempt to scroll past top and bottom (verify wrap or clamp behavior).
  - Select an item; verify it triggers the correct action (load or enter screen).
- Expected results
  - Cursor highlight always matches selected index.
  - Scroll window follows cursor correctly; no jumping/tearing.
  - At ends: behavior is consistent (wraps, as implemented).
  - No missed input steps or double increments under fast turns.
- Fail conditions
  - Highlight desync, wrong item selected, scroll offset bugs.
  - Crashes/freezes or stuck cursor.

## 0.4 Value editor widget
- Setup
  - Choose an audible parameter (e.g., FX LPF or delay mix) and open its screen (FX/MOD).
- Actions
  - Enter edit mode (EXT click), sweep encoder slowly and quickly.
  - Commit (EXT click) and cancel (POD2) at least once each.
  - While playing dense MIDI, repeat edits.
- Expected results
  - UI updates immediately; edit overlay and footer hints reflect mode.
  - Audio changes smoothly (no zipper/clicks) due to parameter smoothing.
  - Enter/exit is reliable; no stuck edit mode.
  - Parameter lane publish remains stable under load.
- Fail conditions
  - Audible zipper noise/clicks during edits.
  - UI value desync from audio behavior.
  - Stuck in edit mode / cannot cancel.
  - Parameter changes affect the wrong target.

## 0.5 Page layout conventions
- Setup
  - Visit at least three screens (HUD, SD BROWSE, FX, MOD, SAMPLE EDIT).
- Actions
  - Move between screens and observe header/body/footer positions.
  - Enter value edit mode and verify footer hints change.
  - Toggle overlay and ensure layout remains consistent.
- Expected results
  - Header title stays in the same place; no overlap with body content.
  - Footer hints consistently present and readable; reflect normal vs edit mode.
  - Body content stays within the body region (no clipping into header/footer).
- Fail conditions
  - Title overlaps list/fields or footer overwrites body content.
  - Inconsistent hint placement across screens.
  - Edit mode does not update footer hints or corrupts layout.

## 0.6 Overlay diagnostics
- Setup
  - Start on HUD, then navigate to SD BROWSE or FX.
- Actions
  - Toggle overlay from at least two different screens (hold SHIFT).
  - While overlay is visible, hammer inputs + send MIDI.
  - Trigger a load/save if you want to see SD/SAVE state update.
- Expected results
  - Overlay appears/disappears reliably and returns to same screen (no nav change).
  - Counters update live (UI/CTRL Hz, CPU, LATE, CLP, UIQO/QO, render stats, SD/SAVE).
  - Overlay does not cause noticeable UI stalls or audio issues.
- Fail conditions
  - Toggle unreliable, screen stuck, overlay breaks navigation.
  - Counters freeze or show impossible values.
  - Overlay causes UI freezes or raises audio late count significantly.

## 0.7 Render budget / partial redraw
- Setup
  - Make overlay visible to see render stats (render_ms/hi, skips, UI Hz, CTRL Hz).
- Actions
  - Stress UI: fast encoder + button mashing.
  - Stress MIDI: dense note stream.
  - Stress SD: enter SD browser with many WAVs, trigger scan/load if available.
- Expected results
  - ctrl_hz stays near 1000; UI stays acceptable (30–60 Hz).
  - render_skips may rise; render_hi_ms stays near/under budget most of the time.
  - No multi-second UI freezes; pages/bands update progressively rather than stalling.
- Fail conditions
  - ctrl_hz collapses during UI rendering.
  - UI freezes/stalls for > ~0.5–1s repeatedly.
  - render_skips grows rapidly at idle or render_hi_ms is consistently far above budget.

## 0.8 Background work (requests → results)
- Setup
  - Insert SD card with many WAVs (for scan) or at least one large file.
  - Open overlay so SD/SAVE lines are visible (recommended).
- Actions
  - Trigger a heavy task (Scan SD WAVs / Load WAV / Save Project / Normalize if present).
  - While it runs, navigate UI, scroll lists, and hammer inputs.
  - While it runs, play MIDI notes to confirm audio unaffected (optional).
- Expected results
  - UI remains responsive (no multi-second stalls); ctrl_hz stays ~1000.
  - Worker progress updates (SD ok/wavs/load and/or SAVE percent/state).
  - Task completes and results appear (list populated, sample loaded, project saved).
  - No queue overflow or error flags (unless SD missing).
- Fail conditions
  - UI freezes during heavy work.
  - ctrl_hz collapses or audio glitches correlate with background work.
  - Task never completes or results partially apply (“half loaded” state).

## 1.0 Event queue correctness / stress
- Setup
  - Connect a MIDI source (arp recommended).
  - If available, view queue counters on HUD/overlay; otherwise inspect `events_pushed/events_popped/queue_overflows` via debugger/temporary log, or rely on audio stability + LATE/CLP.
- Actions
  - Send rapid dense NoteOn/NoteOff (arp + chords).
  - Hold sustain / spam notes to force voice stealing.
  - While spamming MIDI, navigate UI to add contention.
- Expected results
  - Audio continues cleanly; no stalls.
  - If counters are available (HUD/overlay or debugger), PUSH/POP counts increase; OVF remains 0 in normal use.
  - Under extreme spam, OVF may increment but system stays stable and recovers (if counters are available).
  - LATE stays 0 or near 0; UI/CTRL Hz remain stable.
- Fail conditions
  - Audio glitches correlate with queue overflow or queue stalls.
  - If counters are inspected, PUSH grows but POP stops (consumer not draining).
  - If counters are inspected, POP exceeds PUSH (counter bug) or counters freeze unexpectedly.

## 1.1 Parameter Lane smoothing / safety
- Setup
  - Pick an audible parameter (LPF cutoff or delay mix) and start sound/MIDI.
- Actions
  - Enter edit mode (EXT click), sweep encoder slowly/quickly.
  - Commit and cancel at least once.
  - Repeat while sending dense MIDI and mashing UI controls.
- Expected results
  - No zipper noise / stepping artifacts.
  - UI remains responsive; CTRL Hz stable; LATE/CLP not worsened by edits.
- Fail conditions
  - Audible stepping/clicks during edits.
  - Audio instability correlated with edits.
  - Values desync between UI and audio behavior.

## 1.2 Voice pool determinism / no-malloc sanity
- Setup
  - Connect a MIDI source (arp recommended).
  - Optional: open overlay to watch CPU/LATE/CLP.
- Actions
  - Spam >10 NoteOn events repeatedly; hold large chords.
  - Keep sending notes while navigating UI.
- Expected results
  - System remains stable; no crashes or stalls.
  - Audio stays clean; no allocation-driven hiccups.
  - LATE stays low; UI/CTRL Hz remain stable.
- Fail conditions
  - Hard faults, freezes, or resets during note spam.
  - Consistent audio glitches correlated with note spam.

## 1.3 NoteOn/NoteOff end-to-end
- Setup
  - Ensure a sample is loaded (ProcessEvents early-outs if sample is null/empty).
  - Connect a MIDI source (arp recommended).
- Actions
  - Play repeated NoteOn/NoteOff; try rapid repeats and chords.
  - Try AllNotesOff if available.
- Expected results
  - Audible response on NoteOn; clean release/stop on NoteOff.
  - No stuck notes; clicks/pops are minimal or absent.
  - LATE stays low; system remains responsive.
- Fail conditions
  - No audio on NoteOn with a sample loaded.
  - Stuck notes after NoteOff.
  - Repeated clicks/pops or LATE spikes correlated with note events.

## 1.4 Voice stealing stress (Oldest Note)
- Setup
  - Load a sample; connect a MIDI source; max voices = 10.
- Actions
  - Play a chord larger than max voices.
  - Hold sustain and keep adding notes (force steals).
  - Repeat rapidly with arp + chords.
- Expected results
  - Voices cap at max; stealing occurs deterministically (oldest notes replaced).
  - No stuck voices after NoteOff / AllNotesOff.
  - No significant clicks beyond expected hard-steal or short crossfade behavior.
  - LATE/CLP remain stable.
- Fail conditions
  - Crash, lockups, runaway CPU, stuck notes, or non-deterministic stealing behavior.

## 1.5 Thread-safety / handoff stress
- Setup
  - Connect a MIDI arp; open FX/MOD so you can edit a smoothed param.
  - Optional: open overlay to watch LATE/CLP.
- Actions
  - Spam MIDI notes continuously.
  - While spamming, enter value edit and sweep parameters quickly.
  - Navigate screens during spam.
  - Trigger AllNotesOff (if available).
- Expected results
  - No stalls/glitches attributable to cross-thread contention.
  - Param changes are smooth (smoothing active).
  - No stuck notes; event processing remains stable.
  - LATE/CLP remain stable.
- Fail conditions
  - Audio glitches, LATE spikes correlating with UI edits, stuck notes, or evidence of torn/partial state.

## 1.6 PROCESS reverb Phase A.2 8-line tank
- Setup
  - Navigate to `START -> PERFORM -> PROCESS`.
  - Select the REVERB lane and enter the existing REVERB detail screen.
  - Load a sample with clear transients and hold repeated notes or a 5-voice chord.
- Actions
  - Set `Wet` to `0` and confirm dry-only output.
  - Raise `Wet`, then sweep `Pre` from low to high values.
  - Sweep `Dcy` from low to high while listening for tail length and stability.
  - Sweep `Dmp` from low to high while holding the same bright/plucky source material.
  - Toggle `DIR` once to confirm the UI/control path still behaves safely.
  - Repeat while playing 5 voices and while making moderate UI edits.
- Expected results
  - `Wet = 0` is audibly dry.
  - Bright plucks sound less like an exposed delay pattern than the earlier 4-line version.
  - The tail feels denser and smoother than the prior build.
  - `Pre` audibly moves the reverb onset later.
  - `Dcy` extends the tail without runaway, bursts, or self-oscillation.
  - `Dmp` audibly darkens and softens the later tail, and the useful change feels spread across the sweep rather than bunched at one end.
  - Existing REVERB detail UI and quick reverb wet control remain usable.
  - 5-voice playback remains stable enough for Phase A with no obvious zipper noise, glitches, or tail breakup.
  - No new ringing, crackle, or CPU-stress symptoms are introduced by the refinement.
  - `DIR` remains safe to toggle; in Phase A it may have no audible DSP change.
- Fail conditions
  - Wet leaks through at `Wet = 0`.
  - Bright plucks still read mainly as exposed internal repeats instead of a denser field.
  - Pre-delay is not audible or collapses into obvious combing/breakup.
  - Decay runs away, chatters, or dies abruptly at moderate settings.
  - Damp still feels bunched into a small part of the knob travel or only behaves like a blunt treble cut.
  - Polyphony stress causes repeatable glitches, zippering, unstable tails, new ringing, or obvious CPU regressions.

## 2.0 Sample container stub playback
- Setup
  - Default embedded sample (main.cpp sets `g_voice.SetSample(...)` at boot).
- Actions
  - Trigger NoteOn/NoteOff repeatedly (fast taps + sustain).
- Expected results
  - An embedded sample plays and retriggers reliably (recorded-sample proof is TBD).
  - No obvious clicks (click-free is 2.2; note if any are present).
- Fail conditions
  - Silence, wrong sound (still test tone), or unstable playback.

## 2.1 Interpolation correctness
- Setup
  - Use the default embedded sample; connect a MIDI source.
- Actions
  - Play notes across a wide pitch range (low and high).
  - Alternate rapidly between low/high notes.
- Expected results
  - Pitch shifts sound smooth (no obvious stair-step artifacts).
  - Playback remains stable across pitch changes.
- Fail conditions
  - Pitch sounds glitchy/aliased/steppy beyond normal bandwidth limits.
  - Playback breaks or becomes unstable when pitch changes.

## 2.2 Click-free start/stop
- Setup
  - Use a bright/transient sample if available (worst case for clicks).
- Actions
  - Rapid repeated NoteOn/NoteOff at multiple pitches.
  - Test very short notes and legato overlaps.
- Expected results
  - No audible clicks on onset or release.
- Fail conditions
  - Clicks/pops correlated with start/stop, especially on repeated taps.

## 2.3 Pitching
- Setup
  - Use a clearly pitched sample (looped tone preferred); connect a MIDI source.
- Actions
  - Play root note, octave above, fifth above, octave below.
  - Repeat quickly across notes to stress retriggering.
- Expected results
  - Octave sounds 2x frequency; fifth sounds ~3/2 (subjectively correct).
  - No glitches when changing notes quickly.
- Fail conditions
  - Wrong intervals, random pitch, or pitch changes affect the wrong voice.

## 2.4 ADSR envelope
- Setup
  - Use a sustained sample or tone so envelope shape is obvious (ADSR params are fixed defaults).
- Actions
  - Short tap (attack/decay behavior).
  - Hold then release (release tail).
  - Rapid repeated NoteOn/Off (no stuck voices).
- Expected results
  - Envelope behaves musically; release tails end cleanly; voices return to idle after release.
  - No clicks at transitions (if clicks occur, correlate with 2.2).
- Fail conditions
  - Stuck notes/voices, envelope never reaches zero, release doesn’t trigger, obvious clicks.

## 2.5 Looping
- Setup
  - Use a sample with a steady sustain region; enable loop points if available (embedded long sample has loop points).
- Actions
  - Hold a note past the original sample end; confirm it continues by looping.
  - Try extreme loop points (near start/end) to confirm clamping if the UI exposes them.
  - Rapid note retriggers while looping.
- Expected results
  - No end-of-sample cutoff while loop is enabled.
  - Loop boundaries behave correctly (no runaway, no silence unless intended).
- Fail conditions
  - Stops playing when it should loop.
  - Wrap glitches (stuck pos, NaNs, loud pops), loop points out of bounds.
  - Loop never disables / cannot exit.

## 2.6 Per-Voice Filter
- Setup
  - Use a harmonically rich sample (saw/bright loop); ensure LPF cutoff can be edited in UI.
- Actions
  - Sweep cutoff slowly/quickly while holding a note.
  - Trigger multiple voices and sweep to confirm per-voice stability under polyphony.
  - Stress test: dense MIDI + cutoff automation.
- Expected results
  - Audible low-pass behavior; stable output with no zipper noise or clicks.
  - No instability or runaway under polyphony.
- Fail conditions
  - Cutoff has no audible effect.
  - Runaway resonance/oscillation/NaNs, loud DC, or instability.
  - Filter state appears shared incorrectly across voices.

## 2.7 Mixer/Gain Staging Stress
- Setup
  - Open overlay to watch CLP/LATE and voices active.
  - Use a loud/bright sample; ensure master level/sat are at nominal values.
- Actions
  - Trigger 1, 4, 8, 10 voices (max 10); listen for crunch.
  - If a user gain control exists, intentionally overdrive it to force CLP.
- Expected results
  - Clean mix under normal gain; CLP stays ~0.
  - CLP increments only when intentionally driven.
  - LATE remains stable during mixing.
- Fail conditions
  - Distortion at moderate voice counts with nominal gain.
  - CLP climbing unexpectedly at idle/low levels.
  - LATE increases due to mixing.

## 3.0 Keygroup Mapping Test
- Setup
  - Ensure at least two samples are available (embedded sample bank in main.cpp).
  - Use the current hardcoded split (0–59 → sample 0, 60–127 → sample 1).
- Actions
  - Play notes below the split and above the split.
- Expected results
  - Low range triggers sample 0; high range triggers sample 1.
- Fail conditions
  - Wrong mapping, gaps, overlaps, or stuck note routing.

## 3.1 Velocity Layer Test
- Setup
  - Use a sample where brightness changes are audible; current layer split is `vel < 64` vs `>= 64`.
- Actions
  - Play the same note at low velocity and high velocity.
- Expected results
  - Audible brightness/character difference (soft vs hard) if the sample exposes it.
  - TBD: different sample selection per layer (not implemented).
- Fail conditions
  - No response to velocity at all, or inconsistent layer behavior.

## 3.2 Modulation Sources Test
- Setup
  - Enable a mod route (LFO → FilterCutoff or Pitch) in the MOD screen.
- Actions
  - Toggle LFO wave, vary rate and depth while holding a note.
  - Trigger repeated notes to hear ModEnv (attack/decay) influence.
- Expected results
  - Audible periodic modulation; rate/depth changes behave smoothly.
  - ModEnv produces a transient modulation that decays after NoteOn.
- Fail conditions
  - No modulation effect, unstable/steppy modulation, or sudden jumps/drift.

## 3.3 Mod Matrix Routing Test
- Setup
  - Choose a source (LFO) and destination (FilterCutoff or Pitch) in the MOD screen.
  - Ensure route is enabled and amount is nonzero.
- Actions
  - Adjust amount; toggle enable; change destination; stress with MIDI notes.
- Expected results
  - Immediate audible effect; stable under load; no zipper/clicks; UI stays responsive.
- Fail conditions
  - Route changes don’t apply, apply late/unpredictably, or cause instability/stepping.

## 3.4 Parameter Locks Test
- Setup
  - Use a bright sample; ensure `seq_running` and `plock_apply_enabled` are on (defaults).
  - Note: step locks are hardcoded (alternating cutoff) and not UI‑editable yet.
- Actions
  - Hold a sustained note while the sequencer advances steps.
  - Stress with MIDI and UI navigation during playback.
- Expected results
  - Step‑synchronous cutoff changes (alternating bright/dark) with no drift.
  - No glitches/clicks beyond normal filter movement.
- Fail conditions
  - No step changes, late/unpredictable changes, stuck locks, or UI stalls.

## 3.5 Performance Macros Test
- Setup
  - Use a sustained note; enable SAT if you want to hear Drive changes.
  - Macro A is edited in the Macro screen (selection defaults to A).
- Actions
  - Sweep Macro A slowly and quickly; spam MIDI + UI while sweeping.
- Expected results
  - Multiple params move together (cutoff + LFO depth, and drive if SAT enabled) with smooth response.
  - Audio remains stable; LATE stays low; CLP only if intentionally driven.
- Fail conditions
  - Only one param changes, mapping inconsistent, audible artifacts, or UI stalls.

## 4.0 SD browse / load correctness
- Setup
  - SD card inserted with many WAVs + at least one large WAV.
- Actions
  - Enter SD BROWSE and confirm scan/list populates.
  - Scroll quickly through list.
  - Trigger load of an item.
  - While loading: hammer UI + optionally play MIDI.
- Expected results
  - UI remains responsive (no multi‑second stall), ctrl_hz stable.
  - SD status updates (OK/ER, wav count, load progress).
  - Loaded sample is reflected in current slot/published fields.
  - Audio remains stable (no LATE spikes correlated to SD load).
- Fail conditions
  - UI freezes, ctrl_hz collapses, audio glitches correlate with load.
  - Load never completes / partial state.
  - Request/worker overflow flags (if present) rise during normal use.

## 4.1 Background load under stress
- Setup
  - SD card with large WAV(s), MIDI source running.
- Actions
  - Start playing notes continuously (arp).
  - Trigger WAV load.
  - While loading: navigate UI, scroll list, toggle overlay.
- Expected results
  - Audio continues cleanly; LATE stays 0/near‑0; CLP unaffected.
  - ctrl_hz stays near 1000; UI remains usable.
  - Load progress updates and completes; new sample becomes active only after ready handoff.
- Fail conditions
  - Audio glitch/dropout during load.
  - ctrl_hz collapses.
  - Handoff happens mid‑load (“half loaded”).
  - Request/worker overflows during normal usage (if counters exist).

## 4.2 Sample edit correctness / stress
- Setup
  - Load a WAV; enter SAMPLE EDIT screen; overlay visible.
- Actions
  - Adjust trim start/end; verify playback region behavior.
  - Enable loop; adjust loop start/end; hold notes to audition looping.
  - Trigger Normalize; while running, hammer UI + play MIDI.
  - Trigger Loop Find; while running, hammer UI + play MIDI.
- Expected results
  - UI stays responsive; ctrl_hz stable; audio remains clean (LATE near 0).
  - Edits apply predictably (trim/loop).
  - Normalize/loop-find complete and update status.
- Fail conditions
  - Audible glitches during background ops.
  - Edits apply “half way” or desync.
  - UI freeze > ~0.5–1s repeatedly.
  - Operation never completes / status stuck.

## 4.3 Preset save/load correctness / stress
- Setup
  - SD inserted; overlay visible; known parameter positions.
  - Use Project Save/Load with a known selected project slot (preset save is stub).
- Actions
  - Change several params (FX mix, LPF, MOD routes, macro values).
  - Press Button1 to open Settings, navigate to `PROJECT SLOT`, choose a slot with EXT encoder, then navigate to `SAVE PROJECT` and trigger it.
  - Confirm the temporary project-status screen appears immediately, shows the selected slot + `ACTION: SAVE`, and updates live while saving.
  - While saving, hammer UI + play MIDI.
  - Change params to different values.
  - Press Button1 if needed to return to Settings, keep the same slot selected, navigate to `LOAD PROJECT`, and trigger it.
  - Confirm the temporary project-status screen appears immediately, shows the selected slot + `ACTION: LOAD`, and updates live while loading.
  - Verify values restore and WAV reloads, then dismiss the screen with REnc click back to Settings.
  - Repeat rapidly (save/load back-to-back).
- Expected results
  - No UI stall; ctrl_hz stable; audio stable (LATE near 0).
  - Save/load both open the temporary project-status screen from Settings immediately and keep it open until REnc click.
  - Save completes with `PNN SAVED`; load restores values deterministically from the selected slot.
  - No partial application (“half updated” params).
- Fail conditions
  - HUD still exposes a project control path, screen does not appear, auto-closes unexpectedly, REnc click does not dismiss it, load doesn’t restore, corrupted save, stuck busy, glitches during ops.

## 4.4 Project save/load correctness / stress
- Setup
  - SD inserted, overlay visible.
  - Make a distinct state: pick a sample, set trim/loop/gain, change MOD routes + macros.
- Actions
  - Press Button1 to open Settings, select `PROJECT SLOT`, set slot 1, and trigger `SAVE PROJECT`.
  - Confirm the temporary project-status screen appears immediately and shows slot 1 + SAVE action.
  - While saving: hammer UI, scroll menus, send dense MIDI.
  - Change state to something obviously different, use Settings to set slot 2, and trigger Save Project again.
  - Confirm the temporary project-status screen appears immediately and shows slot 2 + SAVE action.
  - Use Settings to load slot 1 and confirm slot 1 state returns.
  - Use Settings to load slot 2 and confirm slot 2 state returns.
  - Use Settings to select an unused slot and trigger Load Project.
  - Confirm the temporary project-status screen appears immediately, shows the selected slot + LOAD action, and updates to `PNN EMPTY` or equivalent visible empty-slot result.
  - Dismiss each project-status screen with REnc click back to Settings.
  - Repeat (save, change, load) multiple times.
- Expected results
  - No multi‑second stalls; ctrl_hz stable; audio stable (LATE near 0).
  - Save ends OK in the selected slot; load ends OK for populated slots.
  - State restores deterministically per slot (sample path + edit + macros + mod routes + seq flags).
  - Unused-slot load fails safely with visible `PNN EMPTY`.
  - Temporary project-status screen shows slot/action/live status and never auto-closes on its own.
  - No HUD path is needed for project save/load.
  - No “half restored” state.
- Fail conditions
  - UI freeze, stuck busy, corrupted saves, load restores only some fields, empty-slot load partially applies state, project-status screen is missing/stale, HUD still exposes project save/load, audio glitches.

## 4.5 Project recall restores A+B after reboot
- Setup
  - Boot device fresh.
  - Load known WAV A into ENGINE layer A.
  - Set ENGINE layer A `TUNE` to an obvious non-default value.
  - Load different WAV B into ENGINE layer B.
  - Set ENGINE layer B `TUNE` to a different obvious non-default value.
- Actions
  - Press Button1, choose the desired project slot, and trigger `SAVE PROJECT`.
  - Power cycle the device.
  - Press Button1, select the same project slot, and trigger `LOAD PROJECT`.
  - Enter ENGINE and inspect layer A and layer B.
- Expected results
  - WAV A returns in ENGINE layer A / slot 0.
  - WAV B returns in ENGINE layer B / slot 1.
  - Layer A `TUNE` shows and sounds like its saved value.
  - Layer B `TUNE` shows and sounds like its saved value.
  - Project load still uses the worker path and does not introduce audible glitches or multi-second UI stalls.
- Fail conditions
  - WAV A and WAV B collapse onto the same restored layer.
  - Layer A or layer B restores the wrong saved sample.
  - Tune values swap between layers or reset to default.
  - Either saved layer remains empty after project recall.
  - Normal non-project sample load behavior changes unexpectedly.

## 4.6 Project recall restores A-only after reboot
- Setup
  - Boot device fresh.
  - Load known WAV A into ENGINE layer A.
  - Set ENGINE layer A `TUNE` to an obvious non-default value.
  - Leave layer B empty.
- Actions
  - Press Button1, choose the desired project slot, and trigger `SAVE PROJECT`.
  - Power cycle the device.
  - Press Button1, select the same project slot, and trigger `LOAD PROJECT`.
  - Enter ENGINE and inspect layer A and layer B.
- Expected results
  - WAV A returns in ENGINE layer A / slot 0.
  - Layer A `TUNE` shows and sounds like its saved value.
  - Layer B stays empty unless it was loaded separately on purpose.
- Fail conditions
  - WAV A lands in layer B / slot 1 after reboot.
  - Layer A remains empty after project recall.
  - Layer A `TUNE` resets or restores to the wrong value.
  - Normal non-project sample load behavior changes unexpectedly.

## 4.7 Project recall restores B-only after reboot
- Setup
  - Boot device fresh.
  - Load known WAV B into ENGINE layer B.
  - Set ENGINE layer B `TUNE` to an obvious non-default value.
  - Leave layer A empty.
- Actions
  - Press Button1, choose the desired project slot, and trigger `SAVE PROJECT`.
  - Power cycle the device.
  - Press Button1, select the same project slot, and trigger `LOAD PROJECT`.
  - Enter ENGINE and inspect layer A and layer B.
- Expected results
  - WAV B returns in ENGINE layer B / slot 1.
  - Layer B `TUNE` shows and sounds like its saved value.
  - Layer A stays empty unless it was loaded separately on purpose.
- Fail conditions
  - WAV B lands in layer A / slot 0 after reboot.
  - Layer B remains empty after project recall.
  - Layer B `TUNE` resets or restores to the wrong value.
  - Normal non-project sample load behavior changes unexpectedly.

## ENGINE_0.1 — Load/Tune/Gain/OneShot
- Setup
  - Navigate `START -> PERFORM -> ENGINE` and select target layer (`A`/`B`) with POD1.
- Actions
  - Focus `LOAD`, press `RClick` to open SD browser, select a WAV.
  - Verify return to ENGINE and confirm sample name + waveform are shown.
  - Focus `TUNE`, turn R encoder to `+12`, then back to `0`.
  - Focus `GAIN`, turn R encoder to `-12dB`, then back to `0dB`.
  - Focus `MODE`, toggle `OneShot`/`Loop`.
- Expected results
  - SD select loads into selected layer; waveform + sample name render on ENGINE.
  - Tune and gain changes publish safely (no zipper/clicks).
  - Play mode toggles and affects looping behavior.
  - Overlay shows layer/sample/tune/gain/mode and worker state/progress.
- Fail conditions
  - Wrong layer is loaded, missing waveform/name, or navigation breaks.
  - Tune/gain edits cause audible zippering/clicks.
  - MODE toggle has no effect on looping behavior.
  - Overlay fields do not match current ENGINE state.
