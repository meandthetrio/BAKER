# APP_STATE_FIELD_INVENTORY.md

## Purpose

Ownership map for the split `AppState` layout.

This doc is no longer a flat-member inventory. The source of truth is now the domain headers included by `app_state.h`, and this file explains which ownership boundary each domain represents.

## Current AppState composition

| Domain member | Header | Ownership | What lives there | What stays out |
| --- | --- | --- | --- | --- |
| `ui` | `app_state_ui.h` | main-thread UI | shell/navigation state, input plumbing, HUD/value-edit helpers, SHIFT/delete flow, SD browser UI state | project coordination, worker job bookkeeping, audio-visible handoff state |
| `project` | `app_state_project.h` | main-thread project flow | slot selection, save/load action state, status text, pending edit restore state | worker queue/progress state, shared publish/apply fields |
| `worker` | `app_state_worker.h` | main-loop worker orchestration | request queue, active job type, progress, result, work-unit bookkeeping | project slot/status chain, UI browser state |
| `recording` | `app_state_recording.h` | main-thread recording UI | record screen lifecycle, source/target selection, preview gate/hold, countdown state | cross-thread record requests, live waveform buffers |
| `engine` | `app_state_engine.h` | app-owned editor state | sample/editor metadata, perform editor state, keyzone/ADSR/process editor state (see nested groups below) | audio-thread runtime voice state, publish/apply atomics |
| `diag` | `app_state_diagnostics.h` | diagnostics only | overlay state, render timing, debug counters, audio/runtime instrumentation | functional UI flow, worker/project state, shared edit/publish state |
| `shared` | `app_state_shared.h` | sensitive publish/apply and handoff | `SampleSharedState`, recording bridge, `PerformanceSharedState` (see nested groups below) | shell/navigation state, project slot/status state, worker queue bookkeeping |

## Nested layout: `AppEngineState` (`app.engine`)

`AppEngineState` groups editor/perform fields under named substructs (member names in parentheses):

| Group | Member | Role |
| --- | --- | --- |
| Layer sample/editor metadata | `layer` | per-layer tune, gain, drive/play mode, sample path/name, load target, applied-gen tracking, header invert timer |
| Perform navigation | `perform_nav` | perform menu index, layer, engine/emphasis row cursors |
| Wave edit | `wave_edit` | wave-edit cursor, `SampleEdit` entries, has-entry flag |
| Keyzone | `keyzone` | per-layer lo/hi note, marker focus, window octave |
| ADSR | `adsr` | ADSR rows, focus flags, loop and envelope parameters per layer |
| Process / FX | `process` | FX cursor/order, main cursor, volume/mute/unmuted level, detail/EQ graph flags, detail params, FX/mod field cursors |

Field names inside each group remain the historical `engine_*` / `perform_*` prefixes from `app_state_engine.h`; access is `app.engine.<group>.<field>`.

## Nested layout: `app.shared.sample` (`SampleSharedState`)

`shared.sample` is a `SampleSharedState` wrapper with two child groups:

| Child | Member | Role |
| --- | --- | --- |
| Publish / audio-visible samples | `publish` | `sd_slots[]`, `sd_current_slot`, `sd_published_slot`, `sd_published_ready`, `sd_published_gen`, `sd_applied_gen` (atomics where declared) |
| Sample edit handoff | `edit` | `sd_edit_slots[]`, `sd_edit_pending`, `sd_edit_slot`, `sd_edit_ready`, `sd_edit_gen`, `sd_edit_applied_gen` |

## Nested layout: `app.shared.performance` (`PerformanceSharedState`)

`shared.performance` groups modulation, pattern locks, sequencer timing, and macros:

| Child | Member | Role |
| --- | --- | --- |
| Modulation | `modulation` | `mod_matrix`, `mod_routes_ui[]`, `mod_route_selected`, atomic `lfo_wave` |
| P-locks / pattern | `plocks` | `plocks` (`PLocksState`), `plock_pattern`, `plock_apply_enabled` |
| Sequencer transport | `sequencer` | `seq_running`, `seq_bpm`, `seq_last_ms`, `seq_accum_ms` |
| Macros | `macros` | `macro_ui`, `macro_a`, `macro_b`, atomic `macro_sel`, `macro_gen` |

## High-sensitivity boundaries

### UI browser state vs. shared sample state

- `ui.sd` is user-visible browser/load/save UI state on the main thread.
- `shared.sample.publish` holds `sd_slots`, `sd_current_slot`, and the published/applied slot + generation atomics (`sd_published_*`, `sd_applied_gen`) that form the publish/apply boundary for audio-visible sample state.
- `shared.sample.edit` holds the `sd_edit_*` sample-edit publish/apply chain and must stay visually separate from `ui.sd`.

### Recording UI vs. recording handoff

- `recording.*` is screen and lifecycle state for the record flow.
- `shared.recording.*` (struct `RecordingBridgeState`) is the cross-thread request/live-meter/apply path used by main and audio code.
- These two groups are related, but they are not the same ownership class.

### Project vs. worker

- `project.*` owns slot selection, save/load intent, status text, and pending restore edits.
- `worker.*` owns queue/progress/job execution bookkeeping.
- Worker code touches project state during save/load, but that does not make the project slot/status chain worker-owned.

### Engine editor vs. shared publish state

- `engine.*` is app-owned editor and perform state (subdivided under `layer`, `perform_nav`, `wave_edit`, `keyzone`, `adsr`, `process`).
- `shared.performance.modulation` (matrix, routes, `lfo_wave`), `shared.performance.plocks` (`plocks`, `plock_pattern`, apply flag), and `shared.performance.macros` participate in publish/apply paths that feed runtime behavior.
- The split keeps editor intent separate from handoff-sensitive runtime-facing state.

## Trace map

Use this map when debugging:

- UI navigation, focus, browser, and value-edit issues:
  `app_state_ui.h`
- Project save/load slot, status, and pending restore issues:
  `app_state_project.h`
- Worker job progress and request lifecycle issues:
  `app_state_worker.h`
- Recording screen lifecycle issues:
  `app_state_recording.h`
- Perform/editor state issues:
  `app_state_engine.h` (nested groups on `AppEngineState`)
- Overlay/debug counter issues:
  `app_state_diagnostics.h`
- Publish/apply, audio-facing handoff, and cross-thread state issues:
  `app_state_shared.h` (`SampleSharedState`, `RecordingBridgeState`, `PerformanceSharedState`)

## Notes

- `app_state.h` is now intentionally structural and should stay that way.
- If a future field move changes ownership boundaries, update this file and the owning header comments together.
