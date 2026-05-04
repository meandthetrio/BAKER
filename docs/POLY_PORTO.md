# PolyPorto Feature

## Goal

PolyPorto is borrow-source polyphonic glide.

- A new note may borrow a starting pitch from a nearby held or recently released source voice.
- The source voice is only a pitch reference.
- The new note owns its own voice lifecycle.

## User-facing controls

Documented final detail-page controls:

TIME:
- range: 100-1000 ms
- step: 10 ms
- default: 150 ms
- meaning: glide time from borrowed source pitch to target note pitch

RANGE:
- range: 1-7 semitones
- step: 1 semitone
- default: 7 semitones
- meaning: maximum pitch distance between new note and source voice

SOURCE:
- options: Closest / Latest
- default: Closest
- Closest means smallest absolute semitone distance to the new note
- Latest means most recently triggered/released eligible source

RELEASE:
- range: 100-1000 ms
- step: 100 ms
- default: 200 ms
- meaning: how long after NoteOff a released voice remains eligible as a PolyPorto source

LIMIT:
- range: 2-10 voices
- step: 1 voice
- default: 3
- meaning: maximum simultaneous active PolyPorto glide voices per layer
- if limit blocks glide, the new note must still play normally

## Most important behavior rules

1. PolyPorto source selection is separate from voice stealing.
2. A source voice is only a pitch reference.
3. The source voice must not be killed, released, stolen, reused, or retargeted just because it was used as a source.
4. The new voice must own its own NoteOn/NoteOff lifecycle.
5. Releasing the source note must not release the new gliding note.
6. If no valid source exists, the new note must play normally.
7. If RANGE blocks the source, the new note must play normally.
8. If RELEASE window blocks the source, the new note must play normally.
9. If LIMIT blocks PolyPorto, the new note must play normally.
10. PolyPorto must only consider same-layer voices.
11. PolyPorto must not cross from Layer A to Layer B.
12. PolyPorto must not add malloc, file I/O, logging, OLED, or any non-real-time work to the audio path.

## Source eligibility

A voice is eligible if:

- same layer
- not Idle
- not being stolen / not in StealFadeOut
- not in forced stop fade
- note distance from new note is <= RANGE
- voice is currently held/sustaining/playing OR was released within RELEASE ms
- voice is not disqualified by lifecycle state

## Source selection

- Closest mode picks smallest absolute semitone distance, with latest source/order id as tie-breaker.
- Latest mode picks most recent eligible source/order id, while still respecting RANGE and RELEASE.

## Limit behavior

- LIMIT only limits PolyPorto glide activation.
- LIMIT must never drop NoteOn.
- If LIMIT is reached, the new note plays normally without glide.

## Runtime ownership

- express_state.h:
  Owns PolyPorto constants, defaults, source-mode enum, and clamping helpers.

- app_state_engine.h:
  Owns editable UI/app state fields for each layer.

- params.h / params.cpp:
  Own published main-to-audio parameter lane fields.

- ui_screens.cpp:
  Publishes app-state PolyPorto values into Params.

- src/ui/ui_screen_perform_express.cpp:
  Owns the EXPRESS screen and PolyPorto detail-page rendering/editing.

- audio_callback.cpp:
  Consumes current Params values and calls VoiceEngine setters.

- voice_engine_poly_porto.cpp:
  Owns VoiceEngine-specific PolyPorto helper logic only:
  source eligibility, source selection, release-window handling, active glide counting, and borrow-source voice start.

- voice_engine_events.cpp:
  Owns normal NoteOn/NoteOff event flow and calls the PolyPorto start helper as part of NoteOn handling.

- voice_engine_render_voice.cpp:
  Owns per-sample/per-block voice rendering and should only contain PolyPorto rendering state updates if already required for glide.

- src/worker/project_manifest.h and project save/load files:
  Own persistence for PolyPorto settings only.

## Current implementation notes

Current repo-grounded state:

- Constants, defaults, source-mode enum, and clamping helpers live in `express_state.h`.
- Editable UI/app state lives in `AppEngineState::PerformExpressState` in `app_state_engine.h`.
- Published main-to-audio fields live in `params.h` / `params.cpp` and are filled from app state in `ui_screens.cpp`.
- The EXPRESS detail page lives in `src/ui/ui_screen_perform_express.cpp` and currently exposes 5 PolyPorto rows: `TIME`, `RANGE`, `SRC`, `REL`, `LIMIT`.
- Audio-thread consumption and per-layer setter calls live in `audio_callback.cpp`.
- `VoiceEngine` PolyPorto setters, source eligibility/selection, active-glide counting, release-window handling, source-order tracking, and PolyPorto note start helper live in `voice_engine_poly_porto.cpp`.
- PolyPorto NoteOn entry lives in `voice_engine_events.cpp`.
- Base voice start/release interaction lives in `voice_engine_voice_lifecycle.cpp`.
- Render-side glide-state commit/update and steal-fade completion live in `voice_engine_render_voice.cpp`.
- Persistence for all five PolyPorto settings lives in `src/worker/project_manifest.h`, `src/worker/ui_worker_project.cpp`, `src/worker/ui_worker_project_load.cpp`, and `src/worker/ui_worker_project_manifest.cpp`.

Current runtime behavior facts:

- PolyPorto is borrow-source, not destructive source reuse.
- `TryStartPolyPortoVoice_()` selects a same-layer source voice as pitch reference only, then starts a separate new voice.
- The selected source voice is excluded from the steal victim search during PolyPorto allocation.
- If allocation must steal, the new PolyPorto voice can still glide from the selected source while a different victim voice is crossfaded out.
- `LIMIT` only gates PolyPorto glide activation. If no valid glide start is allowed, normal NoteOn continues through the regular allocation path.
- Source eligibility is same-layer only, range-limited, and release-window-limited. Layer A and Layer B do not cross.
- `SOURCE=CLOSEST` uses absolute pitch distance with source-order tie-break.
- `SOURCE=LATEST` uses `poly_porto_source_order`, which is updated on musical start/release boundaries only; redundant held/released source marking does not refresh order.
- A released source remains eligible only while its release age is within the configured `RELEASE` window.
- The audio callback enables PolyPorto only when EXPRESS is enabled, mod wheel has been seen, mod wheel value is `> 63`, and a row on that layer owns the PolyPorto target.
- The audio-path implementation stays inside fixed-size voice state and simple scans; it does not add malloc, file I/O, logging, or OLED work to the render path.

## Test matrix

- EXPRESS off: PolyPorto does nothing and normal notes play.
- EXPRESS on, PolyPorto target assigned, mod wheel not seen: normal notes play.
- Mod wheel seen but <=63: normal notes play.
- Mod wheel >63 and source within RANGE: new note glides from source.
- Source outside RANGE: new note plays normally.
- Released source within RELEASE window: new note glides.
- Released source after RELEASE window: new note plays normally.
- LIMIT reached: new note plays normally without glide.
- SOURCE=CLOSEST chooses closest eligible source.
- SOURCE=LATEST chooses latest eligible source.
- Layer A source must not affect Layer B.
- Layer B source must not affect Layer A.
- Releasing the source note must not release the new gliding note.
- Project save/load preserves TIME, RANGE, SOURCE, RELEASE, LIMIT.
