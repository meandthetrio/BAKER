# TEST_MATRIX (ADSR_V2)

## Purpose

Use this file as the practical validation guide for ADSR_V2. Keep it focused on tests a human can actually run and trust.

- Use this doc for validation flow, pass/fail expectations, and regression coverage.
- Use `docs/FILE_MAP.md` for ownership and navigation.
- Use `docs/MILESTONE_STATE.md` for milestone status and proof history.

## Current Build Notes

- Root navigation starts at `Start` (main menu), not at HUD.
- `PROJECT SLOT`, `SAVE PROJECT`, and `LOAD PROJECT` live under Button1 Settings.
- Overlay rendering code exists, but the old dedicated overlay hotkey is not guaranteed on every branch.
- HUD, FX, MOD, MACRO, and SAMPLE EDIT may be route-dependent. If a route is not exposed in the current build, skip that check rather than inventing a path.
- Preset save/load beyond project-file save/load is still partial. Treat project save/load as the supported persistence path.

## How To Use This Doc

1. Run the build sanity check first.
2. Run the core real-time and UI checks before deeper feature coverage.
3. Run audio and storage checks only on routes the current build actually exposes.
4. Treat optional checks as regression coverage, not release gates, unless the current task explicitly targets them.

## Build / Boot Sanity

### Build
- Setup
  - Start from a clean build environment for the current branch.
- Action
  - Run `make -j4`.
- Pass
  - Build completes successfully and produces the expected firmware artifacts.
- Fail
  - Compile, link, or generated-artifact failures.

### Boot and idle sanity
- Setup
  - Flash the device and boot with normal controls connected.
  - Insert an SD card if you plan to run storage checks.
- Action
  - Let the unit idle at the `Start` screen, then navigate into one reachable screen and back.
- Pass
  - Device boots cleanly, draws a stable UI, and returns to the main menu without stalls.
- Fail
  - Blank screen, boot loop, obvious UI lockup, or immediate navigation failures.

### Input sanity
- Setup
  - Open any reachable screen where the encoder or buttons visibly change state.
- Action
  - Turn the encoder slowly and quickly. Tap and hold buttons. Alternate inputs rapidly.
- Pass
  - Inputs feel consistent and deterministic with no obvious misses or double-triggers.
- Fail
  - Repeated missed input, stuck state, or obvious event duplication.

## Core Real-Time / Threading Checks

### Update-rate and responsiveness sanity
- Setup
  - Connect MIDI if available.
  - If the current build exposes a diagnostics surface, keep it visible.
- Action
  - Mash buttons, spin encoders, and send dense MIDI notes and CC.
- Pass
  - UI remains responsive, control-rate behavior feels stable, and any exposed counters remain credible under load.
- Fail
  - Multi-second stalls, obviously broken counters, or repeated responsiveness collapse under normal stress.

### Event / handoff stress
- Setup
  - Connect a dense MIDI source such as an arp or repeated chord stream.
  - Open any reachable screen so UI traffic and MIDI traffic overlap.
- Action
  - Keep MIDI running while navigating, editing values, and entering/leaving screens.
- Pass
  - Audio remains stable, UI remains usable, and no obvious cross-thread corruption appears.
- Fail
  - Stuck notes, UI collapse, obvious torn state, or repeated glitches correlated with UI activity.

### Background work stays off the audio path
- Setup
  - Insert an SD card with enough data to make scan/load work visible.
- Action
  - Trigger a heavy task such as SD scan, WAV load, normalize, loop-find, or project save/load.
  - While it runs, keep navigating and optionally keep MIDI playing.
- Pass
  - Progress is visible, UI remains responsive, and audio does not obviously collapse during worker activity.
- Fail
  - UI freezes, task never resolves, or audio breaks in step with worker activity.

### Parameter smoothing sanity
- Setup
  - Load audible material and open a reachable screen with a smoothed parameter.
- Action
  - Sweep the parameter slowly and quickly while holding notes or feeding MIDI.
- Pass
  - Audible changes remain smooth with no zippering or obvious parameter desync.
- Fail
  - Stepping, clicks, or parameter behavior that disagrees with the UI.

## UI / Navigation Checks

### Router and back-stack sanity
- Setup
  - Start at `Start`.
- Action
  - Enter at least two reachable child screens and back out again.
- Pass
  - Navigation always returns to the correct parent and never strands the UI.
- Fail
  - Wrong-screen returns, blank routes, or inputs affecting non-active screens.

### List-screen behavior
- Setup
  - Open a reachable list screen, ideally `SD BROWSE` with many entries.
- Action
  - Scroll slowly and quickly, try the ends of the list, and select an item.
- Pass
  - Highlight, scroll window, and selected action stay aligned.
- Fail
  - Cursor desync, wrong-item activation, or unstable scroll behavior.

### Value-edit behavior
- Setup
  - Open any reachable screen that supports value editing.
- Action
  - Enter edit mode, change a value, commit once, and cancel once.
- Pass
  - Enter/commit/cancel behavior is reliable and footer or mode hints update coherently.
- Fail
  - Stuck edit mode, wrong-target edits, or commit/cancel behavior that does not match the displayed state.

### Layout and diagnostics sanity
- Setup
  - Visit at least three reachable screens with visibly different content.
- Action
  - Check header, body, and footer alignment. If overlay diagnostics are reachable, toggle them and confirm the screen recovers cleanly.
- Pass
  - Layout remains readable across screens and diagnostics do not corrupt navigation or rendering.
- Fail
  - Header/footer overlap, clipped body content, or diagnostics that strand the UI.

## Audio / Voice / Playback Checks

### Note-on / note-off path
- Setup
  - Load a sample and connect MIDI if available.
- Action
  - Play repeated notes, short taps, longer holds, and small chords.
- Pass
  - Note-on audibly triggers playback and note-off releases cleanly without stuck voices.
- Fail
  - Silent note-on with a loaded sample, stuck notes, or repeated click/pop failures.

### Voice stealing / polyphony stress
- Setup
  - Load a sample and send more notes than the available voice count.
- Action
  - Force steals with sustained dense note streams.
- Pass
  - The synth stays stable and stealing behavior is consistent rather than chaotic.
- Fail
  - Lockups, runaway CPU symptoms, or non-recovering stuck voices.

### PolyPorto gate and fallback sanity
- Setup
  - Enable EXPRESS and assign `POLYPORTO` to a row on the layer under test.
  - Prepare one run each with EXPRESS off, mod wheel unseen, mod wheel `<=63`, and mod wheel `>63`.
- Action
  - Play notes with and without an eligible source voice.
  - Repeat with no source, source outside `RANGE`, source older than `RELEASE`, and `LIMIT` already reached.
- Pass
  - EXPRESS off, unseen mod wheel, or mod wheel `<=63` all produce normal notes.
  - No valid source, `RANGE` block, `RELEASE` block, and `LIMIT` block all fall back to normal NoteOn without dropping the note.
- Fail
  - Silent note-ons, forced glide when gating should block it, or `LIMIT` preventing the note from starting at all.

### PolyPorto source selection
- Setup
  - Configure at least two eligible same-layer source voices with different pitches and start/release order.
  - Prepare one case where a nearby source is on the other layer.
- Action
  - Test `SOURCE=CLOSEST`, then `SOURCE=LATEST`.
  - Release one candidate and retry inside and outside the `RELEASE` window.
- Pass
  - `CLOSEST` picks the nearest eligible same-layer source.
  - `LATEST` picks the most recently triggered or released eligible same-layer source.
  - Cross-layer voices are ignored.
  - A recently released source works only inside the configured `RELEASE` window.
- Fail
  - Cross-layer borrowing, stale-source wins in `LATEST`, or released voices staying eligible past the window.

### PolyPorto borrow-source lifecycle
- Setup
  - Create a glide from one held note into a new note.
  - Prepare a full-pool case where the new PolyPorto voice must steal a non-source voice.
- Action
  - Release the source note while the new note is gliding or sustaining.
  - Force the full-pool stolen-allocation case and repeat.
- Pass
  - The source voice is used as pitch reference only.
  - Releasing the source note does not release the new gliding note.
  - In the full-pool case, the selected source is not stolen and the new note still glides correctly after a different victim is stolen.
- Fail
  - Source reuse/destruction, source note-off killing the new note, or full-pool allocation breaking the glide.

### Sample playback and edit sanity
- Setup
  - Load a WAV and enter SAMPLE EDIT if the current route exposes it.
- Action
  - Adjust trim, loop enable, and loop points. If available, run normalize or loop-find once.
- Pass
  - Playback reflects the edited region and worker-backed edits complete without destabilizing the system.
- Fail
  - Edit state does not audibly apply, worker operations wedge the UI, or loop behavior is clearly broken.

### Filter / pitch / envelope sanity
- Setup
  - Load a sample with clear transients or sustained content.
- Action
  - Sweep filter-related controls, play across pitch range, and compare short taps versus held notes.
- Pass
  - Pitch tracks plausibly, ADSR behavior is audible, and filter changes are stable and smooth.
- Fail
  - Broken pitch mapping, absent envelope behavior, or unstable filtering.

## Save / Load / Worker / SD Checks

### SD browse and WAV load
- Setup
  - Insert an SD card with multiple WAVs.
- Action
  - Scan the card, browse the list, and load at least one file.
- Pass
  - Scan populates the list, load completes, and the loaded sample becomes usable without UI collapse.
- Fail
  - Scan never populates, load partially applies, or the UI gets stuck in busy/error state.

### Background load under interaction
- Setup
  - Pick a larger WAV if available.
- Action
  - Trigger a load and keep navigating while the load runs.
- Pass
  - UI remains usable and the final handoff to the loaded sample completes coherently.
- Fail
  - The UI stalls until load completion or the loaded state is only partially applied.

### Project save/load core path
- Setup
  - Build a distinct state and pick a known project slot through Button1 Settings.
- Action
  - Save the project, change state, then load the same slot.
- Pass
  - Save/load status is surfaced clearly and the supported persisted state restores deterministically.
- Fail
  - Save/load never completes, status gets stuck, or restored state is obviously partial or corrupt.

### Project recall regression coverage
- Setup
  - Prepare at least one two-layer save and, when practical, one A-only save and one B-only save.
- Action
  - Save and reload each slot. Reboot-and-reload coverage is preferred for high-confidence regression checks.
- Pass
  - The currently supported project state restores honestly:
    - per-layer sample selection and edit state
    - ENGINE tune and keyzone state
    - ADSR submenu state
    - EMPHASIS state
    - currently supported PROCESS, macro, and mod-route state
  - Empty-slot load fails safely and does not partially apply state.
- Fail
  - Layer assignment swaps, partial restore, stale status screens, or empty-slot loads that mutate live state.

## Optional Advanced Regression Checks

Run these only when the build clearly exposes the route or when a current task specifically targets the area.

### MOD, macros, and performance controls
- Suggested coverage
  - LFO or ModEnv routed to pitch or filter
  - mod-route enable/amount changes
  - macro sweeps affecting multiple parameters together
- Pass
  - Audible modulation and macro changes apply immediately and remain stable under note playback.

### PROCESS reverb
- Suggested coverage
  - `Start -> Perform -> Process` path if present
  - `Wet`, `Pre`, `Dmp`, and `Dcy` sanity
- Pass
  - Controls still map to expected user-facing behavior and the tail remains stable.

### Keygroups, velocity layers, and parameter locks
- Suggested coverage
  - split-note behavior if multi-sample/keygroup routes are active
  - soft-versus-hard velocity response if the current build exposes it
  - sequencer-driven brightness changes when parameter locks are enabled
- Pass
  - The feature behaves consistently with its current supported scope.
- Note
  - These remain partial or route-dependent areas; do not overstate confidence if the build does not expose a clear validation path.

## Exit Criteria

This document is satisfied when:

- the build passes
- the device boots and navigates sanely
- core real-time and worker handoffs stay stable
- sample playback and project persistence work on the current supported routes
- optional checks are either passed, skipped with reason, or left clearly outside current branch scope
