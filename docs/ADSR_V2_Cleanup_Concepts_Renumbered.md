ADSR_V2 Cleanup Concepts — Renumbered and Expanded

Source basis: the attached “High-level cleanup concepts for ADSR_V2” step list, with original Step 1 removed and all remaining steps renumbered. The conceptual intent is preserved, but each step now includes repo-specific substeps based on the current ADSR_V2 tree and the hotspots visible in the attached repo.

Execution note

- This file is the cleanup-loop source of truth for repo cleanup passes.
- Each top-level numbered item is one checklist step.
- The numbered substeps under each concept define scope and acceptance guidance for that step.
- In single-step mode, the active step is the lowest-numbered top-level item without a trailing `[done]` marker.
- During execution, the active step heading may be marked `[in progress]`.
- When a step is completed and validated, replace that marker with `[done]`.
- When confronted with an ambiguity/risk boundary, stop and think through the correct, smart way to proceed before editing further. Prefer pausing or asking over guessing.

1. Make the physical file layout match the documented architecture [done]

Concept
One of the biggest gaps in ADSR_V2 is that the docs are cleaner than the actual tree. The architecture is described well, but the code is still split across root-level files, src/ui/, older patterns, and newer extracted patterns. The repo should look more like what the docs claim it is.

Repo-specific substeps
1.1. Decide on one durable top-level source layout and stop mixing root-level implementation files with extracted src/ui files.
1.2. Move all screen-specific UI implementation toward a single screen area so files like ui_screen_perform_*.cpp, ui_screen_record.cpp, ui_screen_status.cpp, ui_screen_browser.cpp, and ui_screen_shift.cpp read as one physical subsystem.
1.3. Keep shared UI infrastructure grouped separately from per-screen code so ui_input, ui_layout, ui_list_menu, ui_render, ui_overlay, ui_value_edit, and ui_logic are clearly “framework” rather than “screen content.”
1.4. Keep engine-side files grouped by engine responsibility so audio_engine.*, voice_engine_*, params.*, macros.*, modulation files, and supporting DSP files are physically easier to scan as one cluster.
1.5. Group worker/project persistence files more explicitly so ui_worker.*, ui_worker_project.cpp, project_manifest.h, sd_browser_state.*, sd_sample_pool.*, and related project actions read as a coherent project/storage subsystem.
1.6. Update docs/FILE_MAP.md after each boundary move so the file map remains a literal reflection of the tree rather than a target state.

2. Reduce the “transitional repo” feel [done]

Concept
Right now the repo still feels like it is halfway through a cleanup. Old and new organization styles coexist, split modules still depend on central files too heavily, and several boundaries feel partial rather than final. A cleaner repo should feel settled.

Repo-specific substeps
2.1. Identify every place where a file was extracted but still behaves like an appendage of a giant parent file, especially around ui_screens.cpp, ui_worker.cpp, and app_state.h.
2.2. Convert “paired giant file plus extracted helper” patterns into true ownership boundaries, where the extracted file owns a real concern rather than just copied lines.
2.3. Remove stale comments, compatibility shims, or include patterns that only exist because an earlier cleanup pass stopped halfway.
2.4. Decide whether root-level ui_screens.cpp remains a permanent façade or whether more of its current responsibilities should finish moving into src/ui.
2.5. Do the same review for the app_state split, making sure app_state_ui.h, app_state_worker.h, app_state_engine.h, app_state_project.h, app_state_recording.h, app_state_diagnostics.h, and app_state_shared.h are more than partial buckets.
2.6. Prefer one naming and ownership pattern consistently across new files so future cleanup does not create a third organizational style.

3. Shrink the biggest hotspot files [done]

Concept
Several files still carry too much responsibility. The repo gets cleaner when those files stop being gravity wells and instead move toward one dominant purpose.

Repo-specific substeps
3.1. Continue shrinking ui_worker.cpp so it becomes orchestration-heavy rather than the place where unrelated worker details accumulate.
3.2. Keep ui_worker_project.cpp focused on project save/load and restore sequencing, not on unrelated worker behavior.
3.3. Continue splitting ui_screens.cpp by stable screen or widget responsibility until it stops acting like a universal UI dumping ground.
3.4. Reduce the amount of render-heavy logic living in ui_screen_perform_process.cpp and ui_screen_perform_process_draw.cpp by extracting stable formatting, normalization, and helper drawing logic into smaller units.
3.5. Review ui_logic.cpp for mixed responsibilities between navigation, input interpretation, dispatch, and per-screen exceptions.
3.6. Review voice_engine.cpp and remaining engine-side hubs so they keep moving toward one-purpose files rather than regaining central mass.
3.7. Track hotspot reduction in docs so “largest file in subsystem” becomes a deliberate exception, not the default pattern.

4. Reduce central app_state gravity [done]

Concept
Even after separation work, app_state.h still acts like a conceptual center of mass. A cleaner state model means less central accumulation, narrower ownership, and clearer boundaries between UI, worker, engine, diagnostics, and shared handoff data.

Repo-specific substeps
4.1. Treat the split app_state headers as domain entry points, not just overflow bins for fields that used to live in app_state.h.
4.2. Move fields toward the narrowest true owner whenever possible so UI-only state lives with UI, worker-only state lives with worker, and diagnostics do not pollute general runtime state.
4.3. Reduce includes on app_state.h and the split headers so unrelated modules are not forced to depend on broad state definitions.
4.4. Audit “write from many places, read from many places” fields and decide whether they should become explicit handoff data instead.
4.5. Preserve the threading contract while narrowing state visibility so audio-thread-facing state remains deterministic and main-thread state remains clearly separate.
4.6. Use docs/APP_STATE_FIELD_INVENTORY.md and docs/APP_STATE_HEADER_SPLIT_CHECKLIST.md as the enforcement tools for continued state cleanup.

5. Finish module boundaries so they are real, not aspirational [done]

Concept
ADSR_V2 already has strong architectural language. The next step is making the boundaries truly hold. The boundary should be obvious from the tree and the includes, not just from docs.

Repo-specific substeps
5.1. For each major subsystem, define which file is the public entry point, which files are internal helpers, and which headers should not leak across subsystems.
5.2. Use ui_worker_internal.h, voice_engine_internal.h, and similar internal headers as true containment tools rather than broad shared escape hatches.
5.3. Reduce cross-domain includes so UI code is not casually reaching into engine internals or worker internals without a clear handoff layer.
5.4. Where a subsystem still depends on a giant shared header, replace that dependency with a narrower shared struct, function surface, or internal helper header.
5.5. Keep naming aligned with ownership so “screen,” “worker,” “engine,” “project,” “shared,” and “diagnostic” actually predict what the file may contain.
5.6. Use docs/FILE_MAP.md and AGENTS.md to reject extractions that change file count without improving ownership clarity.

6. Make UI code easier to navigate by screen and by responsibility [done]

Concept
The UI side is one of the biggest readability opportunities. The pain is not just size, but mixed mental layers: routing, event handling, drawing, formatting, edit behavior, widget logic, and per-screen details. Cleaner UI code separates these concerns.

Repo-specific substeps
6.1. Keep screen entry points shallow so a reader can tell quickly how a screen is entered, updated, and rendered.
6.2. Separate event handling from drawing for perform screens, record screens, browser screens, status screens, and shift/settings screens.
6.3. Keep reusable draw helpers and text/value formatting out of per-screen files when those helpers are used across multiple screens.
6.4. Make screen-specific files truly screen-specific: for example, perform engine files should not carry record or browser concerns.
6.5. Continue the pattern already visible in ui_screen_perform_engine.cpp, ui_screen_perform_adsr.cpp, ui_screen_perform_keyzone.cpp, ui_screen_perform_emphasis.cpp, ui_screen_perform_wave_edit.cpp, and related files.
6.6. Keep docs/PERFORM_MENU_FOCUS_REFERENCE.md, docs/RECORD_MENU_FOCUS_REFERENCE.md, and docs/PRESETS_MENU_FOCUS_REFERENCE.md synchronized with structural UI changes so navigation knowledge remains externalized.
6.7. Avoid creating new “misc UI helper” files that become the next dumping ground.

7. Make worker/project persistence code narrower and easier to reason about [done]

Concept
The worker and project-loading areas are still major hot spots. They are stateful, failure-prone, cross-domain, and easy to destabilize. Cleaner persistence code means a smaller orchestration surface, clearer save/load stages, and narrower restore responsibilities.

Repo-specific substeps
7.1. Make the project save path and the project load path easy to trace end to end without scanning unrelated worker functionality.
7.2. Keep status/result reporting separate from the file IO and manifest handling so failure display logic does not muddy persistence logic.
7.3. Isolate shared SD/file-handle bookkeeping so manual sample load, project load, and project save are not subtly coupled through hidden worker state.
7.4. Keep project_manifest.h strictly about serialized project structure rather than runtime orchestration details.
7.5. Keep the restore pipeline explicit: parse manifest, stage restore intent, restore owned domains, republish params, and update UI-visible status.
7.6. Keep docs/TEST_MATRIX.md and milestone-style restore regressions aligned with every persistence cleanup step.
7.7. Favor narrow helper names around slot selection, path building, manifest read/write, sample restore, and post-load publication so failures can be localized quickly.

8. Keep voice/render code split by mental task [done]

Concept
The repo has already been moving in the right direction with voice_engine_* files. The idea should be pushed further wherever needed so DSP code is easier to inspect one concern at a time.

Repo-specific substeps
8.1. Preserve the split between voice lifecycle, event handling, playback math, render processing, and emphasis/filter support.
8.2. Keep voice allocation and stealing logic in lifecycle-focused files rather than letting it creep back into general render code.
8.3. Keep note/event translation and per-block note handling in event-focused files.
8.4. Keep sample playback, loop math, interpolation behavior, and playback position logic in playback-focused files.
8.5. Keep block render mixing and output-stage processing in render-focused files.
8.6. Keep per-layer emphasis/filter behavior in dedicated files such as voice_engine_emphasis.cpp unless there is a strong reason not to.
8.7. Make sure audio_engine.cpp stays a higher-level coordinator rather than another place where voice-level detail quietly accumulates.

9. Reduce mixed ownership signals [done]

Concept
Some files still feel like they belong to more than one subsystem. That creates questions about whether a file is UI or engine, worker or state, shared or domain-local, permanent or transitional. Cleaner repos make ownership obvious.

Repo-specific substeps
9.1. Rename or relocate files whose current name does not predict their contents well enough.
9.2. Make “shared” files truly small and intentionally shared rather than fallback homes for anything that crosses a boundary.
9.3. Avoid putting UI policy into generic helpers if that policy really belongs to one screen family.
9.4. Avoid putting worker-specific persistence concerns into broadly named files where they look like general infrastructure.
9.5. Tighten headers so public headers expose only what downstream modules truly need.
9.6. Review whether files like project_actions, ui_requests, and shared app state surfaces are still carrying mixed signals and should be narrowed.

10. Turn documentation advantage into structural advantage [done]

Concept
ADSR_V2 already wins on architecture docs, thread ownership clarity, AI/operator guidance, refactor safety rails, and “where do I touch this safely?” answers. The next step is making the code structure deserve the docs it already has.

Repo-specific substeps
10.1. Treat docs/FILE_MAP.md as a structural contract and correct either the tree or the doc whenever they drift.
10.2. Keep docs/README_GPT.md focused on true durable architecture, not on temporary cleanup-stage explanations that should disappear once the tree is cleaner.
10.3. Where docs identify hotspots, use that list to drive actual file reductions until the hotspot list gets shorter over time.
10.4. Keep AGENTS.md aligned with the current cleanup style so AI prompts continue to produce narrow, reversible changes.
10.5. When a module extraction lands, update the docs immediately so the documentation advantage stays real.
10.6. Prefer fewer but sharper docs that directly help editing over broad narrative docs that repeat the same ideas.

11. Preserve the strong AI-safe workflow while simplifying the codebase [done]

Concept
One of ADSR_V2’s biggest strengths is that it is already very AI-aware. The goal is to keep the guardrails while reducing the need for them by making the code simpler.

Repo-specific substeps
11.1. Keep checklist-driven cleanup, exact-file reporting, and compile-after-each-pass discipline.
11.2. Keep thread/domain tags and “Where To Look” guidance where they still prevent dangerous edits.
11.3. Reduce situations where a prompt must explain large amounts of repo lore before an edit can be made safely.
11.4. Prefer module structures that make the correct edit path obvious from filenames and folder placement.
11.5. Make future prompts shorter by making ownership clearer in code rather than relying on ever-longer instructions.
11.6. Preserve docs and workflow rules that defend the real-time threading contract while simplifying everything around them.

12. Make first-open readability better [done]

Concept
Just-a-Sample was easier to understand on first open, while ADSR_V2 was easier to reason about safely once inside. A key cleanup goal is improving the first-open experience without losing the safety advantages.

Repo-specific substeps
12.1. Make the repo root easier to scan by reducing the amount of implementation detail and historical clutter visible immediately.
12.2. Make the main entry points obvious: main.cpp, audio_engine.*, UI entry surfaces, worker/project surfaces, and the most important docs.
12.3. Prefer subsystem folders and filenames that let a new reader answer “where do I look first?” within a few seconds.
12.4. Keep generated or incidental files from competing visually with real source files in shared archives and repo snapshots.
12.5. Consider a short “start here” doc that points to the current durable entry points once the tree is more settled.
12.6. Reduce the number of giant files that dominate a first-open scan of the codebase.

13. Reduce the mismatch between “clean in theory” and “clean in shape” [done]

Concept
This is the core of the earlier verdict. ADSR_V2 is not lacking in architectural thought; it is lacking in full physical follow-through. The goal is to move from documented cleanliness to structural cleanliness.

Repo-specific substeps
13.1. Use every cleanup pass to make one structural truth more visible in the filesystem, not just in documentation.
13.2. Prefer one-file/one-purpose boundaries wherever they are stable enough to hold.
13.3. Reduce oversized files, central dependency magnets, mixed layout, and transitional scaffolding incrementally but permanently.
13.4. Treat each extracted file as successful only if it lowers cognitive load for the next reader.
13.5. Measure progress by easier navigation, narrower edits, fewer cross-domain touches, and less need for explanatory prompt scaffolding.
13.6. Keep the end goal in mind: ADSR_V2 should become physically as clean as it already is conceptually.

Condensed summary

At a high level, ADSR_V2 would be cleaner if it became:
- less mixed in file layout
- less transitional in feel
- less dependent on giant hotspot files
- less centered on app_state
- more explicit in real module ownership
- more navigable on the UI side
- narrower in worker/project persistence paths
- more segmented in voice/render logic
- more aligned with the strong architecture docs it already has
- easier to understand on first open without losing its AI-safe workflow
