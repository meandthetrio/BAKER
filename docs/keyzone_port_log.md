# Keyzone Screen Port Log

**Date:** 2026-04-16  
**Source:** `oled_ui_sim/oled_ui_sim_skeleton/ui_ref/ui_screens.cpp`  
**Target:** `ADSR_V2/src/ui/ui_screen_perform_keyzone.cpp`  
**Goal:** Exact visual and button-behavior match to the OLED sim's singular `PerformKeyzone` screen.

---

## What Changed and Why

### 1. `app_state_engine.h` — `PerformKeyzoneState`

**Removed fields** (only used by the old two-layer separate-view screen):
- `uint8_t perform_keyzone_marker_focus` — tracked LO/HI arrow marker focus for the old 3D keyboard view
- `uint8_t perform_keyzone_window_octave[2]` — tracked the scrolling octave window for the old 3D keyboard

**Added field:**
- `bool perform_keyzone_is_split = false` — keyzone UI mode / edit-state flag. It changes the screen’s focus model and which subscreens are reachable. Layer note eligibility in the current code is driven by `perform_keyzone_lo_note[]` and `perform_keyzone_hi_note[]`; FULL vs SPLIT behavior is achieved by how the UI writes those ranges (not by a separate `perform_keyzone_is_split` check in the main MIDI note path). Stored in `AppEngineState` with the other perform keyzone fields.

**Changed defaults:**
- `perform_keyzone_lo_note[2]`: `{48u, 48u}` → `{12u, 12u}` (C0 — full range default matching sim)
- `perform_keyzone_hi_note[2]`: `{60u, 60u}` → `{108u, 108u}` (C8 — full range default matching sim)

The old C3–C4 defaults were narrow and showed as a thin bar. The sim defaults to the full keyboard span in FULL mode.

---

### 2. `app_state_ui.h` — `AppUiState`

**Added field:**
- `uint8_t perform_keyzone_focus = 0` — which UI element on the keyzone screen currently has focus. Pure UI-navigation state; does not affect audio. Placed in `AppUiState`.

Focus values:
| Value | FULL mode | SPLIT mode |
|-------|-----------|------------|
| 0 | FULL/SPLIT button | FULL/SPLIT button |
| 1 | "velocity Mod" button | split-point label |
| 2 | *(unused)* | mod block A rectangle |
| 3 | *(unused)* | mod block B rectangle |

---

### 3. `src/ui/ui_screen_perform_keyzone.cpp` — Full rewrite

The entire file was replaced. The old screen showed one layer at a time (A or B, toggled with Btn2/SELECT), with a 3D scrolling piano keyboard. The new screen matches the sim exactly.

#### Visual layout (new)
```
┌──────────────────────────── [kyzn a+b] ┐  ← header (top-right)
│ [FULL/SPLIT]    [split pt]             │  ← status row (16px tall)
│ ████████ layer a ████████              │  ← layer A bar (9px)
│ ████████ layer b ████████              │  ← layer B bar (9px)
│ 🎹 keyboard bitmap (128×13)            │
│ [velocity Mod]  or  [mod blk A][B]     │  ← bottom area
└────────────────────────────────────────┘
```
Both layers are always visible simultaneously. The header always reads "kyzn a+b".

#### Button behavior (new vs old)

| Input | Old behavior | New behavior |
|---|---|---|
| **Btn2 (SELECT)** | Toggle active layer A↔B | **Removed** — no layer toggle on this screen |
| **L encoder (Pod)** | Move LO note of active layer | Cycle focus through UI elements |
| **R encoder (Ext) rotate** | Move HI note of active layer | In SPLIT mode: move split point between A and B |
| **R encoder (Ext) click** | rshift toggle of split | Activate focused element: toggle FULL/SPLIT at focus 0, or `PerformKeyzone_TryPushSubscreen` for vel mod / mod blocks when focus matches |
| **rshift + Ext click** | Set FULL range or default split | *(removed)* |

#### State mapping from sim (`ctx.app->`) to ADSR_V2 split state

| Sim field | ADSR_V2 destination |
|---|---|
| `app.perform_keyzone_is_split` | `engine.keyzone.perform_keyzone_is_split` |
| `app.perform_keyzone_focus` | `ui.perform_keyzone_focus` |
| `app.perform_keyzone_lo_note[i]` | `engine.keyzone.perform_keyzone_lo_note[i]` |
| `app.perform_keyzone_hi_note[i]` | `engine.keyzone.perform_keyzone_hi_note[i]` |
| `app.perform_layer` | `engine.perform_nav.perform_layer` |
| `app.ui_dirty` | `ui.ui_dirty` |
| `app.ui_nav` | `ui.ui_nav` |
| `app.engine_header_invert_until_ms` | `engine.layer.engine_header_invert_until_ms` |
| `EngineRefreshLoadedMetadata(app)` | `EngineRefreshLoadedMetadata(*ctx.ui, *ctx.engine, *ctx.shared)` |

#### Bitmap
`kPerformKeyzoneKeyboard128x16` (256 bytes) — copied verbatim from the sim. Rendered at `y = kStatusH + 2*kBarH` (y=34), 128 wide, 13 rows drawn, 16-byte stride.

#### Subscreen navigation (`PerformKeyzone_TryPushSubscreen`)

`PerformKeyzone_TryPushSubscreen(...)` in `ui_screen_perform_keyzone.cpp` calls `UiNav_Push` when focus and split mode match:

- `UiScreenId::VelocityMod` — not split (`perform_keyzone_is_split == false`) and `perform_keyzone_focus == 1`
- `UiScreenId::ModBlockA` — split and `perform_keyzone_focus == 2`
- `UiScreenId::ModBlockB` — split and `perform_keyzone_focus == 3`

It is invoked from `PerformKeyzone_OnEnter` (external-encoder focus entry) and from `PerformKeyzone_OnEvent` on external button down when the same conditions apply. The corresponding screens are registered in `src/ui/ui_screen_registry.cpp` (implementations in `src/ui/ui_screen_perform_velmod.cpp`).

---

### 4. `src/ui/ui_screens_internal.h`

**Added declaration:**
```cpp
bool PerformKeyzone_OnEnter(UiScreenCtx& ctx);
```
This is the `on_enter` hook (external-encoder focus entry, distinct from `OnEnter`/`OnExit` stack transitions).

---

### 5. `src/ui/ui_screen_registry.cpp`

**Updated `perform_keyzone` entry** to wire `PerformKeyzone_OnEnter` as the `on_enter` slot:
```cpp
// Before:
static const UiScreen perform_keyzone{..., nullptr, nullptr, PerformKeyzone_OnEvent, PerformKeyzone_Render};

// After:
static const UiScreen perform_keyzone{..., nullptr, nullptr, PerformKeyzone_OnEvent, PerformKeyzone_Render, PerformKeyzone_OnEnter};
```

---

## Fields Left Unchanged (no migration needed)

- `project_manifest.h` keyzone defaults (`48u/60u`) — these represent on-disk project format for older saves; not changed to preserve backward compatibility when loading projects.
- `params.h` keyzone defaults — same rationale.
- `keygroups.h` constants (`kPerformKeyzoneMinNote`, `kPerformKeyzoneMaxNote`, etc.) — still used by `ClampProjectKeyzoneRange` in the worker; not removed.
