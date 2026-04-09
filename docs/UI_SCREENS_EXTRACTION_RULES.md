Rules for each individual extraction step

Every step should follow the same template:

For each step
	•	move code only
	•	keep functions static where possible
	•	do not rename functions unless there is a collision
	•	do not change signatures unless strictly necessary
	•	update ui_screens_internal.h only as needed
	•	compile immediately
	•	compare file-local includes and remove only obvious unused headers
	•	verify GetScreen(...) references still point to the same handlers
	•	verify no screen lost its OnEnter vs on_enter assignment distinction

That last one matters because your UiScreen struct has both:
	•	OnEnter
	•	on_enter

and they are not interchangeable.

⸻

What not to do

Do not do any of these during cleanup:
	•	do not redesign the screen system
	•	do not replace static functions with classes
	•	do not merge all drawing helpers into a giant “ui_utils.cpp”
	•	do not start moving app logic into headers
	•	do not “simplify” focus logic while extracting
	•	do not combine record + perform just because both are screen code
	•	do not change project/save/load behavior as part of screen cleanup
	•	do not move worker or request logic in the same pass
