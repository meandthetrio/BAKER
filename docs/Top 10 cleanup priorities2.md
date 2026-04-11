Top 10 cleanup priorities, ranked highest payoff to lowest
	1.	Further split voice_engine_render_voice.cpp by lifecycle phase, not just by file size
Separate sample fetch, envelope progression, loop advancement, steal/xfade path, and bus write policy. This is the highest-risk readability hotspot.
	2.	Narrow audio_callback.cpp into explicit sub-owners
Recording bridge, sample/edit handoff apply, macro smoothing, engine param application, and monitor mix should not all live inline in the callback body.
	3.	Refactor src/ui/ui_screen_record.cpp into controller/state-transition/render units
Right now it is too much screen + recording workflow + graphics cache in one place.
	4.	Restructure project persistence around clearer phases/types instead of helper piles
ui_worker_project.cpp should stop being the place where all project-related behavior accumulates.
	5.	Kill or further shrink ui_screens.cpp
Move remaining ownership to truly local screen modules or to purpose-specific utility modules.
	6.	Reduce special-case routing rules in src/ui/ui_router.cpp and src/ui/ui_logic.cpp
Too many bespoke behaviors are encoded in central dispatch instead of screen-local or mode-local abstractions.
	7.	Move more domain policy out of main.cpp
Boot wiring should be boring. It is still doing too much real application behavior.
	8.	Tighten AppEngineState around actual engine/editor boundaries
It is organized, but still too overloaded with perform/session/editor details.
	9.	Rework manifest upgrade code to reduce repetitive upgrade staircases
The current version chain is readable only through brute force. That will age badly.
	10.	Fix repo packaging hygiene and root layout discipline
No .git, build/, __MACOSX, or .DS_Store in handoff archives. And stop growing the root.