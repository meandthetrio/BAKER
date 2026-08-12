#pragma once

#include <cstddef>
#include <cstdint>

class OledPager;
struct PerformParamsTargets;
struct UiScreenCtx;
struct UiInputEvent;

struct ProcessLayerVolumeUiState
{
    float cutoff_angle; // global process LPF cutoff knob angle
    float res_angle;    // global process LPF resonance knob angle
};

float Clamp01(float x);
float ClampEqTiltDb(float x);
float ClampEqQ(float x);
float ClampEqShelfGain(float x);
float ClampEqFilterQ(float x);

ProcessLayerVolumeUiState ProcessSyncLayerVolumeUiState(const PerformParamsTargets& t);

void DrawProcessLayerVolumePane(OledPager& d,
                                const ProcessLayerVolumeUiState& ui,
                                uint8_t main_cursor,
                                int left_y,
                                int left_h);

void DrawEqGraphScreen(OledPager& d, const PerformParamsTargets& t, uint32_t now_ms, uint8_t eq_band, bool rshift);

// Draw helpers (ui_screen_perform_process_draw.cpp).
void DrawProcessFxReorderOverlay(OledPager& d,
                                 int fader_x,
                                 int fader_y,
                                 int fader_w,
                                 int fader_h,
                                 int32_t selected_index,
                                 bool rshift_held);
void DrawFxDetailScreen(OledPager& d,
                        const PerformParamsTargets& t,
                        uint8_t fx_id,
                        uint8_t selected_param,
                        uint32_t now_ms,
                        bool rshift_held);

// Edit / parameter adjustment (ui_screen_perform_process_edit.cpp).
float UiDeltaNormAccelerated(int enc_delta, uint32_t t_ms, uint32_t& last_t_ms, float base_step);
uint8_t ProcessDetailParamCount(uint8_t fx_id, uint8_t sat_mode = 0);
void ProcessHandleLayerToggle(UiScreenCtx& ctx);
void ProcessHandleProcessCutoffEdit(UiScreenCtx& ctx, const UiInputEvent& e);
void ProcessHandleProcessResonanceEdit(UiScreenCtx& ctx, const UiInputEvent& e);
void ProcessEditEqGraph(UiScreenCtx& ctx, float delta);
bool ProcessEditFxDetail(UiScreenCtx& ctx,
                         const UiInputEvent& e,
                         uint8_t fx_id,
                         uint8_t pidx,
                         float delta);
