# UI_SCREEN_PERFORM_CLEANUP_RULES

## Purpose

These rules define how `src/ui/ui_screen_perform_process.cpp` should be cleaned up so the work stays narrow, safe, and readability-focused.

This cleanup is about improving navigability and separating mental layers. It is not a redesign.

---

## Core rules

1. This pass is extraction-only unless a task explicitly states otherwise.
2. Do not change runtime behavior.
3. Do not change screen flow, control mapping, focus behavior, or edit semantics.
4. Keep `PerformProcess_OnEvent` and `PerformProcess_Render` as the public screen entry points.
5. Prefer small `static` helpers with specific names over broad shared abstractions.
6. Keep PROCESS-specific logic local unless it is already reused elsewhere.
7. Compile after each cleanup pass.
8. Favor commit-sized steps that can be reviewed and reverted easily.

---

## File boundary rules

9. Keep the first cleanup passes in `src/ui/ui_screen_perform_process.cpp`.
10. Do not start by splitting into multiple files.
11. Only consider a second file after the current file has been made easier to read internally.
12. If a second file is introduced later, keep ownership clear:
    - entry points remain in `src/ui/ui_screen_perform_process.cpp`
    - rendering-heavy helpers may move to a detail or draw file
13. Do not move PROCESS code back into `ui_screens.cpp`.

---

## Behavior-safety rules

14. Do not alter event ordering.
15. Do not alter focus transitions.
16. Do not alter encoder meaning or button meaning.
17. Do not alter FX reorder behavior.
18. Do not alter EQ graph behavior.
19. Do not alter FX detail edit ranges, normalization, clamp behavior, or formatting.
20. Do not change any persisted state layout or state ownership as part of cleanup.
21. Do not fold cleanup work together with bug fixes unless a task explicitly allows it.

---

## Extraction rules

22. Extract the highest-noise logic first.
23. First priority is reducing mental mixing inside `PerformProcess_OnEvent`.
24. Second priority is splitting the giant FX detail render path into mode-specific helpers.
25. Third priority is removing inline value and knob drawing from `PerformProcess_Render`.
26. Fourth priority is isolating EQ graph recompute and graph-coordinate helpers.
27. Each extracted helper should have one clear job.
28. Do not create helpers that merely rename a single obvious line unless they improve readability at the call site.

---

## Naming rules

29. Use PROCESS-specific helper names that make the purpose obvious at the call site.
30. Prefer names like:
    - `ProcessHandleLayerVolumeEdit(...)`
    - `ProcessEditDelayDetail(...)`
    - `DrawProcessReverbDetail(...)`
31. Keep naming consistent with existing screen-function style.
32. Do not invent generic helper names if the logic is PROCESS-only.

---

## Render cleanup rules

33. `PerformProcess_Render` should become easier to read as layout and dispatch code.
34. Move pixel-dense drawing logic into named helpers.
35. Keep `DrawFxDetailScreen(...)` only as a dispatcher once mode-specific helpers exist.
36. Keep EQ graph drawing coordinated through a screen-level function, even if internals are split further.
37. Avoid introducing rendering abstractions that make the file harder to trace.

---

## Event cleanup rules

38. `PerformProcess_OnEvent` should read top-down by interaction mode.
39. Keep common exits and high-level routing visible in the main function.
40. Move per-FX edit math out of the main event body.
41. Keep navigation logic separate from parameter-edit math.
42. Keep layer-volume logic separate from FX detail edit logic.

---

## Review rules

43. Each cleanup task should report:
    - short summary
    - exact files changed
    - commands run
    - build result
    - risk notes
44. If a helper extraction forces behavior uncertainty, stop and keep the change smaller.
45. If a proposed abstraction hides more than it clarifies, do not introduce it.
46. Readability wins are measured by easier scanning of entry points, not by helper count alone.

---

## Recommended helper targets

47. Good first-pass extraction targets include:
    - layer-volume edit helpers
    - per-FX detail edit helpers
    - FX lane selection and reorder helpers
48. Good second-pass extraction targets include:
    - SAT detail renderer
    - DELAY detail renderer
    - REVERB detail renderer
    - EQ detail placeholder or dispatcher logic
49. Good later-pass extraction targets include:
    - value width and advance helpers
    - knob renderer
    - EQ graph recompute helpers

---

## Final guardrail

50. The goal is not to make the file “abstract.”
51. The goal is to make the file readable enough that a person can scan:
    - what the PROCESS screen does
    - how events are routed
    - how each edit mode behaves
    - how each visual mode is drawn

If a cleanup step does not improve that, it is probably not the right next step.
