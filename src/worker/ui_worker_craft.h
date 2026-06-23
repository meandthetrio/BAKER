#pragma once

#include <cstdint>

struct AppUiState;
struct AppSharedState;

// CRAFT render: process the sample currently resident in the bake-preview SDRAM
// buffer (shared.bake_preview.sample) through the CRAFT effect chain built from
// the UI state, write the result into the SD-manage scratch buffer, and hand it
// to the existing save pipeline (StartSave / SaveStep).
//
// overwrite == false -> save as a new auto-incremented REND####.WAV.
// overwrite == true  -> atomically replace the loaded source (ui.craft_loaded_path).
//
// Runs on the worker thread. The DSP pass is synchronous (one call); the SD
// write is then stepped by SaveStep via the normal request loop. Returns false
// if there is no loaded sample, the chain is empty, or save setup fails.
bool StartCraftRender(AppUiState& ui, AppSharedState& shared, bool overwrite);
