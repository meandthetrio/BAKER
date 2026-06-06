# Loop Seam Freeze — High-Level Reference

> Note: This is NOT related to anything labeled "Bake"/BakeMenu in the code.
> That is a separate, unrelated feature.

## What this feature is

Let the user dial in a loop's crossfade (seam) length and curve **by ear, in real
time**, on a sample that may not loop cleanly on its own. Once the seam sounds
seamless, "freeze" it — bake those exact seam settings permanently into the
sample's audio so normal playing is cheap and identical every time.

The point: you pay the cost of the fancy seam math only while listening/editing
(one note at a time). After freezing, playing chords or fast MIDI is as cheap as
playing any ordinary looped sample.

---

## What already exists today

- **The seam crossfade itself works live.** When loop mode is on, the engine
  already blends the loop's tail into its head every time a note plays, using a
  selectable length and curve. This is the thing being auditioned.
- **The length and curve are already adjustable** from the perform/ADSR edit
  area, per layer.
- **The math that does the blend is self-contained** and can be reused to do the
  same blend offline (i.e. to write it into the audio instead of doing it live).
- **There is spare audio memory** to hold a frozen copy of the sample.
- **There is already a safe way to hand a new/edited sample to the audio engine**
  without glitching playback (used today when loading/editing samples).
- **Incoming MIDI already drives notes**, so the audition source is in place.

In short: every low-level piece needed already exists. What's missing is the
dedicated screen, the "force mono while editing" behavior, and the freeze step.

---

## What needs to be built

1. **A new dedicated Seam Edit screen.**
   - Entered by clicking the right encoder while the WAV preview is focused.
   - Shows the seam length and curve and lets the user adjust them.

2. **Force monophonic on entry.**
   - While in this screen the device plays only one voice at a time.
   - This keeps the live seam math cheap and lets the seam respond instantly to
     encoder moves. It's also the only sensible way to judge a seam by ear.

3. **Real-time audition from incoming MIDI.**
   - Notes played over MIDI sound through the live seam settings as they're
     edited, so the user can hear the seam appear/disappear as they turn the
     encoder.

4. **Freeze on exit.**
   - When the user leaves the screen, the current seam length + curve are baked
     into a copy of the sample's audio.
   - Polyphony is restored, and from then on every note plays the frozen audio
     with no live seam math.

5. **A clean exit handoff.**
   - The freeze takes a few milliseconds and must happen off the audio path.
   - Keep the live (mono) sound until the frozen copy is ready, then swap to it
     and restore polyphony — no dropped or glitched notes if MIDI arrives during
     the swap.

6. **Re-bake instead of storing the frozen audio.**
   - Projects save the seam settings (length + curve), not the baked audio.
   - On load, the freeze is recalculated from the original sample. Keeps the
     source audio pristine and saves storage.

---

## Open / deferred items

- **Region (start/end) edits invalidate a freeze.** A freeze is only valid for a
  fixed loop region + seam length + curve. Changing the loop region means the
  freeze must be recalculated. Likely handled via the engine -> Trim screen flow,
  which needs to be worked through first.
- **Audition note behavior detail:** confirm whether the looping audition note
  should keep sounding after key release while editing, so the user can keep both
  hands on the encoders.

---

## Decisions locked in

- Audition source: **incoming MIDI**.
- Freeze is **recalculated**, not stored (saved per project as settings; rebuilt
  on load).
- Sample is **mono** at this stage; L/R layer routing happens afterward and is
  unaffected.
- **Rebake on load.**
