# APP_STATE_NARROWING_PLAN

## Purpose

This document defines the policy for the final AppState cleanup passes.

The goal is not to make `app_state.h` smaller.
The goal is to reduce whole-app dependency reach so subsystem code stops taking `AppState&` or `AppState*`, and leaf files stop including `app_state.h`.

## Rule

- `AppState` is the composition root.
- Top-level orchestration files may still use `AppState`.
- Leaf subsystem files should prefer specific `app_state_*` bucket headers.
- Leaf subsystem files may also use narrow context structs or narrower helper signatures when that is the cleaner dependency boundary.
- `app_state.h` should gradually disappear from subsystem leaf files.

## Acceptable Whole-App Boundaries

These are the kinds of places where whole-app access can remain acceptable:

- top-level app startup and scheduling
- top-level main-loop orchestration
- top-level composition/setup boundaries that must wire multiple subsystems together

Current stable exceptions in this repo:

- `main.cpp` for app startup, audio/main-loop scheduling, and top-level composition
- `src/ui/ui_logic.cpp` for control/UI tick orchestration that still wires multiple UI/runtime buckets together
- `src/ui/ui_render.cpp` for top-level render/tick orchestration
- `src/worker/ui_worker.cpp` for worker request dispatch/tick orchestration and a small set of wrapper entry points
- `src/worker/ui_worker_project.cpp` for project save/load orchestration entry points

These are transitional, not stable exceptions:

- small wrapper utilities that still expose `AppState&` only for compatibility
- helper-level worker or UI functions that can still be narrowed in later passes

These are not good whole-app boundaries:

- UI screen files
- worker static helpers
- small render/event helpers
- leaf subsystem utilities

## Non-Goals

- This is not a behavior rewrite.
- This is not a threading model rewrite.
- This is not a save/load semantics rewrite.
- This is not a rename-only cleanup.

## Pass Standard

Each narrowing pass should prefer:

- signature narrowing over logic rewriting
- specific bucket includes over `app_state.h`
- smaller compile-safe slices over broad cleanup
