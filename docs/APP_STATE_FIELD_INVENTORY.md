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
| `engine` | `app_state_engine.h` | app-owned editor state | sample/editor metadata, perform editor state, keyzone/ADSR/process editor state | audio-thread runtime voice state, publish/apply atomics |
| `diag` | `app_state_diagnostics.h` | diagnostics only | overlay state, render timing, debug counters, audio/runtime instrumentation | functional UI flow, worker/project state, shared edit/publish state |
| `shared` | `app_state_shared.h` | sensitive publish/apply and handoff | sample publish/apply chain, recording handoff, modulation/lock/macro publish state, audio-facing snapshots | shell/navigation state, project slot/status state, worker queue bookkeeping |

## High-sensitivity boundaries

### UI browser state vs. shared sample state

- `ui.sd` is user-visible browser/load/save UI state on the main thread.
- `shared.sd_slots`, `shared.sd_current_slot`, and the `shared.sd_*gen/ready` fields are the publish/apply boundary that feeds audio-visible sample state.
- `shared.sd_edit_*` is the sample-edit publish/apply chain and must stay visually separate from `ui.sd`.

### Recording UI vs. recording handoff

- `recording.*` is screen and lifecycle state for the record flow.
- `shared.rec_*` is the cross-thread request/live-meter/apply path used by main and audio code.
- These two groups are related, but they are not the same ownership class.

### Project vs. worker

- `project.*` owns slot selection, save/load intent, status text, and pending restore edits.
- `worker.*` owns queue/progress/job execution bookkeeping.
- Worker code touches project state during save/load, but that does not make the project slot/status chain worker-owned.

### Engine editor vs. shared publish state

- `engine.*` is app-owned editor and perform state.
- `shared.mod_*`, `shared.plocks`, `shared.plock_pattern`, `shared.lfo_wave`, and `shared.macro_*` participate in publish/apply paths that feed runtime behavior.
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
  `app_state_engine.h`
- Overlay/debug counter issues:
  `app_state_diagnostics.h`
- Publish/apply, audio-facing handoff, and cross-thread state issues:
  `app_state_shared.h`

## Notes

- `app_state.h` is now intentionally structural and should stay that way.
- If a future field move changes ownership boundaries, update this file and the owning header comments together.
