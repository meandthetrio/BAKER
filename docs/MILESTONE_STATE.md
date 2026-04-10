# MILESTONE_STATE (ADSR_V2)

This file is the milestone/status ledger.

- Use it for current status, concise proof, and validation notes.
- Use `docs/FILE_MAP.md` for ownership and code navigation.
- Use `docs/TEST_MATRIX.md` for step-by-step validation flows.
- Prefer concise evidence over implementation tours.

## UI 0.x

### 0.0 — Fixed update rates + ownership
- Status: DONE
- Summary: Main-loop control scanning, UI logic, and UI rendering run on distinct, explicit update paths.
- Code proof: `main.cpp`, `controls.cpp`, `ui_logic.cpp`, `ui_render.cpp`.
- Runtime proof: reachable diagnostics surfaces show stable UI/control counters under normal stress.

### 0.1 — Input event plumbing
- Status: DONE
- Summary: Hardware input is queued and drained through the UI event path rather than directly mutating screens.
- Code proof: `controls.cpp`, `ui_input.h`, `ui_input.cpp`, `ui_logic.cpp`, `src/ui/ui_screen_registry.cpp`.
- Runtime proof: button and encoder traffic remains coherent under rapid input.

### 0.2 — Screen router / navigation model
- Status: DONE
- Summary: UI navigation uses an explicit router and back-stack model.
- Code proof: `ui_screens.h`, `app_state.h`, `src/ui/ui_router.cpp`, `src/ui/ui_screen_registry.cpp`, `ui_logic.cpp`.
- Runtime proof: current routes enter and exit child screens cleanly without stranding the user.

### 0.3 — Selection list widget
- Status: DONE
- Summary: Long-list selection and scrolling behavior is centralized and reusable.
- Code proof: `ui_list_menu.h`, `ui_list_menu.cpp`, `src/ui/ui_screen_hud.cpp`, `src/ui/ui_screen_browser.cpp`.
- Runtime proof: populated list screens keep highlight, scroll window, and selection behavior aligned.

### 0.4 — Value editor widget
- Status: DONE
- Summary: Value editing has explicit begin, edit, commit, and cancel behavior.
- Code proof: `ui_value_edit.h`, `ui_value_edit.cpp`, `src/ui/ui_screen_fx.cpp`, `src/ui/ui_screen_mod.cpp`, `params.cpp`.
- Runtime proof: editable screens enter and exit value-edit mode reliably without zipper noise.

### 0.5 — Page layout conventions
- Status: DONE
- Summary: Header, body, and footer layout helpers are shared across current UI screens.
- Code proof: `ui_layout.h`, `ui_layout.cpp`, `src/ui/ui_screen_hud.cpp`, `src/ui/ui_screen_browser.cpp`, `src/ui/ui_screen_fx.cpp`, `src/ui/ui_screen_mod.cpp`.
- Runtime proof: current screens keep titles, body content, and footer hints aligned consistently.

### 0.6 — Overlays / diagnostics
- Status: DONE
- Summary: Diagnostic overlay rendering and metric collection still exist, even if access can be branch-dependent.
- Code proof: `ui_overlay.h`, `ui_overlay.cpp`, `ui_logic.cpp`, `ui_render.cpp`, `app_state.h`.
- Runtime proof: when the current build exposes diagnostics, counters update live and recover cleanly to the active screen.

### 0.7 — Render budget rules
- Status: DONE
- Summary: UI rendering uses budgeted updates and paged transfer behavior instead of blocking redraws.
- Code proof: `ui_render.cpp`, `oled_pager.cpp`, `oled_pager.h`, `app_state.h`.
- Runtime proof: under load, UI may degrade gracefully but does not collapse into repeated multi-second stalls.

### 0.8 — UI-safe background work pattern
- Status: DONE
- Summary: scan/load/save/normalize-style jobs run through request and worker paths rather than in the audio callback.
- Code proof: `ui_requests.h`, `ui_requests.cpp`, `ui_worker.cpp`, `app_state.h`, `src/ui/ui_screen_browser.cpp`, `src/ui/project_actions.cpp`.
- Runtime proof: background work can progress while UI interaction remains usable.

## Sampler 1.x

### 1.0 — Event Queue (SPSC ring buffer)
- Status: DONE
- Summary: main-thread event production and audio-thread event consumption are separated by the SPSC queue.
- Code proof: `event_queue.h`, `main.cpp`, `voice_engine_events.cpp`, `app_state.h`.
- Runtime proof: dense MIDI stays stable and queue behavior remains credible under stress.

### 1.1 — Parameter Lane (safe shared params + smoothing)
- Status: DONE
- Summary: UI-facing edit targets publish across the main/audio boundary with smoothing at the audio block edge.
- Code proof: `params.h`, `params.cpp`, `main.cpp`, `ui_value_edit.cpp`, `src/ui/ui_screen_fx.cpp`, `src/ui/ui_screen_mod.cpp`.
- Runtime proof: audible parameter sweeps remain smooth during UI and MIDI activity.

### 1.2 — Fixed voice pool (no malloc)
- Status: DONE
- Summary: the voice engine uses a fixed preallocated voice pool rather than dynamic allocation.
- Code proof: `voice_engine.h`, `voice_engine.cpp`, `main.cpp`.
- Runtime proof: heavy note spam does not introduce allocation-style stalls or crashes.

### 1.3 — NoteOn/NoteOff end-to-end (test tone)
- Status: DONE
- Summary: Note events propagate from MIDI decode through queueing into sample playback and release handling.
- Code proof: `main.cpp`, `event_queue.h`, `voice_engine_events.cpp`, `voice_engine_voice_lifecycle.cpp`, `ui_worker.cpp`.
- Runtime proof: loaded samples play on note-on and release cleanly on note-off.

### 1.4 — Voice pool + stealing (Oldest Note)
- Status: DONE
- Summary: voice exhaustion falls back to deterministic oldest-note stealing.
- Code proof: `voice_engine.h`, `voice_engine.cpp`, `voice_engine_events.cpp`, `voice_engine_voice_lifecycle.cpp`.
- Runtime proof: over-capacity note streams stay stable and recover without stuck voices.

### 1.5 — Block-boundary handoff + shared-state safety
- Status: DONE
- Summary: audio-visible state changes are applied at controlled handoff points instead of arbitrary mid-block mutation.
- Code proof: `main.cpp`, `voice_engine_events.cpp`, `params.cpp`, `app_state.h`, `ui_worker.cpp`.
- Runtime proof: MIDI, UI edits, and worker-backed sample/edit handoffs remain stable when overlapped.

## Sampler 2.x

### 2.0 — Sample Container Stub (one embedded sample, no SD yet)
- Status: PARTIAL
- Summary: embedded-sample plumbing exists, but this milestone remains broader than the current embedded-only proof.
- Code proof: `sampler_sample.h`, `embedded_sample.h`, `embedded_long_sample.h`, `main.cpp`, `voice_engine_events.cpp`, `voice_engine_render.cpp`.
- Runtime proof: embedded playback paths exist, but the milestone is still only partially proven as a general sample-container layer.

### 2.1 — Basic Sample Reader (linear interpolation)
- Status: DONE
- Summary: playback uses linear interpolation for fractional sample positions.
- Code proof: `voice_engine_playback.cpp`, `voice_engine_render.cpp`, `voice_engine.h`.
- Runtime proof: pitched playback behaves plausibly across a useful note range.

### 2.2 — Click-Free Start/Stop (short fades)
- Status: DONE
- Summary: note start and stop transitions use short fades to reduce clicks.
- Code proof: `voice_engine_playback.cpp`, `voice_engine_voice_lifecycle.cpp`, `voice_engine.cpp`, `voice_engine.h`.
- Runtime proof: short notes and quick retriggers avoid obvious start/stop pops.

### 2.3 — Pitching (MIDI note → playback rate)
- Status: DONE
- Summary: MIDI note and sample root-key state drive playback ratio.
- Code proof: `voice_engine_playback.cpp`, `voice_engine_voice_lifecycle.cpp`, `voice_engine_events.cpp`, `sampler_sample.h`.
- Runtime proof: interval and octave tests track with expected musical behavior.

### 2.4 — Per-Voice Amp Envelope (ADSR)
- Status: DONE
- Summary: each voice owns ADSR state and applies it inside render.
- Code proof: `voice_engine.h`, `voice_engine_playback.cpp`, `voice_engine_render.cpp`.
- Runtime proof: held notes and released notes expose distinct attack/decay/sustain/release behavior.

### 2.5 — Looping (forward loop + loop points)
- Status: DONE
- Summary: samples can loop through explicit loop-point state.
- Code proof: `sampler_sample.h`, `sample_edit.h`, `voice_engine_playback.cpp`, `voice_engine_render.cpp`.
- Runtime proof: sustained playback loops when enabled and responds to edited loop points.

### 2.6 — Per-Voice Filter
- Status: DONE
- Summary: voices apply a per-voice low-pass path driven by the shared parameter lane.
- Code proof: `voice_engine.h`, `voice_engine_render.cpp`, `params.cpp`, `main.cpp`.
- Runtime proof: cutoff changes remain audible and stable under polyphony.

### 2.7 — Mixer + Gain Staging (headroom rules)
- Status: DONE
- Summary: voice summing, clip counting, and master-level control enforce usable headroom.
- Code proof: `voice_engine_render.cpp`, `audio_engine.cpp`, `app_state.h`, `ui_overlay.cpp`.
- Runtime proof: nominal polyphony stays clean and clip counts only rise when intentionally driven.

### 2.8 — PROCESS Reverb (Phase A Baker late tank)
- Status: DONE
- Summary: the PROCESS reverb backend moved to the Phase A Baker-style tank while keeping the existing UI and parameter lane.
- Code proof: `audio_engine.h`, `audio_engine.cpp`, `params.cpp`, `src/ui/ui_screen_perform_process.cpp`.
- Runtime proof: `Pre`, `Dmp`, `Dcy`, and `Wet` keep their expected user-facing meaning.
- Scope note: reverse-reverb DSP and more advanced routing are still outside this phase.

### 2.8.1 — PROCESS Reverb (Phase A.1 voicing refinement)
- Status: DONE
- Summary: Phase A.1 refines voicing and damping behavior without changing UI scope.
- Code proof: `audio_engine.cpp`.
- Runtime proof: bright input produces a smoother tail and `Dmp` sweeps more musically.
- Scope note: no new modulation, size, or menu surface was added.

### 2.8.2 — PROCESS Reverb (Phase A.2 8-line tank)
- Status: DONE
- Summary: the late tank expands from 4 to 8 fixed lines for a denser field.
- Code proof: `audio_engine.cpp`, `audio_engine.h`.
- Runtime proof: repeated taps read less like a simple delay and remain stable at current polyphony targets.
- Scope note: the user-facing control surface remains intentionally unchanged.

### 3.0 — Keygroups / Zones
- Status: PARTIAL
- Summary: note-range sample selection exists, but editor/persistence scope is still incomplete.
- Code proof: `keygroups.h`, `keygroups.cpp`, `main.cpp`, `voice_engine_events.cpp`.
- Runtime proof: split-note behavior can be demonstrated with multiple embedded samples.

### 3.1 — Velocity Layers
- Status: PARTIAL
- Summary: velocity-dependent behavior exists, but not as a full per-layer sample architecture.
- Code proof: `velocity_layers.h`, `velocity_layers.cpp`, `main.cpp`, `voice_engine_events.cpp`, `voice_engine_voice_lifecycle.cpp`.
- Runtime proof: softer versus harder velocity can change response, but the broader milestone remains partial.

### 3.2 — Modulation Sources (LFO + extra envelopes)
- Status: DONE
- Summary: global LFO and mod-envelope sources feed the modulation matrix.
- Code proof: `mod_sources.h`, `mod_sources.cpp`, `mod_matrix.h`, `mod_matrix.cpp`, `voice_engine_render.cpp`, `params.cpp`.
- Runtime proof: routed modulation audibly affects pitch or filter behavior.

### 3.3 — Modulation Routing / Matrix
- Status: DONE
- Summary: enabled modulation routes publish and apply through the matrix path.
- Code proof: `mod_matrix.h`, `mod_matrix.cpp`, `app_state.h`, `src/ui/ui_screen_mod.cpp`, `voice_engine_render.cpp`.
- Runtime proof: route and amount edits apply immediately and audibly.

### 3.4 — Parameter Locks
- Status: PARTIAL
- Summary: runtime application exists, but authoring and broader lock coverage remain limited.
- Code proof: `plocks.h`, `plocks.cpp`, `ui_logic.cpp`, `voice_engine_render.cpp`.
- Runtime proof: sequencer-driven cutoff-lock behavior can be demonstrated, but the milestone remains partial.

### 3.5 — Performance Macros
- Status: DONE
- Summary: macro state can drive multiple parameters together through a shared smoothing path.
- Code proof: `macros.h`, `macros.cpp`, `src/ui/ui_screen_macro.cpp`, `voice_engine_render.cpp`, `main.cpp`.
- Runtime proof: macro sweeps move multiple audible targets together without destabilizing playback.

## Sampler 4.x

### 4.0 — SD / File Browser (load WAVs)
- Status: DONE
- Summary: SD browsing and WAV loading are routed through the request/worker path and applied at block boundaries.
- Code proof: `src/ui/ui_screen_browser.cpp`, `ui_requests.h`, `ui_requests.cpp`, `ui_worker.cpp`, `app_state.h`, `main.cpp`.
- Runtime proof: scans populate the browser and loaded WAVs become available without UI collapse.

### 4.1 — Background loading (main loop loads, audio keeps playing)
- Status: DONE
- Summary: large sample loads advance incrementally and publish only when ready.
- Code proof: `ui_worker.cpp`, `ui_requests.h`, `ui_requests.cpp`, `app_state.h`, `main.cpp`.
- Runtime proof: UI and audio remain usable while load progress advances.

### 4.2 — Sample edit (trim/loop/normalize/loop-find)
- Status: DONE
- Summary: trim/loop edits and worker-backed edit operations are wired into the live sample path.
- Code proof: `src/ui/ui_screen_browser.cpp`, `sample_edit.h`, `ui_requests.h`, `ui_worker.cpp`, `app_state.h`, `main.cpp`.
- Runtime proof: edited playback regions and worker edit operations behave coherently on currently exposed routes.

### 4.3 — Presets / Programs
- Status: PARTIAL
- Summary: project-file save/load is functional, but preset/program scope remains only partially implemented.
- Code proof: `ui_requests.h`, `ui_worker_project.cpp`, `project_manifest.h`, `app_state.h`, `src/ui/ui_screen_shift.cpp`, `src/ui/project_actions.cpp`, `src/ui/ui_screen_status.cpp`.
- Runtime proof: slot-tagged save/load status works and project-backed restore covers supported saved state.
- Scope note: `SavePreset` remains stubbed and is not an honest full preset/program implementation yet.

### 4.4 — Project Save/Load
- Status: PARTIAL
- Summary: project save/load is real and fairly broad, but the milestone remains partial because not every performance parameter is yet in manifest scope.
- Code proof: `ui_requests.h`, `ui_worker_project.cpp`, `project_manifest.h`, `app_state.h`, `src/ui/ui_screen_shift.cpp`, `src/ui/project_actions.cpp`, `src/ui/ui_screen_status.cpp`, `src/ui/ui_screen_perform_adsr.cpp`.
- Runtime proof: distinct slot saves restore supported per-layer sample/edit, ENGINE, KEYZONE, ADSR, EMPHASIS, PROCESS, macro, and mod-route state.
- Scope note: unsupported or out-of-manifest performance state should not be implied as restored.
