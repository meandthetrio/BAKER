Execution note

- Each `Step X.Y` heading is one loop step.
- Mark the active step `[in progress]` while working.
- Mark a step `[done]` only after its intended pass is complete and `make -j4` passes.


This plan is designed to:
	•	avoid another fake “app_state cleanup”
	•	narrow dependencies instead of just moving fields around
	•	preserve behavior
	•	work in small, testable passes
	•	give you exact prompt-sized steps for Codex/Cursor

⸻

AppState cleanup goal

The goal is not to make app_state.h smaller.

The goal is to make this true:

most subsystem code no longer takes AppState& or AppState*, and most leaf files no longer include app_state.h.

That is the real win.

⸻

Ground rules for every step

Use these for every prompt:
	•	This pass is dependency narrowing only
	•	Do not change behavior
	•	Do not change threading ownership
	•	Do not change save/load semantics
	•	Do not rewrite working logic just to “clean it up”
	•	Prefer changing function signatures and includes over changing logic
	•	Keep diffs narrow and compile after each pass
	•	If a helper needs too many buckets, that is allowed temporarily, but do not pass the whole AppState

Recommended validation each step:
	•	make -j4
	•	no behavior changes intended
	•	report exact files changed
	•	report remaining AppState usage in touched files

⸻

Stage 0 — Freeze the rule before editing

Step 0.1 — Add the repo rule for AppState [done]

Purpose

Make the repo rule explicit before doing more state surgery.

Files
	•	AGENTS.md
	•	docs/FILE_MAP.md
	•	optionally a new doc:
	•	docs/APP_STATE_NARROWING_PLAN.md

What to add

State clearly:
	•	AppState is the composition root
	•	top-level orchestration files may still use it
	•	subsystem leaf files should prefer specific state buckets or narrow context structs
	•	app_state.h should gradually disappear from leaf modules

Prompt

Create a narrow documentation-only pass for AppState cleanup policy.

Goals:
- Define AppState as a composition root, not a normal subsystem parameter type.
- State that leaf modules should prefer specific state bucket headers or narrow context structs.
- State that app_state.h should gradually disappear from subsystem leaf files.
- Do not change code in this pass.

Edit only:
- AGENTS.md
- docs/FILE_MAP.md
- docs/APP_STATE_NARROWING_PLAN.md (new file, if helpful)

Include:
- short summary
- exact files changed
- no behavior changes


⸻

Stage 1 — Kill AppState* inside UI screen context

This is the highest-value real cleanup.

Step 1.1 — Inspect and narrow UiScreenCtx [done]

Purpose

Remove AppState* app from the main screen context.

Likely files
	•	src/ui/ui_screens.h
	•	src/ui/ui_screen_registry.h
	•	src/ui/ui_router.cpp
	•	src/ui/ui_render.cpp
	•	src/ui/ui_logic.cpp

You may also have context definitions elsewhere; use the actual owner in the repo.

Target shape

Replace this kind of thing:

AppState* app;

with narrow pointers/references like:

AppUiState* ui;
AppEngineState* engine;
AppRecordingState* recording;
AppProjectState* project;
AppDiagnosticsState* diag;
AppSharedState* shared;
Params* params;

Do not over-minimize yet. It is okay if the first narrowed UI context still has several buckets.

Prompt

Perform a narrow dependency pass on the UI screen context.

Goal:
- Remove AppState* from UiScreenCtx and replace it with explicit pointers/references to the specific state buckets the UI layer is allowed to access.
- Preserve behavior exactly.
- Do not redesign screen logic yet.
- Do not change screen behavior, routing, or rendering behavior.

Focus on the files that define and populate UiScreenCtx, likely including:
- src/ui/ui_screens.h
- src/ui/ui_screen_registry.h
- src/ui/ui_router.cpp
- src/ui/ui_render.cpp
- src/ui/ui_logic.cpp

Requirements:
- UiScreenCtx must no longer expose AppState*.
- Use explicit state buckets instead, even if the initial narrowed context is still somewhat broad.
- Update only the minimum call sites needed to compile.
- Keep this pass mechanical and behavior-preserving.

Validation:
- make -j4

Report:
- short summary
- exact files changed
- remaining UI files still indirectly relying on whole-app access


⸻

Step 1.2 — Update all screen entry points to use narrowed context [done]

Purpose

Make screen code compile and stop relying on ctx.app.

Likely files
	•	src/ui/ui_screens.cpp
	•	src/ui/ui_screen_*.cpp

What to do

Convert code like:

ctx.app->ui...
ctx.app->engine...
ctx.app->project...

into:

ctx.ui->...
ctx.engine->...
ctx.project->...

Keep this mechanical. Do not redesign the screens yet.

Prompt

Perform a mechanical follow-up pass after UiScreenCtx narrowing.

Goal:
- Update UI screen files to stop using ctx.app and instead use the explicit narrowed state pointers/references now present in UiScreenCtx.
- Preserve behavior exactly.
- Do not do any conceptual cleanup beyond replacing whole-app access with the new narrow context access.

Focus on:
- src/ui/ui_screens.cpp
- all relevant src/ui/ui_screen_*.cpp files that currently access ctx.app

Requirements:
- Remove ctx.app usage from touched UI screen files.
- Replace each access with the correct narrowed bucket from UiScreenCtx.
- Keep diffs narrow and mechanical.
- Do not rewrite rendering or event logic.

Validation:
- make -j4

Report:
- short summary
- exact files changed
- any remaining screen files that still depend on whole-app patterns


⸻

Step 1.3 — Remove app_state.h from touched UI leaf files [done]

Purpose

Make the cleanup real, not cosmetic.

Files

Whichever UI files were changed in Steps 1.1 and 1.2.

What to do

Replace #include "app_state.h" with only the needed bucket headers.

Examples:
	•	app_state_ui.h
	•	app_state_engine.h
	•	app_state_project.h
	•	app_state_recording.h
	•	app_state_diagnostics.h
	•	app_state_shared.h

Prompt

Do a narrow include cleanup for the UI files touched by the UiScreenCtx migration.

Goal:
- Remove app_state.h from touched UI leaf files wherever it is no longer needed.
- Include only the specific app_state_* headers actually required.
- Preserve behavior exactly.

Focus only on the UI files changed in the prior UiScreenCtx passes.

Requirements:
- Do not broaden the cleanup to unrelated files.
- Prefer the narrowest possible includes.
- Keep compile behavior stable.

Validation:
- make -j4

Report:
- short summary
- exact files changed
- which touched files no longer include app_state.h


⸻

Stage 2 — Separate UI-facing session state from raw bucket soup

This is optional but strongly recommended after Stage 1.

Step 2.1 — Introduce a narrow UI aggregate if needed [done]

Purpose

If UiScreenCtx still feels too broad, create one intentional UI-owned aggregate.

Likely files
	•	src/ui/ui_screens.h
	•	src/ui/ui_logic.cpp
	•	src/ui/ui_router.cpp

Example

struct UiSessionState
{
    AppUiState* ui;
    AppEngineState* engine;
    AppRecordingState* recording;
    AppProjectState* project;
};

Then UiScreenCtx can point to that instead of carrying too many raw pointers.

Prompt

Perform a narrow UI context refinement pass.

Goal:
- If UiScreenCtx now carries too many raw state bucket pointers, introduce a small UI-owned aggregate (for example UiSessionState) to group the UI-relevant application state cleanly.
- Preserve behavior exactly.
- Do not reintroduce AppState*.

Focus on:
- src/ui/ui_screens.h
- src/ui/ui_logic.cpp
- src/ui/ui_router.cpp
- any minimal UI files needed to compile

Requirements:
- This is a context-shape cleanup only.
- Do not change UI behavior.
- Keep the resulting context more intentional and readable than a large bag of raw pointers.

Validation:
- make -j4


⸻

Stage 3 — Split AppSharedState into real handoff lanes

This is the next major conceptual win.

Step 3.1 — Define sub-structs inside shared state [done]

Purpose

Turn shared from a mini-monolith into named lanes.

Likely files
	•	app_state_shared.h
	•	app_state.h if the composition struct is declared there
	•	maybe docs:
	•	docs/FILE_MAP.md

Target

Break AppSharedState into sub-structs like:
	•	SamplePublishState
	•	RecordingBridgeState
	•	ModSharedState

This first pass can be layout-only:
	•	move fields into named nested structs
	•	preserve all behavior
	•	no logic changes yet

Prompt

Perform a structure-only cleanup on AppSharedState.

Goal:
- Split AppSharedState into named nested sub-structures representing real handoff lanes.
- Preserve behavior exactly.
- Do not change logic, threading, or field semantics.
- This pass is layout-only plus mechanical access updates.

At minimum, introduce sub-structures along these conceptual lines if they match the current repo:
- sample publish/apply lane
- recording bridge lane
- modulation/plock/shared-performance lane

Primary files:
- app_state_shared.h
- app_state.h (only if needed for composition)
- any minimal call sites required to compile

Requirements:
- Do not rename fields unless necessary for nesting.
- Prefer nested structs inside AppSharedState rather than a large cross-repo redesign.
- Keep the diff narrow and mechanical.

Validation:
- make -j4

Report:
- exact files changed
- the new shared sub-structures introduced


⸻

Step 3.2 — Narrow worker/UI call sites that use shared [done]

Purpose

Make access more explicit after nesting.

Likely files
	•	src/worker/ui_worker.cpp
	•	src/worker/ui_worker_project.cpp
	•	some UI files
	•	maybe engine-side files that read shared lanes

What to do

Convert:

shared.sd_edit_pending

to:

shared.sample.sd_edit_pending

or equivalent.

Prompt

Perform a mechanical follow-up pass after AppSharedState nesting.

Goal:
- Update worker/UI/engine call sites to use the new explicit shared sub-structures.
- Preserve behavior exactly.
- Do not broaden the cleanup beyond the files needed for compileable, explicit access.

Focus on the files that directly read/write AppSharedState fields, especially:
- src/worker/ui_worker.cpp
- src/worker/ui_worker_project.cpp
- any touched UI or engine files

Requirements:
- Keep this pass mechanical.
- Do not redesign workflows.
- Make shared-lane access more explicit and readable.

Validation:
- make -j4


⸻

Stage 4 — Move project restore scratch out of AppProjectState

This is one of the most important ownership fixes.

Step 4.1 — Identify restore scratch vs user-facing project state [done]

Purpose

Split project UI state from project restore pipeline scratch.

Likely files
	•	app_state_project.h
	•	app_state_worker.h
	•	src/worker/ui_worker_project.cpp
	•	src/worker/ui_worker.cpp

Likely fields to move

Anything like:
	•	pending edit masks
	•	pending sample edit arrays
	•	restore staging scratch
	•	transient restore bookkeeping

These should move under either:
	•	AppWorkerState
or
	•	a new ProjectRestoreState

Prompt

Perform a narrow ownership cleanup on project restore scratch state.

Goal:
- Move transient project-restore staging fields out of AppProjectState into a more appropriate runtime owner, likely AppWorkerState or a new ProjectRestoreState owned by the worker path.
- Preserve behavior exactly.
- Do not change project save/load semantics.

Focus on:
- app_state_project.h
- app_state_worker.h
- src/worker/ui_worker_project.cpp
- src/worker/ui_worker.cpp

Requirements:
- Separate user-facing project state from transient restore pipeline scratch.
- Keep field meanings the same.
- Update only the mechanical access sites required.

Validation:
- make -j4

Report:
- which fields moved
- new owner struct
- exact files changed


⸻

Stage 5 — Narrow worker helper signatures

This is where the cleanup becomes real on the worker side.

Step 5.1 — Narrow helpers in src/worker/ui_worker.cpp [done]

Purpose

Stop helper-level whole-app access.

File
	•	src/worker/ui_worker.cpp

What to target

Static helpers that currently take:

AppState& app

Convert each helper to take only what it uses:
	•	AppWorkerState&
	•	AppUiState&
	•	AppProjectState&
	•	AppEngineState&
	•	AppSharedState&
	•	Params&

Do this in small batches, not the whole file at once.

Suggested batch order
	1.	scan helpers
	2.	sample-load helpers
	3.	normalization helpers
	4.	request completion / status helpers

Prompt

Perform the first narrow helper-signature pass in src/worker/ui_worker.cpp.

Goal:
- Replace AppState& parameters in a small batch of static worker helpers with only the specific state buckets/services each helper actually uses.
- Preserve behavior exactly.
- Do not attempt the whole file in one pass.

For this pass, focus only on one small helper cluster, preferably the easiest self-contained cluster in ui_worker.cpp:
- scan helpers, or
- sample-load helpers, or
- normalization helpers

Requirements:
- Do not pass AppState& into the touched helpers.
- Pass only the narrow state buckets/services actually used.
- Keep the diff small and compileable.
- Do not rewrite logic.

Validation:
- make -j4

Report:
- which helper cluster was narrowed
- exact helper signatures changed
- exact files changed
- remaining AppState& helper usage still in ui_worker.cpp


⸻

Step 5.2 — Repeat helper narrowing cluster-by-cluster [done]

Use the same prompt structure for the next worker clusters until ui_worker.cpp is mostly free of helper-level AppState&.

You will likely do this several times.

⸻

Step 5.3 — Narrow helpers in src/worker/ui_worker_project.cpp [done]

Purpose

Do the same for project save/load flow.

File
	•	src/worker/ui_worker_project.cpp

Suggested batch order
	1.	manifest save helpers
	2.	manifest load helpers
	3.	project apply helpers
	4.	project status/reporting helpers

Prompt

Perform a narrow helper-signature pass in src/worker/ui_worker_project.cpp.

Goal:
- Replace AppState& parameters in a small cluster of static project-worker helpers with only the specific state buckets/services actually used.
- Preserve behavior exactly.
- Do not attempt the entire file at once.

For this pass, focus on one coherent helper cluster only, such as:
- manifest save helpers
- manifest load helpers
- apply helpers
- status/reporting helpers

Requirements:
- Do not pass AppState& into the touched helpers.
- Keep the pass mechanical and compile-safe.
- Do not change project behavior.

Validation:
- make -j4

Report:
- helper cluster narrowed
- helper signatures changed
- remaining AppState& usage in ui_worker_project.cpp


⸻

Stage 6 — Remove app_state.h from worker leaf files

Step 6.1 — Narrow includes in worker files [done]

Purpose

Lock in the dependency cleanup.

Likely files
	•	src/worker/ui_worker.cpp
	•	src/worker/ui_worker_project.cpp
	•	src/worker/ui_worker_wav.cpp
	•	maybe other worker leaf files

Prompt

Perform a narrow include cleanup for worker leaf files after helper signature narrowing.

Goal:
- Remove app_state.h from touched worker leaf files wherever it is no longer needed.
- Include only the specific app_state_* headers required.
- Preserve behavior exactly.

Focus only on worker leaf files already touched by the prior narrowing passes.

Requirements:
- Do not broaden the cleanup to unrelated files.
- Keep include changes minimal and compile-safe.

Validation:
- make -j4

Report:
- exact files changed
- which worker files no longer include app_state.h


⸻

Stage 7 — Audit remaining AppState usage repo-wide

This is the “make it stick” phase.

Step 7.1 — Produce an inventory of remaining whole-app access [done]

Purpose

Find what is left before more surgery.

Files

No fixed files; this is an analysis pass.

What to inventory
	•	all .cpp files including app_state.h
	•	all functions taking AppState&
	•	all structs containing AppState*
	•	all UI/worker helpers still using whole-app access

Prompt

Perform an analysis-only audit of remaining whole-app AppState usage.

Goal:
- Inventory all remaining whole-app dependency patterns in the repo.

Find and report:
- every .cpp file still including app_state.h
- every function still taking AppState& or AppState*
- every context struct still containing AppState* or AppState&
- the biggest remaining subsystem hotspots for whole-app access

Do not change code in this pass.

Report:
- grouped by subsystem
- ranked by cleanup value
- with specific file names and brief notes


⸻

Step 7.2 — Pick off the highest-value remaining cluster [done]

After the audit, do one small cluster at a time.

Likely remaining clusters:
	•	main.cpp top-level orchestration, which may stay broad
	•	any leftover UI router/render glue
	•	engine-side files
	•	legacy central UI files like ui_screens.cpp

⸻

Stage 8 — Decide what is allowed to keep AppState

Not everything must be stripped.

Step 8.1 — Declare the acceptable exceptions [done]

Good candidates to keep AppState
	•	main.cpp
	•	possibly one top-level app tick/orchestration function
	•	maybe one composition/setup layer

Bad candidates
	•	UI screen files
	•	worker static helpers
	•	leaf subsystem utilities
	•	small render/event helpers

Prompt

Perform a documentation-only pass to define the stable exceptions for whole-app AppState access.

Goal:
- Document which files/functions are still allowed to accept AppState as top-level orchestration boundaries.
- Clarify that leaf modules and helper-level subsystem code should not use whole-app access.

Edit only docs:
- docs/APP_STATE_NARROWING_PLAN.md
- docs/FILE_MAP.md
- AGENTS.md if needed

Do not change code.


⸻

Best execution order

If you want the order that will give the biggest payoff with the least fake work, do it in exactly this sequence:

Pass order
	1.	Step 0.1 — document the rule
	2.	Step 1.1 — remove AppState* from UiScreenCtx
	3.	Step 1.2 — migrate screen files off ctx.app
	4.	Step 1.3 — remove app_state.h from touched UI leaf files
	5.	Step 3.1 — split AppSharedState into lanes
	6.	Step 3.2 — update shared call sites
	7.	Step 4.1 — move restore scratch out of project state
	8.	Step 5.1+ — narrow ui_worker.cpp helper clusters
	9.	Step 5.3+ — narrow ui_worker_project.cpp helper clusters
	10.	Step 6.1 — remove app_state.h from touched worker files
	11.	Step 7.1 — repo-wide audit of what remains
	12.	Step 8.1 — document final allowed exceptions

⸻

What “success” looks like

After this plan, the repo should have these measurable improvements:
	•	Most UI files no longer include app_state.h
	•	Most UI code no longer accesses ctx.app
	•	AppSharedState is no longer one flat mixed bag
	•	Restore scratch no longer lives in AppProjectState
	•	Most worker helpers no longer take AppState&
	•	AppState is mostly restricted to orchestration boundaries

That is the point where the app-state cleanup becomes real.

⸻

Blunt warning

Do not let Codex turn this into:
	•	another field-shuffling pass
	•	another rename-only pass
	•	another comment pass
	•	another “split header but still pass AppState everywhere” pass

If the signatures and includes do not narrow, the cleanup did not really happen.

⸻
