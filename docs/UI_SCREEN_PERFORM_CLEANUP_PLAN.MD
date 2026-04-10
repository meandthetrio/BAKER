# UI_SCREEN_PERFORM_CLEANUP_PLAN

## Purpose

This document defines the cleanup sequence for `src/ui/ui_screen_perform_process.cpp` so refactors stay narrow, reversible, and behavior-safe.

The goal is to improve readability by separating distinct mental layers that are currently interleaved in one file:
- PROCESS state and edit behavior
- PROCESS main render
- PROCESS sub-render helpers for FX detail, EQ graph, and value drawing

---

## Current readability problem

`src/ui/ui_screen_perform_process.cpp` currently mixes several different responsibilities:
- small math and clamp helpers
- PROCESS value formatting and knob normalization
- large FX detail rendering with multiple modes
- EQ graph rendering and cache behavior
- event handling for layer volume, FX focus, FX reorder, EQ graph edit, and FX detail edit
- main PROCESS render layout, including inline knob and tiny text drawing logic

The issue is not that the file is "too long" by itself. The issue is that too many mental layers are mixed together, making the file harder to read safely.

---

## Cleanup principles

- Prefer extraction-only refactors first.
- Do not change behavior.
- Keep `PerformProcess_OnEvent` and `PerformProcess_Render` as the screen entry points.
- Prefer small static helpers over broad abstraction.
- Only split into additional files after the internal structure is easier to read.
- Compile after each cleanup pass.
- Keep PROCESS-specific helpers local unless they are truly reused elsewhere.

---

## Stage 1 — Biggest readability win first

### Goal

Keep `src/ui/ui_screen_perform_process.cpp` as the owner file, but split its internals into clearly named static helpers.

### Target shape

The file should clearly present:
- `PerformProcess_OnEvent`
- `PerformProcess_Render`
- small static helpers above them

### Pass 1A — Extract layer-volume helpers

Extract the layer-volume behavior out of the main event and render bodies.

Recommended helpers:
- `ProcessHandleLayerToggle(...)`
- `ProcessHandleLayerVolumeEdit(...)`
- `ProcessHandleLayerMuteToggle(...)`
- `ProcessSyncLayerVolumeUiState(...)`

Why this is first:
- Layer volume behavior is a separate mental model from FX editing.
- It appears in both event and render paths.
- Removing it makes the main event switch and render flow easier to scan.

### Pass 1B — Extract FX detail edit helpers

Pull the per-FX detail edit math out of `PerformProcess_OnEvent`.

Recommended helpers:
- `ProcessEditSatDetail(...)`
- `ProcessEditDelayDetail(...)`
- `ProcessEditReverbDetail(...)`
- `ProcessEditEqGraph(...)`

Why this matters:
- The large `switch(fx_id)` is one of the main readability problems.
- Event flow becomes easier to follow.
- FX-specific math stops competing with navigation logic.

### Pass 1C — Extract FX lane helpers

Pull out tiny repeated concepts related to selected FX and FX lane behavior.

Recommended helpers:
- `ProcessDetailParamCount(uint8_t fx_id)`
- `ProcessSelectedFxId(const AppState&)`
- `ProcessMainCursorSelectsFx(uint8_t main_cursor)`
- `ProcessApplyFxOrderToTargets(...)`
- `ProcessHandleFxReorder(...)`

Why this helps:
- These are repeated ideas hidden as inline logic.
- They reduce cursor and FX-order decoding noise in both event and render code.

---

## Stage 2 — Split giant render helper by mode

### Goal

Keep the current behavior, but split `DrawFxDetailScreen(...)` into mode-specific render helpers.

### Recommended structure

- `DrawProcessSatDetail(...)`
- `DrawProcessEqDetailPlaceholder(...)`
- `DrawProcessDelayDetail(...)`
- `DrawProcessReverbDetail(...)`
- keep `DrawFxDetailScreen(...)` as a small dispatcher

Why this is a strong second pass:
- One function currently contains multiple different screen designs.
- Each mode has different constants, labels, and drawing assumptions.
- After the split, each mode becomes locally understandable.

---

## Stage 3 — Extract PROCESS-specific value drawing helpers

### Goal

Reduce inline drawing and formatting logic inside `PerformProcess_Render`.

### Recommended helpers

- `ProcessValueAdvance(...)`
- `ProcessValueWidth(...)`
- `DrawProcessValueText(...)`
- `DrawProcessPlusGlyph(...)`
- `DrawProcessKnob(...)`

Why this helps:
- The inline lambda-based drawing makes the main render body harder to skim.
- The knob renderer is real UI logic and deserves a clear name.
- The main render function should read more like layout code and less like pixel-font implementation.

---

## Stage 4 — Isolate EQ graph code

### Goal

Make the EQ graph path easier to reason about without changing its behavior.

### Recommended helpers

- `ProcessEqGraphNeedsRecompute(...)`
- `ProcessEqGraphRecomputeCurve(...)`
- `ProcessEqGraphHzToX(...)`
- keep `DrawEqGraphScreen(...)` as the screen-level coordinator

Why this is later:
- The EQ graph path is already more self-contained than the event and FX detail paths.
- It is worth cleaning up, but it is not the biggest readability win first.

---

## Stage 5 — Optional file split only after internal cleanup

### Goal

Consider a second file only after the internal helper cleanup is complete.

### Best likely split

Keep `src/ui/ui_screen_perform_process.cpp` for:
- `PerformProcess_OnEvent`
- `PerformProcess_Render`
- top-level dispatch helpers

Move rendering-heavy helpers into a second file such as:
- `src/ui/ui_screen_perform_process_detail.cpp`
- or `src/ui/ui_screen_perform_process_draw.cpp`

Good candidates for that second file:
- `DrawFxDetailScreen(...)`
- `DrawProcessSatDetail(...)`
- `DrawProcessDelayDetail(...)`
- `DrawProcessReverbDetail(...)`
- `DrawEqGraphScreen(...)`
- PROCESS value and knob drawing helpers

Important:
- Do not make this the first cleanup pass.
- First reduce mental load inside the current file.
- Then decide whether a second compilation unit is actually worthwhile.

---

## Recommended exact order

1. Extract layer-volume event and render helpers.
2. Extract FX edit helpers from `PerformProcess_OnEvent`.
3. Split `DrawFxDetailScreen(...)` into mode-specific render helpers.
4. Extract knob and value drawing from `PerformProcess_Render`.
5. Clean up EQ graph helper boundaries.
6. Only then decide whether to split into a second file.

---

## Desired end state

The next-pass cleanup should make `PerformProcess_OnEvent` read approximately like this:
- common exits
- pod button handling
- ext-click mode handling
- pod encoder navigation
- ext encoder handling for:
  - EQ graph edit
  - detail edit
  - FX reorder
  - layer volume edit
  - top-level FX wet edit

And `PerformProcess_Render` should read more like:
- high-level screen layout
- focus and mode display
- dispatch to named render helpers
- minimal inline formatting and pixel math

That is the biggest readability win first, with the least risk.
