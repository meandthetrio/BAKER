# ADSR_V2

Daisy Pod firmware: sampler/voice engine, OLED UI, SD project/manifest persistence.

## Architecture

### Architecture (30-second)

- **`main.cpp`**: Boot and main loop (UI tick, init). Audio ISR callback: `audio_callback.cpp`.
- **`audio_engine.cpp`** and **`voice_engine*.cpp`**: Audio graph and voice/render stack at repo root (see [docs/START_HERE.md](docs/START_HERE.md) for the file split).
- **`src/ui/`**: Router, OLED screens, drawing, and layout.
- **`src/worker/`**: SD card, project load/save, manifest upgrade, and WAV-related worker paths.

Deeper map: [docs/START_HERE.md](docs/START_HERE.md) (entry points, module map, and links to deeper docs).

Build: run `make` in this directory (requires the Daisy ARM toolchain per libDaisy).

## Handoff archives

Do not ship `build/` artifacts, `.git/`, `__MACOSX/`, or `.DS_Store` in ad-hoc zips. Prefer `git archive` from a clean tree, or copy with explicit excludes. Ignores are listed in `.gitignore`.

## Controls

### Simple (no SHIFT)
- Pod Button 1: (unused)
- Pod Button 2: (unused)
- Pod Encoder Click: Toggle saturation on/off
- Pod Encoder Rotate: Adjust saturation drive
- External Encoder Click: Toggle delay on/off
- External Encoder Rotate: Adjust delay mix

### With SHIFT
- SHIFT + Button 1: TestPing (event-queue test)
- SHIFT + Button 2: All Notes Off (panic)
- SHIFT + Pod Encoder Click: Hold‑10 test (push 10 notes + 11th to force one steal)
- SHIFT + Pod Encoder Rotate: Adjust master level
- SHIFT + External Encoder Click: Toggle loop mode (FWD ↔ PINGPONG)
- SHIFT + External Encoder Rotate: Adjust LPF cutoff
