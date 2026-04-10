# VOICE_ENGINE_EXTRACTION_RULES

## Purpose

These rules govern cleanup work on `voice_engine.cpp` and any files extracted from it.

The purpose is to improve readability and file ownership without changing runtime behavior, threading behavior, or the current engine contract.

This is an extraction-only ruleset.

---

## Primary rule

**This pass is extraction-only. Do not change behavior.**

If a change is not required to move code safely into a clearer boundary, do not make it.

---

## Scope rules

### 1. Preserve the public API
Do not change the public `VoiceEngine` interface unless a step explicitly calls for it.

### 2. Preserve the ownership model
Keep `VoiceEngine` as the owning class.
Do not introduce a new architecture, manager, or subsystem layer during extraction.

### 3. Preserve the real-time contract
Do not change the audio-thread contract.
Do not introduce any blocking work, allocation, filesystem work, or other non-real-time behavior into render/event/audio paths.

### 4. Preserve runtime behavior
No intentional sound, timing, routing, envelope, loop, allocation, or debug behavior changes are allowed in extraction steps.

### 5. Prefer movement over rewrite
When splitting code into new files, move existing logic with minimal edits.
Do not rewrite working logic for style reasons.

---

## File-boundary rules

### 6. Split by responsibility, not by arbitrary size
Each extracted file should have a clear conceptual owner.
Examples:
- playback math
- emphasis/filter support
- voice lifecycle/allocation
- event handling
- render path

### 7. Do not create “misc” or “helpers” dumping grounds unless absolutely required
File names should tell Codex/ChatGPT where to look.
Prefer explicit names such as:
- `voice_engine_playback.cpp`
- `voice_engine_events.cpp`
- `voice_engine_render.cpp`

### 8. Keep related code together
If two helpers are conceptually part of the same behavior, keep them together.
Do not scatter one responsibility across multiple files without need.

### 9. Keep private helper ownership obvious
Private helpers should live near the implementation that uses them most heavily.
Avoid moving logic so far away that the render/event/lifecycle flow becomes harder to follow.

---

## Safety rules for extraction steps

### 10. Compile after every step
Every extraction step must end with a successful build.

### 11. Keep changes narrow
Each prompt/task should move one responsibility cluster at a time.
Avoid multi-domain edits in one pass.

### 12. Do not combine extraction with refactor
Do not move code and redesign it in the same step.
First move it intact. Later, if needed, do a separate cleanup pass.

### 13. Do not combine extraction with bug fixing
If a real bug is discovered, record it separately unless the current task explicitly allows fixing it.
Do not hide bug fixes inside cleanup movement.

### 14. Preserve signatures where possible
Do not change helper signatures unless required to compile after movement.
If a signature must change, keep that change as small and local as possible.

### 15. Minimize rename churn
Do not rename symbols casually.
Use existing names unless a rename is required for clarity or collision avoidance.

---

## Render-path rules

### 16. Treat `RenderBlock()` as high risk
`RenderBlock()` is the center of gravity in this cleanup.
Do not rewrite it wholesale.

### 17. Decompose render in phases before moving it
Before moving `RenderBlock()` into its own file, extract small private helpers that reflect the actual render phases.

### 18. Keep special render paths separate
The stolen-voice crossfade path and the normal voice path should remain separate helpers.
Do not force them into one generic abstraction.

### 19. Do not change render-time math during extraction
No changes to:
- interpolation behavior
- loop seam handling
- playhead advancement
- fade step behavior
- clip behavior
- per-block scalar meaning

unless explicitly required by a later non-cleanup task.

### 20. Preserve debug writeback behavior
Do not change debug counters, telemetry meaning, or writeback timing during extraction-only passes.

---

## Voice lifecycle and event rules

### 21. Preserve allocation policy
Do not alter how voices are chosen, stolen, reused, or retired unless a later task explicitly allows policy changes.

### 22. Preserve voice-start initialization
Do not change how a new voice is initialized during extraction.

### 23. Preserve note-off behavior
Do not change release handling, stop-fade behavior, or voice retirement timing.

### 24. Preserve event semantics
Do not change the meaning of incoming events or their dispatch order during extraction.

### 25. Move `ProcessEvents()` intact first
If `ProcessEvents()` is later split internally, that must be a separate cleanup step.
The initial extraction should move it intact.

---

## State-layout rules

### 26. Do not change `Voice` layout in the first pass
The `Voice` struct may be visually dense, but layout regrouping is out of scope for the first extraction campaign.

### 27. Do not change `VoiceEngine` state layout unless required
If internal declarations must move to support compilation, keep state layout changes minimal and explicit.

### 28. Do not introduce heap allocation
No `new`, dynamic containers with runtime growth, or ownership changes that add allocation to engine/runtime code.

---

## Internal-header rules

### 29. Add an internal header only if necessary
If extracted files need shared internal declarations, use a narrow internal header such as `voice_engine_internal.h`.
Do not turn the internal header into a second monolith.

### 30. Keep internal declarations private to the engine implementation
Do not expose internal helper details through public headers unless truly required.

---

## Documentation rules

### 31. Update docs when file ownership moves
If a cleanup step changes where code lives, update file-map or engine-path docs accordingly.

### 32. Keep “where to look” guidance accurate
Do not leave stale references telling future readers to look only in `voice_engine.cpp` after code has moved.

---

## Review/reporting rules

### 33. Every cleanup step should report:
- short summary
- exact files changed
- commands run
- build result
- risk notes

### 34. Call out any unavoidable deviations
If a step required a signature change, declaration move, or other necessary deviation from pure movement, state it explicitly.

### 35. Prefer reversible steps
Each extraction step should be small enough that it can be reviewed or reverted without ambiguity.

---

## Out-of-scope changes for this cleanup

Do not do any of the following unless a later task explicitly permits it:

- change DSP behavior
- change modulation routing behavior
- change envelope timing or curves
- change loop mode behavior
- change sample seam-crossfade behavior
- change voice-stealing policy
- change debug field meanings
- introduce new classes or subsystem layers
- redesign `Voice` into nested sub-structs
- merge normal and stolen render paths into one abstraction

---

## Practical decision rule

When unsure, ask:

**“Is this change required to move code into a clearer boundary without changing behavior?”**

If the answer is no, do not include it in the extraction step.

---

## One-line rule summary

**Move code by responsibility, keep behavior frozen, compile after each step, and do not mix cleanup with redesign.**
