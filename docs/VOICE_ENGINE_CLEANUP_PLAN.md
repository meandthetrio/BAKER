# VOICE_ENGINE_CLEANUP_PLAN

## Purpose

This document defines a surgical, extraction-first cleanup plan for `voice_engine.cpp`.

The goal is to improve readability for humans, Codex, and ChatGPT without changing runtime behavior, the audio-thread contract, or the current ownership model.

This cleanup is not a rewrite. It is a narrow file-boundary and helper-boundary cleanup intended to make the engine code easier to navigate, reason about, and modify safely.

---

## Core goal

Split `voice_engine.cpp` by responsibility while preserving:

- `VoiceEngine` as the owning class
- the current public API
- the current real-time threading contract
- current behavior and sound
- current allocation model
- current event flow
- current state layout unless a step explicitly allows otherwise

---

## Current responsibility clusters in `voice_engine.cpp`

The file currently mixes several distinct mental layers:

1. sample playback / loop math
2. envelope helpers
3. per-layer emphasis / filter support
4. voice allocation / stealing / lifecycle
5. event handling
6. modulation / macros / plocks
7. full block rendering

That concentration is the main readability problem.

---

## Target end state

Keep:

- `voice_engine.h`
- a small `voice_engine.cpp` only if needed for shared glue, constructor/init, or minimal top-level ownership

Introduce responsibility-focused implementation files:

- `voice_engine_playback.cpp`
- `voice_engine_emphasis.cpp`
- `voice_engine_voice_lifecycle.cpp`
- `voice_engine_events.cpp`
- `voice_engine_render.cpp`

If internal declarations become shared across translation units, add a narrow internal header such as:

- `voice_engine_internal.h`

---

## Cleanup strategy

Sequence matters.

The safest order is:

1. extract stateless helpers first
2. extract small isolated member helpers next
3. extract lifecycle/allocation before event dispatch
4. extract event handling after its dependencies are already moved
5. decompose `RenderBlock()` into internal helper phases before moving it to its own file

This keeps the highest-risk logic stable until the surrounding context is cleaner.

---

## Stage 0 — Freeze behavior and define extraction-only scope

### Goals

- Establish that this pass is extraction-only.
- Define the intended file split before implementation begins.
- Avoid opportunistic behavior changes during movement.

### Tasks

- Create or update this cleanup plan.
- Create or update `VOICE_ENGINE_EXTRACTION_RULES.md`.
- Ensure future prompts explicitly say extraction-only.
- Keep compile/test discipline after every step.

### Done when

- The plan and rules documents exist and reflect the intended file ownership.
- The next cleanup prompt can refer to this document as the source of truth.

---

## Stage 1 — Extract stateless playback, loop, and envelope helpers

### Goals

Move the top-of-file stateless helpers out of `voice_engine.cpp` first.

These are the safest extractions because they already behave like isolated implementation helpers.

### Move to `voice_engine_playback.cpp`

#### Envelope helpers
- `InitEnvelope`
- `SetEnvelopeRelease`
- `StepEnvelope`

#### Playback / loop helpers
- `ComputeRatio`
- `ComputeFadeStepMs`
- `ComputeFadeStep`
- `ComputeLoopBoundaryFade`
- `AdvancePos`
- `SampleAtLinear`
- `SampleAtLinearRegion`
- `ComputeLoopSeamCrossfadeFrames`
- `ComputeLoopSeamCrossfadeWeight`
- `SampleAtLoopSeamCrossfade`

### Keep in place for now

Small trivial helpers may remain where they are if moving them adds unnecessary churn.

Examples:
- `Clamp01`
- `VoiceLayerForAllocation`

### Why this stage comes first

- lowest behavior risk
- reduces scroll burden immediately
- separates reusable signal/path helpers from policy code
- makes later render extraction easier

### Done when

- the listed helpers compile from `voice_engine_playback.cpp`
- `voice_engine.cpp` is visibly smaller
- no behavior changes were introduced

---

## Stage 2 — Extract per-layer emphasis / filter support

### Goals

Move the per-layer bus shaping helpers into a focused implementation file.

### Move to `voice_engine_emphasis.cpp`

- `VoiceEngine::RecomputeLayerEmphasisCoeffs_`
- `VoiceEngine::ProcessLayerBusSample_`

### Why this stage matters

These functions are a clean conceptual island:

- they are not event dispatch
- they are not voice allocation
- they are not sample-position math
- they are not the whole render function

This gives the repo a clear answer to: “Where does per-layer emphasis live?”

### Done when

- both helpers compile from `voice_engine_emphasis.cpp`
- file ownership is obvious from the filename
- `RenderBlock()` still behaves identically

---

## Stage 3 — Extract voice lifecycle and allocation

### Goals

Move voice-start, stop-fade, steal, and note lifecycle behavior into one focused file.

### Move to `voice_engine_voice_lifecycle.cpp`

- `VoiceEngine::StartStopFade_`
- `VoiceEngine::FinishStopFade_`
- `VoiceEngine::StartVoice_`
- `VoiceEngine::AllocateVoice_`
- `VoiceEngine::AllNotesOff_`
- `VoiceEngine::NoteOff_`
- `VoiceEngine::PackVoiceDebug_`

### Why this stage comes before event extraction

`ProcessEvents()` depends on these behaviors.

Moving lifecycle/allocation first makes `ProcessEvents()` easier to read later as dispatch/policy code instead of dispatch mixed with implementation detail.

### Cautions

Do not alter:

- free-voice preference
- oldest-note or steal policy
- per-layer allocation assumptions
- stop-fade timing
- voice-start initialization semantics

This stage is movement only.

### Done when

- the listed helpers compile from `voice_engine_voice_lifecycle.cpp`
- allocation and steal behavior remain unchanged
- `ProcessEvents()` still reads correctly against the moved helpers

---

## Stage 4 — Extract event handling

### Goals

Move the event dispatch function into its own file once its dependencies already live elsewhere.

### Move to `voice_engine_events.cpp`

- `VoiceEngine::ProcessEvents`

### Why now

After Stage 3, `ProcessEvents()` becomes much easier to understand because the called lifecycle/allocation code is already separated.

### Cautions

- do not rewrite the `NoteOn` path during the initial move
- do not refactor for elegance in the same step
- move the function intact first

A later cleanup can split internal branches if still needed.

### Done when

- `ProcessEvents()` compiles from `voice_engine_events.cpp`
- event behavior is unchanged
- note-on/note-off/all-notes-off behavior remains identical

---

## Stage 5 — Decompose `RenderBlock()` internally before moving it

### Goals

Reduce the mental load inside `RenderBlock()` by extracting private helper phases before moving the whole render path into its own file.

### Why this must happen before file extraction

If the code simply moves one giant `RenderBlock()` into `voice_engine_render.cpp`, the repo still ends up with a giant render monolith.

The real readability win is phase separation.

### Current phases already present inside `RenderBlock()`

#### A. Block snapshot / control prep
Examples include:
- loop mode snapshot
- macro snapshot
- plock snapshot
- sample edit snapshot
- engine tune/gain precompute
- block-size-dependent scalar prep
- modulation route snapshot

#### B. Output and layer prep
Examples include:
- buffer clear/reset
- emphasis coeff refresh
- clip/debug/playhead local init

#### C. Per-voice render
Examples include:
- mod env tick
- modulation accumulation
- pitch scaling
- special steal crossfade path
- normal voice path
- playhead/debug tracking

#### D. Layer bus and final mix
Examples include:
- layer bus processing
- mono combine
- clip detect

#### E. Debug / telemetry writeback
Examples include:
- counters
- debug outputs
- playhead atomics
- active voice reporting

### First internal extractions

Extract small private member helpers such as:

- `RefreshBlockState_(...)`
- `SnapshotMacroState_()`
- `SnapshotPLockState_()`
- `PrepareRenderScalars_(...)`
- `WriteRenderDebug_(...)`

Exact names can vary, but the phase boundaries should remain clear.

### Done when

- `RenderBlock()` becomes visibly shorter and more orchestration-focused
- helper names reflect actual render phases
- behavior remains unchanged

---

## Stage 6 — Extract the two per-voice render branches

### Goals

Separate the normal voice path from the stolen-voice crossfade path.

### Extract as private member helpers

- `RenderStealXFadeVoice_(...)`
- `RenderNormalVoice_(...)`

### Why this is the biggest readability win inside render

These are meaningfully different behaviors and should not be forced into one overly generic helper.

Keeping them separate makes it much easier to reason about:

- steal behavior
- stop-fade/crossfade behavior
- normal voice playback behavior
- future bug isolation

### Cautions

- do not merge both paths into one generic “render voice” function
- do not change how stolen and non-stolen voices transition
- only extract existing logic into named helpers

### Done when

- both branches are separate private helpers
- `RenderBlock()` reads as orchestration over named render paths
- no audible behavior changes occur

---

## Stage 7 — Move render implementation into `voice_engine_render.cpp`

### Goals

After internal render decomposition is complete, move render ownership into its own implementation file.

### Move to `voice_engine_render.cpp`

- `VoiceEngine::RenderBlock`
- the new render-phase private helpers
- the new per-voice render helpers

### Why this is safe now

At this point the render logic has already been decomposed into readable phases, so the new file becomes a clear destination for render ownership instead of another monolith.

### Done when

- `voice_engine_render.cpp` is the clear home of render logic
- render-phase helpers live alongside `RenderBlock()`
- the build stays clean

---

## Stage 8 — Update docs and file map references

### Goals

Make repo docs match the new implementation layout.

### Tasks

Update any engine-related doc that points people only to `voice_engine.cpp`.

Examples may include:
- `docs/FILE_MAP.md`
- `docs/README_GPT.md`
- engine code-path references
- any perform/engine navigation docs

### Done when

- docs point to the new implementation files accurately
- “where to look” guidance matches reality
- Codex/ChatGPT no longer gets false anchors from old file references

---

## Not part of the first pass

These are intentionally out of scope for the first cleanup campaign:

- changing `Voice` struct layout
- introducing new classes
- changing event queue behavior
- changing modulation routing semantics
- changing loop behavior
- changing voice stealing policy
- changing envelope timing or curve behavior
- changing layer emphasis behavior
- changing debug/telemetry meaning
- consolidating stolen and non-stolen render paths into one abstraction

Those are separate refactor or behavior tasks, not extraction-only cleanup.

---

## Suggested end-state ownership summary

### `voice_engine_playback.cpp`
Owns:
- playback math
- envelope helpers
- loop helpers
- seam crossfade helpers

### `voice_engine_emphasis.cpp`
Owns:
- per-layer emphasis coefficient refresh
- per-layer bus sample processing

### `voice_engine_voice_lifecycle.cpp`
Owns:
- voice start
- voice stop fade
- voice finish/retire
- note off / all notes off
- allocation / stealing policy support
- voice debug packing

### `voice_engine_events.cpp`
Owns:
- event dispatch and handling

### `voice_engine_render.cpp`
Owns:
- block prep
- per-voice render orchestration
- normal voice rendering
- steal crossfade rendering
- final layer mix
- debug/telemetry writeback

---

## Success criteria

This cleanup is successful when:

- `voice_engine.cpp` is no longer the single place where all mental layers accumulate
- file names communicate ownership clearly
- `RenderBlock()` is decomposed into named phases
- event handling, lifecycle, and playback math are navigable without scrolling through unrelated DSP logic
- the public API and behavior remain unchanged
- each step remains small enough to review and revert safely if needed

---

## One-line priority summary

Follow this order:

**helpers → emphasis → lifecycle/allocation → events → render decomposition → render file extraction → docs update**
