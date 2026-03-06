# PRESETS MENU FOCUS REFERENCE

## Notes
- This file documents the CURRENT IMPLEMENTED PRESETS branch only.
- Source of truth is code (`ui_screens.*`, `ui_logic.cpp`, `app_state.h`).
- This branch currently contains a placeholder PRESETS screen.

## Screen Inventory
- Start (Main Menu entry point)
- Presets

## UI Tree
- Start (Main Menu)
  - Presets

## Screen Reference

### Start (Main Menu, PRESETS path)
- Parent: root
- Entered by: boot initialization (`ui_nav.stack[0] = UiScreenId::Start`) and BACK pop from child screens.
- Exited by: `kUiBtnExtEnc` on selected menu row.

#### Focusable Objects
1. **Main menu PRESETS row (`main_menu_index == 0`)**
- Type: menu item
- Purpose: enter PRESETS branch.
- Behavior:
  - `kUiEncPod` selects the PRESETS row.
  - `kUiBtnExtEnc` pushes `UiScreenId::Presets`.
  - `kUiBtnPodEnc` at root does not pop further.
- Result:
  - Navigates into PRESETS screen.
- Notes:
  - Main menu rows are `PRESETS`, `RECORD`, `PERFORM`.

### Presets (`UiScreenId::Presets`)
- Parent: Start (Main Menu)
- Entered by: Start PRESETS row + `kUiBtnExtEnc`.
- Exited by: `kUiBtnPodEnc` BACK pop to Start.

#### Focusable Objects
1. **None (placeholder)**
- Type: placeholder screen body
- Purpose: reserved for future preset list.
- Behavior:
  - `Presets_OnEvent` does not consume encoder/click events.
  - BACK handling is done by router pop.
- Result:
  - No in-screen selection, edit, or action trigger.
- Notes:
  - Header renders `PRESETS`; body is blank by design in current code.
