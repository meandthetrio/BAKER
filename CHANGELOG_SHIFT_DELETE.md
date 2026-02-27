# SHIFT Menu + Safe Delete (2026-02-27)

## What changed
- POD BUTTON1 now toggles a locked SHIFT menu (no longer loads samples).
- EXT encoder click (R encoder) is the only ENTER/LOAD action.
- SHIFT menu contains:
  - DELETE: opens SD BROWSE in delete-select mode
  - VOLUME: edits master level (2% per detent), click to toggle edit
- Safe delete flow:
  - SHIFT → DELETE → SD list → EXT click selects file → confirmation screen
  - Confirmation screen: "ARE YOU SURE?  R=YES  L=NO"
  - R deletes (FatFS f_unlink) and triggers a rescan.

## Files touched
- ui_logic.cpp
- ui_screens.h
- ui_screens.cpp
- ui_requests.h
- ui_worker.cpp
- ui_overlay.cpp
- app_state.h
