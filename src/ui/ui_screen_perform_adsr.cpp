#include "ui_screens_internal.h"
#include "ui_screen_perform_adsr_internal.h"

#include <cstdio>

#include "app_state_ui.h"
#include "app_state_engine.h"
#include "app_state_recording.h"
#include "app_state_project.h"
#include "app_state_diagnostics.h"
#include "app_state_shared.h"
#include "app_state_worker.h"
#include "oled_pager.h"
#include "sample_edit.h"
#include "ui_input.h"

static constexpr int32_t kAdsrRowCount = 3;
static constexpr int32_t kAdsrRowOneShot = 0;
static constexpr int32_t kAdsrRowLoop = 1;
static constexpr int32_t kAdsrRowAdsr = 2;
static constexpr int32_t kAdsrStageCount = 4;

static int ClampInt(int v, int lo, int hi)
{
    if(v < lo)
        return lo;
    if(v > hi)
        return hi;
    return v;
}

uint16_t PerformAdsrStageValue(const AppEngineState& engine, uint8_t layer, uint8_t stage)
{
    const uint8_t safe_layer = layer & 1u;
    switch(stage % static_cast<uint8_t>(kAdsrStageCount))
    {
        case 0: return engine.adsr.perform_adsr_loop_attack[safe_layer];
        case 1: return engine.adsr.perform_adsr_loop_decay[safe_layer];
        case 2: return engine.adsr.perform_adsr_loop_sustain[safe_layer];
        default: return engine.adsr.perform_adsr_loop_release[safe_layer];
    }
}

uint8_t& PerformAdsrRow(AppEngineState& engine, uint8_t layer)
{
    return engine.adsr.perform_adsr_row[layer & 1u];
}

bool PerformAdsrStageEnabled(uint8_t adsr_row, uint8_t stage)
{
    const uint8_t safe_row = adsr_row % static_cast<uint8_t>(kAdsrRowCount);
    const uint8_t safe_stage = stage % static_cast<uint8_t>(kAdsrStageCount);
    if(safe_row == static_cast<uint8_t>(kAdsrRowOneShot)
       && (safe_stage == 1u || safe_stage == 2u))
        return false;
    return true;
}

bool PerformAdsrWaveFocusable(uint8_t adsr_row)
{
    return (adsr_row % static_cast<uint8_t>(kAdsrRowCount)) == static_cast<uint8_t>(kAdsrRowLoop);
}

static int PerformAdsrFocusIndex(const AppEngineState& engine, uint8_t layer)
{
    if(engine.adsr.perform_adsr_type_focus)
        return 0;

    const uint8_t adsr_row = engine.adsr.perform_adsr_row[layer & 1u];
    if(PerformAdsrWaveFocusable(adsr_row) && engine.adsr.perform_adsr_wave_focus)
        return 1;

    return static_cast<int>(engine.adsr.perform_adsr_stage_focus % static_cast<uint8_t>(kAdsrStageCount))
           + (PerformAdsrWaveFocusable(adsr_row) ? 2 : 1);
}

static void PerformAdsrSetFocusIndex(AppEngineState& engine, uint8_t layer, int idx)
{
    const uint8_t adsr_row = engine.adsr.perform_adsr_row[layer & 1u];
    if(idx <= 0)
    {
        engine.adsr.perform_adsr_type_focus = true;
        engine.adsr.perform_adsr_wave_focus = false;
        return;
    }

    if(PerformAdsrWaveFocusable(adsr_row) && idx == 1)
    {
        engine.adsr.perform_adsr_type_focus = false;
        engine.adsr.perform_adsr_wave_focus = true;
        return;
    }

    engine.adsr.perform_adsr_type_focus = false;
    engine.adsr.perform_adsr_wave_focus = false;
    const int stage_base = PerformAdsrWaveFocusable(adsr_row) ? 2 : 1;
    engine.adsr.perform_adsr_stage_focus
        = static_cast<uint8_t>(ClampInt(idx - stage_base, 0, kAdsrStageCount - 1));
}

void PerformAdsrEnsureValidFocus(AppEngineState& engine, uint8_t layer)
{
    const uint8_t adsr_row = PerformAdsrRow(engine, layer);
    if(engine.adsr.perform_adsr_type_focus)
    {
        engine.adsr.perform_adsr_wave_focus = false;
        return;
    }

    if(!PerformAdsrWaveFocusable(adsr_row))
        engine.adsr.perform_adsr_wave_focus = false;
    if(engine.adsr.perform_adsr_wave_focus)
        return;

    const uint8_t stage = engine.adsr.perform_adsr_stage_focus % static_cast<uint8_t>(kAdsrStageCount);
    if(PerformAdsrStageEnabled(adsr_row, stage))
        return;

    engine.adsr.perform_adsr_stage_focus = (stage <= 1u) ? 0u : 3u;
}

bool PerformAdsr_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.ui)
        return false;
    if(ctx.shift)
        return false;

    AppUiState& ui = *ctx.ui;
    AppEngineState& engine = *ctx.engine;
    AppRecordingState& recording = *ctx.recording;
    AppProjectState& project = *ctx.project;
    AppDiagnosticsState& diag = *ctx.diag;
    AppSharedState& shared = *ctx.shared;
    AppWorkerState& worker = *ctx.worker;

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPod2)
    {
        engine.perform_nav.perform_layer ^= 1u;
        const uint8_t layer = engine.perform_nav.perform_layer & 1u;
        shared.sample.publish.sd_current_slot.store(layer, std::memory_order_release);
        engine.layer.engine_header_invert_until_ms = e.t_ms + 250u;
        PerformAdsrEnsureValidFocus(engine, layer);
        PublishEngineLayerParams(ctx);
        ui.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod && e.value != 0)
    {
        const uint8_t layer = engine.perform_nav.perform_layer & 1u;
        const uint8_t adsr_row = PerformAdsrRow(engine, layer);
        int idx = PerformAdsrFocusIndex(engine, layer);
        const bool wave_focusable = PerformAdsrWaveFocusable(adsr_row);
        const int focus_count = kAdsrStageCount + (wave_focusable ? 2 : 1);
        const int stage_base = wave_focusable ? 2 : 1;
        int steps = (e.value < 0) ? -e.value : e.value;
        const int dir = (e.value < 0) ? -1 : 1;
        while(steps-- > 0)
        {
            do
            {
                idx += dir;
                while(idx < 0)
                    idx += focus_count;
                while(idx >= focus_count)
                    idx -= focus_count;
            } while(idx >= stage_base
                    && !PerformAdsrStageEnabled(adsr_row,
                                                static_cast<uint8_t>(idx - stage_base)));
        }

        PerformAdsrSetFocusIndex(engine, layer, idx);
        ui.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt && e.value != 0)
        return PerformAdsr_OnEventExtEncoder(ctx, e);

    return false;
}

void PerformAdsr_OnScreenEnter(UiScreenCtx& ctx)
{
    if(!ctx.ui)
        return;
    AppUiState& ui = *ctx.ui;
    AppEngineState& engine = *ctx.engine;
    AppRecordingState& recording = *ctx.recording;
    AppProjectState& project = *ctx.project;
    AppDiagnosticsState& diag = *ctx.diag;
    AppSharedState& shared = *ctx.shared;
    AppWorkerState& worker = *ctx.worker;
    engine.adsr.perform_adsr_stage_focus = 0;
    engine.adsr.perform_adsr_type_focus = false;
    engine.adsr.perform_adsr_wave_focus = false;
    const uint8_t layer = engine.perform_nav.perform_layer & 1u;
    uint8_t& adsr_row = PerformAdsrRow(engine, layer);
    if(adsr_row >= static_cast<uint8_t>(kAdsrRowCount))
        adsr_row = (engine.layer.engine_play_mode[layer] & 1u) ? kAdsrRowLoop : kAdsrRowOneShot;
    PerformAdsrEnsureValidFocus(engine, layer);
    ui.ui_dirty = true;
}

void PerformAdsr_Render(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.display)
        return;

    AppUiState& ui = *ctx.ui;
    AppEngineState& engine = *ctx.engine;
    AppRecordingState& recording = *ctx.recording;
    AppProjectState& project = *ctx.project;
    AppDiagnosticsState& diag = *ctx.diag;
    AppSharedState& shared = *ctx.shared;
    AppWorkerState& worker = *ctx.worker;
    EngineRefreshLoadedMetadata(ui, engine, shared);

    OledPager& d = *ctx.display;
    d.Fill(false);

    PerformAdsr_DrawMainContent(d, ctx);
}
