#pragma once

#include <cstdint>

struct AppEngineState;
struct AppSharedState;
struct UiScreenCtx;
struct UiInputEvent;
class OledPager;

// Shared ADSR helpers (implemented in ui_screen_perform_adsr.cpp; used by render TU).
uint8_t& PerformAdsrRow(AppEngineState& engine, uint8_t layer);
bool PerformAdsrWaveFocusable(uint8_t adsr_row);
bool PerformAdsrStageEnabled(uint8_t adsr_row, uint8_t stage);
uint16_t PerformAdsrStageValue(const AppEngineState& engine, uint8_t layer, uint8_t stage);
bool PerformAdsrLoopStageLocked(const AppSharedState& shared,
                                const AppEngineState& engine,
                                uint8_t layer,
                                uint8_t stage);
bool PerformAdsrStageFocusable(const AppSharedState& shared,
                               const AppEngineState& engine,
                               uint8_t layer,
                               uint8_t adsr_row,
                               uint8_t stage);

// Focus/navigation (ui_screen_perform_adsr.cpp; called from edit TU).
void PerformAdsrEnsureValidFocus(AppEngineState& engine, const AppSharedState& shared, uint8_t layer);

// Value edit / ext-encoder manipulation (ui_screen_perform_adsr_edit.cpp).
bool PerformAdsr_OnEventExtEncoder(UiScreenCtx& ctx, const UiInputEvent& e);

// Drawing/layout (ui_screen_perform_adsr_render.cpp).
void PerformAdsr_DrawMainContent(OledPager& d, UiScreenCtx& ctx);

// Shared seam-length / crossfade-curve overlay (ui_screen_perform_adsr_render.cpp).
// Used by the ADSR read-only focused preview and the dedicated seam-edit screen.
void PerformAdsr_DrawSeamOverlay(OledPager& d,
                                 int preview_x0,
                                 int preview_y0,
                                 int preview_x1,
                                 int preview_y1,
                                 float amount,
                                 float shape,
                                 bool invert,
                                 bool show_curve);
