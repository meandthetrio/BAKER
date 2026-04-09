## Step 0.1 — Freeze behavior and extraction-only scope

This section governs the upcoming `ui_screens.cpp` cleanup series.

This cleanup series is extraction-only unless a later step explicitly says otherwise.

Global rules:
- no UI behavior changes
- no input mapping changes
- no navigation changes
- no parameter publish path changes
- no project save/load changes
- no audio-thread changes
- no new shared state unless a later step explicitly requires it
- no renaming of `UiScreenId`
- preserve `GetScreen(...)` behavior exactly until the dedicated registry step
- preserve `OnEnter` vs `on_enter` assignments exactly
- move code verbatim where possible
- do not combine cleanup with redesign
- compile after every extraction step

For this Step 0.1 pass only:
- do not modify `ui_screens.cpp`
- do not move any code yet
- do not extract any functions yet
- only add the cleanup-scope freeze documentation needed to govern the upcoming passes

## Step 0.2 — Mini screen inventory

Current major ownership buckets inside `ui_screens.cpp`:

- Shared draw / math / text helpers
  - top-of-file clamp / accel helpers, tiny font and glyph code, bitmap helpers, dotted rects, micro/tiny text, waveform and control drawing helpers
  - includes generic-looking draw code plus some helpers that are actually screen-owned and should move with their screen later

- Main / presets / record screens
  - main menu: `MainMenu_OnEvent`, `MainMenu_OnEnter`, `MainMenu_Render`, menu label/index helpers, main-menu art
  - presets: `Presets_OnEvent`, `Presets_Render`
  - record: `Record_*` state/reset/start/preview helpers plus `Record_OnEnter`, `Record_OnExit`, `Record_OnEvent`, `Record_Render`
  - record is a screen-local business-logic cluster and should stay together when extracted

- Perform menu + perform subpages
  - perform menu: `PerformMenu_OnEvent`, `PerformMenu_OnEnter`, `PerformMenu_Render`, perform-menu label/index helpers, perform-menu art
  - engine / wave edit: `PerformEngine_*`, `PerformWaveEdit_*`
  - keyzone / ADSR / emphasis / process: `PerformKeyzone_*`, `PerformAdsr_*`, `PerformEmphasis_*`, `PerformProcess_*`
  - perform-local helper clusters live nearby: ADSR stage/focus helpers, loop crossfade math, engine/tuning helpers, EQ / FX detail drawing, keyzone drawing templates

- HUD / FX / MOD / MACRO / SHIFT screens
  - HUD: `EnsureHudMenu`, `Hud_OnEvent`, `Hud_Render`
  - FX: `Fx_OnEvent`, `Fx_Render`
  - MOD: `Mod_OnEvent`, `Mod_Render`
  - MACRO: `Macro_OnEvent`, `Macro_Render`
  - SHIFT: `ShiftMenu_OnScreenEnter`, `ShiftMenu_OnEvent`, `ShiftMenu_Render`
  - shift contains screen-local settings/project-action flow and should not be mixed into unrelated screen passes

- Router / navigation / registry glue
  - `UiNav_Active`, `UiNav_Push`, `UiNav_Pop`
  - `GetScreen(...)` static screen registry
  - `UiRouter_DispatchEvent`, `UiRouter_Render`
  - registry currently also wires in already-extracted screens such as project status, SD browser, delete confirm, and sample edit

Extraction map notes:
- safest earlier extractions: router / nav glue, then `GetScreen(...)`, then main/presets/HUD
- mid-risk extractions: FX, MOD, MACRO, SHIFT
- later / tighter-coupled areas: record and the perform subpages, especially ADSR, process, and wave-edit helpers that share local state assumptions and special BACK / `on_enter` behavior

Stage 0 — Baseline and guardrails

Step 0.1 — Freeze behavior and declare extraction-only scope

Before touching files, define these rules for the cleanup pass:
	•	no UI behavior changes
	•	no input mapping changes
	•	no navigation changes
	•	no parameter publish path changes
	•	no new shared state
	•	no renaming of UiScreenId
	•	no moving logic into audio-thread code
	•	compile after every extraction
	•	keep GetScreen(...) behavior identical until dedicated registry step

Step 0.2 — Create a mini screen inventory from current file

From the current file, explicitly list what lives where. Roughly:
	•	shared draw/math/text helpers: top of file through menu art/helpers
	•	main/presets/record
	•	perform menu + perform subpages
	•	HUD / FX / MOD / MACRO / SHIFT
	•	registry/router

This inventory should be added to a doc or temporary checklist before extraction starts.

Step 0.3 — Add a narrow internal header contract first

Create one internal header for extracted screen functions, extending the pattern already started in src/ui/ui_screens_internal.h.

Goal:
	•	centralize forward declarations for extracted screen functions
	•	avoid circular include chaos later
	•	make each extraction mostly “move code + add prototypes”

Do this before moving more code.

⸻

Stage 1 — Separate the global router/registry from screen content

This is one of the safest early wins.

Step 1.1 — Move navigation/router glue out of ui_screens.cpp

Extract these into a new file like:
	•	src/ui/ui_router.cpp

Move:
	•	UiNav_Push
	•	UiNav_Pop
	•	UiRouter_DispatchEvent
	•	UiRouter_Render

Keep:
	•	signatures unchanged
	•	includes minimal
	•	logic unchanged

Why first:
	•	this is global infrastructure, not screen-specific
	•	it reduces the tail of ui_screens.cpp
	•	it makes later screen extraction cleaner

Step 1.2 — Move GetScreen(...) into a dedicated registry file

Extract into:
	•	src/ui/ui_screen_registry.cpp

This file should do nothing except hold the static UiScreen definitions and the switch(id) registry.

Why this helps:
	•	later screen splits become “change one include / one function reference”
	•	registry stays as the central map of where screens live
	•	ui_screens.cpp stops being both content and registry

Do not redesign the registry yet. Just move it.

⸻

Stage 2 — Isolate shared drawing/helpers from actual screens

This is the most important structural cleanup.

Step 2.1 — Extract generic text/font helpers

Create something like:
	•	src/ui/ui_draw_text.cpp
	•	src/ui/ui_draw_text.h

Move only helpers that are clearly reusable and not screen-owned, such as:
	•	DrawScaledText6x8
	•	tiny/micro string helpers
	•	glyph width helpers
	•	string formatting helpers that are purely presentational

Do not move screen-specific formatters yet if they are tightly coupled to one screen family.

Step 2.2 — Extract generic shape/widget helpers

Create something like:
	•	src/ui/ui_draw_shapes.cpp
	•	src/ui/ui_draw_shapes.h

Move helpers like:
	•	dotted rects
	•	bitmap helpers
	•	circle pixels
	•	other generic drawing primitives

Step 2.3 — Extract reusable control visuals

Create something like:
	•	src/ui/ui_draw_controls.cpp
	•	src/ui/ui_draw_controls.h

Move helpers like:
	•	vertical faders
	•	FX detail fader drawing
	•	loop crossfade curve drawing
	•	EQ graph rendering helpers if they are screen-agnostic enough

Important:
	•	only move helpers that are truly drawing-focused
	•	if a helper mutates AppState or publishes params, it is not a draw helper and should stay with its owning screen domain

Step 2.4 — Leave ambiguous helpers in place temporarily

Some helpers may look reusable but are tightly coupled to perform/record logic. Leave those alone until the owning domain is extracted.

This is safer than over-generalizing.

⸻

Stage 3 — Extract the smallest/least-coupled screen families first

You already have a pattern in src/ui/ with status/browser files. Continue that pattern.

Step 3.1 — Extract Main Menu and Presets

Create:
	•	src/ui/ui_screen_main.cpp

Move:
	•	MainMenu_OnEvent
	•	MainMenu_OnEnter
	•	MainMenu_Render
	•	Presets_OnEvent
	•	Presets_Render
	•	menu art helpers only if they are used exclusively by these screens

Why first:
	•	low risk
	•	limited side effects
	•	good way to validate the pattern

Step 3.2 — Extract HUD

Create:
	•	src/ui/ui_screen_hud.cpp

Move:
	•	EnsureHudMenu
	•	Hud_OnEvent
	•	Hud_Render

Why early:
	•	relatively bounded
	•	central but not as huge as perform/record
	•	good checkpoint for top-level non-perform screens

Step 3.3 — Extract FX / MOD / MACRO as separate files

Create:
	•	src/ui/ui_screen_fx.cpp
	•	src/ui/ui_screen_mod.cpp
	•	src/ui/ui_screen_macro.cpp

Move each screen’s event/render pair and only the helpers they exclusively use.

These are good mid-risk targets because they are reasonably bounded and help collapse the giant file fast.

⸻

Stage 4 — Extract Shift Menu separately

Step 4.1 — Move Shift Menu into its own file

Create:
	•	src/ui/ui_screen_shift.cpp

Move:
	•	ShiftMenu_OnScreenEnter
	•	ShiftMenu_OnEvent
	•	ShiftMenu_Render

Why separate:
	•	it has project/settings-ish behavior
	•	it is big enough to justify isolation
	•	it should not stay tangled with perform or record screens

This also helps later if project actions/settings continue moving out of old screen code.

⸻

Stage 5 — Extract Record screen as its own domain

Record looks like its own mini subsystem, so treat it that way.

Step 5.1 — Create record screen file

Create:
	•	src/ui/ui_screen_record.cpp

Move:
	•	Record_ResetLiveWave
	•	Record_StopPreview
	•	Record_PrepareRecordingUiState
	•	Record_StartRecording
	•	Record_RenderReadyCuzStyle
	•	Record_OnEvent
	•	Record_Render
	•	Record_OnEnter
	•	Record_OnExit

Why as one chunk:
	•	record has internal helper cohesion
	•	breaking it across multiple files too early would increase risk

Step 5.2 — Keep record-local helper ownership intact

Do not try to “genericize” record helpers during extraction. Keep them local/static inside the new record file unless obviously shared.

⸻

Stage 6 — Extract Perform menu shell before the heavy perform subpages

Do not start with ADSR or Process first. Start with the shallow shell.

Step 6.1 — Move Perform Menu into its own file

Create:
	•	src/ui/ui_screen_perform_menu.cpp

Move:
	•	NextPerformMenuIndex
	•	DrawPerformMenuFriendStyle if only used there
	•	PerformMenu_OnEvent
	•	PerformMenu_OnEnter
	•	PerformMenu_Render

Why:
	•	this decouples the perform entry screen from the heavy parameter editors
	•	safe first slice of the perform family

⸻

Stage 7 — Split Perform subpages one screen at a time

This is the highest-risk stage, so do it gradually.

Step 7.1 — Extract Perform Engine

Create:
	•	src/ui/ui_screen_perform_engine.cpp

Move:
	•	FormatMidiNoteName
	•	FormatDbTenths
	•	DriveModeLabel
	•	FormatProcessLevelDb
	•	ProcessLevelToKnobNorm
	•	PublishEngineLayerParams
	•	EngineRefreshLoadedMetadata
	•	waveform preview helpers used only by engine screen
	•	PerformEngine_OnScreenEnter
	•	PerformEngine_OnEnter
	•	PerformEngine_OnEvent
	•	PerformEngine_Render

Reason:
	•	engine screen appears self-contained enough
	•	it has clear metadata/publish ownership

Step 7.2 — Extract Perform Wave Edit

Create:
	•	src/ui/ui_screen_perform_wave_edit.cpp

Move:
	•	PerformWaveEdit_Render
	•	PerformWaveEdit_OnScreenEnter
	•	PerformWaveEdit_OnEnter
	•	PerformWaveEdit_OnEvent
	•	any wave-edit-only drawing helpers

Step 7.3 — Extract Perform Keyzone

Create:
	•	src/ui/ui_screen_perform_keyzone.cpp

Move:
	•	KeyzonePageFromNote
	•	DrawKeyzoneUiRangeTemplate
	•	PerformKeyzone_OnEvent
	•	PerformKeyzone_Render

Step 7.4 — Extract Perform ADSR

Create:
	•	src/ui/ui_screen_perform_adsr.cpp

Move:
	•	ClampInt
	•	ADSR stage min/max helpers
	•	ADSR focus helpers
	•	SetPerformAdsrStageValue
	•	PerformAdsr_OnEvent
	•	PerformAdsr_OnScreenEnter
	•	PerformAdsr_Render

This is a good place to keep all ADSR-specific math and focus logic together.

Step 7.5 — Extract Perform Emphasis

Create:
	•	src/ui/ui_screen_perform_emphasis.cpp

Move:
	•	PerformEmphasis_OnEvent
	•	PerformEmphasis_OnScreenEnter
	•	PerformEmphasis_Render

Step 7.6 — Extract Perform Process

Create:
	•	src/ui/ui_screen_perform_process.cpp

Move:
	•	ComputePerformLoopCrossfadeWeight
	•	DrawFxDetailScreen
	•	DrawEqGraphScreen
	•	PerformProcess_OnEvent
	•	PerformProcess_Render

This one is probably the most helper-heavy after ADSR/Engine, so do it after the pattern is established.

⸻

Stage 8 — Final helper cleanup pass

Only after all screen families are out.

Step 8.1 — Re-open leftover helpers in the old file

At this point, whatever is still sitting in ui_screens.cpp is likely:
	•	truly shared
	•	miscategorized
	•	dead

Sort remaining code into:
	•	keep as shared helper module
	•	move into specific owner file
	•	delete if unused

Step 8.2 — Eliminate duplicate helper implementations

I noticed waveform-preview style helpers appear more than once in the file. After extractions, compare for duplicates and collapse only when behavior is clearly identical.

Do this late, not early.

Step 8.3 — Decide whether ui_screens.cpp should survive at all

Best end state is likely:
	•	ui_screens.h remains
	•	ui_screens.cpp disappears entirely
or becomes a tiny legacy wrapper with nothing but maybe shared declarations if absolutely needed

But do not force that early.

⸻

Safe checkpoint order

For the safest sequence, do it in this order:
	1.	internal header cleanup
	2.	router extraction
	3.	screen registry extraction
	4.	generic text helpers
	5.	generic shape/widget helpers
	6.	main/presets
	7.	HUD
	8.	FX
	9.	MOD
	10.	MACRO
	11.	shift menu
	12.	record
	13.	perform menu
	14.	perform engine
	15.	perform wave edit
	16.	perform keyzone
	17.	perform ADSR
	18.	perform emphasis
	19.	perform process
	20.	leftover/dead helper cleanup

Best target structure

A good end state would look roughly like this:
	•	src/ui/ui_router.cpp
	•	src/ui/ui_screen_registry.cpp
	•	src/ui/ui_draw_text.cpp
	•	src/ui/ui_draw_shapes.cpp
	•	src/ui/ui_draw_controls.cpp
	•	src/ui/ui_screen_main.cpp
	•	src/ui/ui_screen_record.cpp
	•	src/ui/ui_screen_hud.cpp
	•	src/ui/ui_screen_fx.cpp
	•	src/ui/ui_screen_mod.cpp
	•	src/ui/ui_screen_macro.cpp
	•	src/ui/ui_screen_shift.cpp
	•	src/ui/ui_screen_perform_menu.cpp
	•	src/ui/ui_screen_perform_engine.cpp
	•	src/ui/ui_screen_perform_wave_edit.cpp
	•	src/ui/ui_screen_perform_keyzone.cpp
	•	src/ui/ui_screen_perform_adsr.cpp
	•	src/ui/ui_screen_perform_emphasis.cpp
	•	src/ui/ui_screen_perform_process.cpp
