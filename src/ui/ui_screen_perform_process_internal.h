#pragma once

#include <cstddef>
#include <cstdint>

class OledPager;
struct AppEngineState;
struct PerformParamsTargets;
struct UiScreenCtx;
struct UiInputEvent;

constexpr float kProcessLayerLevelUiMax = 2.0f;

struct ProcessLayerVolumeUiState
{
    char  value_text[2][12];
    float angle_rad[2];
};

float Clamp01(float x);
float ClampEqTiltDb(float x);
float ClampEqQ(float x);

void FormatProcessLevelDb(float level, char* out, size_t out_n);
float ProcessLevelToKnobNorm(float level);

ProcessLayerVolumeUiState ProcessSyncLayerVolumeUiState(AppEngineState& engine,
                                                         const PerformParamsTargets& t);

void DrawProcessLayerVolumePane(OledPager& d,
                                const ProcessLayerVolumeUiState& ui,
                                uint8_t main_cursor,
                                int left_y,
                                int left_h);

void DrawEqGraphScreen(OledPager& d, const PerformParamsTargets& t, uint32_t now_ms);

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
uint8_t ProcessDetailParamCount(uint8_t fx_id);
void ProcessHandleLayerToggle(UiScreenCtx& ctx);
void ProcessHandleLayerMuteToggle(UiScreenCtx& ctx, uint8_t layer);
void ProcessHandleLayerVolumeEdit(UiScreenCtx& ctx, const UiInputEvent& e, uint8_t layer);
void ProcessEditEqGraph(UiScreenCtx& ctx, float delta);
bool ProcessEditFxDetail(UiScreenCtx& ctx,
                         const UiInputEvent& e,
                         uint8_t fx_id,
                         uint8_t pidx,
                         float delta);
