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

// CRAFT render-then-play preview (non-destructive, no SD write). Begin sets up
// a stepped render of the loaded source through the chain into an SDRAM buffer;
// Step processes one chunk per worker tick and, when finished, hands the result
// to the audio thread for dry auto-play and clears craft_preview_dirty. Step
// returns true when the render (and hand-off) is complete.
bool BeginCraftRenderPreview(AppUiState& ui, AppSharedState& shared);
bool StepCraftRenderPreview(AppUiState& ui, AppSharedState& shared);

// --- Live audition (A1) -------------------------------------------------------
// True if the CRAFT config includes a latency (STFT) effect (currently only
// `fresh`). Zero-latency configs can audition LIVE on the audio thread.
bool CraftConfigHasLatency(const AppUiState& ui);
// Publish the current UI config to the audio-thread live chain (seqlock). Call on
// each audio-affecting edit while a live audition is running.
void PublishCraftCfgLive(const AppUiState& ui, AppSharedState& shared);
// Start a LIVE craft audition: the audio thread runs the chain on the looping
// source in real time, params via the seqlock. No worker render. For cheap configs.
bool CraftPreviewStartLive(AppUiState& ui, AppSharedState& shared);
