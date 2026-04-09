# CLEANUP_RULES.md

## Purpose

These rules govern all cleanup and refactor work in ADSR_V2.

They exist to keep cleanup safe, narrow, and compatible with the repo’s real-time architecture.

---

## Rule 1 — Preserve behavior by default

Cleanup work must not change runtime behavior unless the task explicitly requests behavior change.

This includes:
- UI flow
- focus behavior
- status text
- worker sequencing
- project save/load behavior
- sample load/apply behavior
- parameter behavior
- timing behavior

If a cleanup task would require behavior change to succeed, that must be stated explicitly in the result.

---

## Rule 2 — Preserve the threading contract

The repo’s threading model is a hard boundary.

### Audio thread / callback
- deterministic DSP only
- no blocking work
- no file I/O
- no UI work
- no heap allocation
- no logging added casually
- no new non-deterministic behavior

### Main thread / worker side
- UI
- storage
- SD/file operations
- orchestration
- non-real-time workflows

Do not blur these ownership boundaries during cleanup.

---

## Rule 3 — Prefer narrow diffs

Cleanup tasks should be small and reviewable.

Prefer:
- one subsystem at a time
- one screen/domain at a time
- one worker responsibility at a time
- one structural improvement per task

Avoid broad “while I’m here” edits.

---

## Rule 4 — Prefer code movement over code rewrite

If working code can be improved by moving it into a better file or grouping it more clearly, do that instead of rewriting it.

Prefer:
- extraction
- regrouping
- file splitting
- helper relocation

Avoid:
- unnecessary logic rewrites
- opportunistic redesign
- style-only churn in unrelated areas

---

## Rule 5 — Do not expand task scope casually

Only modify files outside the named target area if required to:
- keep the build passing
- preserve existing interfaces
- update documentation
- complete the requested split safely

When extra files are touched, they must be listed and justified.

---

## Rule 6 — Preserve naming unless renaming is the task

Keep stable names stable unless:
- the prompt explicitly asks for renaming
- a rename is required to complete a split cleanly
- the rename meaningfully improves clarity and is small in scope

Avoid broad naming churn during structural cleanup.

---

## Rule 7 — Keep public behavior and strings stable

Do not casually change:
- menu labels
- settings labels
- status messages
- overlay wording
- workflow wording
- diagnostic text

User-visible strings are part of behavior.

---

## Rule 8 — Do not introduce new abstractions without payoff

New abstractions are allowed only if they:
- reduce duplication
- improve module boundaries
- make behavior easier to find
- preserve clarity

Do not introduce patterns, wrappers, managers, or helper layers just because they seem architecturally “cleaner.”

This repo should remain practical and direct.

---

## Rule 9 — Keep screen logic local to screen/domain splits

When splitting `ui_screens.cpp`:
- move code by screen/domain
- preserve router behavior
- preserve focus/navigation semantics
- avoid cross-domain edits unless required

The split should improve navigation, not trigger a UI redesign.

---

## Rule 10 — Keep worker logic local to operation splits

When splitting `ui_worker.cpp`:
- group project code with project code
- group sample-load code with sample-load code
- group scan/browser code with scan/browser code
- preserve request and status behavior

Do not redesign the worker model unless explicitly requested.

---

## Rule 11 — Keep `main.cpp` as orchestration

When cleaning `main.cpp`, prefer extracting details into helpers while preserving:
- init order
- scheduling behavior
- callback linkage
- control/update timing

Do not make the entry point harder to follow.

---

## Rule 12 — State cleanup should be mechanical first

When restructuring `AppState`:
- group related fields
- preserve semantics
- preserve ownership
- avoid hidden behavioral changes

Mechanical organization comes before conceptual redesign.

---

## Rule 13 — Compile after every cleanup task

Every cleanup task must end with:

make -j4

If the build fails, the task is not complete.

Build status must always be reported.

---

## Rule 14 — Report exactly what changed

Each cleanup result must include:

- short summary
- exact files changed
- commands run
- build result
- risk notes
- docs updated yes/no

Do not hide spillover edits.

---

## Rule 15 — Update docs when boundaries move

When module boundaries change, update the relevant docs, especially:
- `docs/FILE_MAP.md`
- any screen or menu reference docs affected by the change
- any worker/project/sample flow doc made inaccurate by the cleanup

Docs should remain useful after refactors.

---

## Rule 16 — Avoid mixing cleanup with feature work

Do not combine:
- cleanup
- bug fixes
- new features
- behavior changes

unless the task explicitly asks for that combination.

Cleanup tasks should stay cleanup tasks.

---

## Rule 17 — Keep cleanup reversible

A cleanup task should be small enough that it can be:
- reviewed easily
- reverted easily
- understood in one sitting

If the diff becomes hard to explain, it is too large.

---

## Rule 18 — Respect existing stable architecture

This repo already has a meaningful architecture. Cleanup should strengthen it, not replace it.

Respect:
- deterministic audio-thread ownership
- main-thread orchestration
- queue/parameter handoff patterns
- existing UI and worker flow unless explicitly targeted

The goal is cleaner structure, not a fresh rewrite.

---

## Recommended prompt framing

Cleanup prompts should usually contain:

- the exact target file or subsystem
- a statement that behavior must not change
- a statement that unrelated files should not be modified unless required to keep build passing
- a requirement to run `make -j4`
- a requirement to report exact files changed and risk notes

Example:

"Split the Settings-related screen code from `ui_screens.cpp` into `ui_screen_settings.cpp`. Do not change behavior. Do not modify unrelated screens unless required to keep the build passing. Run `make -j4` before finishing. Report exact files changed, build result, and risk notes."