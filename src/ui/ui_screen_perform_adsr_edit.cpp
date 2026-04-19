#include "ui_screen_perform_adsr_internal.h"

#include "ui_screens_internal.h"

#include "app_state_ui.h"
#include "app_state_engine.h"
#include "app_state_recording.h"
#include "app_state_project.h"
#include "app_state_diagnostics.h"
#include "app_state_shared.h"
#include "app_state_worker.h"
#include "ui_input.h"

static constexpr int32_t kAdsrRowCount = 3;
static constexpr int32_t kAdsrRowOneShot = 0;
static constexpr int32_t kAdsrRowLoop = 1;
static constexpr int32_t kAdsrRowAdsr = 2;
static constexpr int32_t kAdsrStageCount = 4;
static constexpr float kPerformLoopCrossfadeMin = 0.0f;
static constexpr float kPerformLoopCrossfadeMax = 0.5f;
static constexpr float kPerformLoopCrossfadeStep = 1.0f / 128.0f;
static constexpr float kPerformLoopCrossfadeShapeMin = 0.0f;
static constexpr float kPerformLoopCrossfadeShapeMax = 1.0f;
static constexpr float kPerformLoopCrossfadeShapeStep = 1.0f / 64.0f;
static constexpr uint16_t kPerformAdsrAttackMinMs  = 2u;
static constexpr uint16_t kPerformAdsrReleaseMinMs = 1u;
static constexpr uint16_t kPerformAdsrAttackReleaseMaxMs = 1000u;
static constexpr uint16_t kPerformAdsrDecayMaxMs = 100u;
static constexpr uint16_t kPerformAdsrSustainMax = 100u;

static int ClampInt(int v, int lo, int hi)
{
    if(v < lo)
        return lo;
    if(v > hi)
        return hi;
    return v;
}

static void SetPerformAdsrStageValue(AppEngineState& engine, uint8_t layer, uint8_t stage, uint16_t value)
{
    const uint8_t safe_layer = layer & 1u;
    switch(stage % static_cast<uint8_t>(kAdsrStageCount))
    {
        case 0:
            engine.adsr.perform_adsr_loop_attack[safe_layer] = value;
            return;
        case 1:
            engine.adsr.perform_adsr_loop_decay[safe_layer] = static_cast<uint8_t>(value);
            return;
        case 2:
            engine.adsr.perform_adsr_loop_sustain[safe_layer] = static_cast<uint8_t>(value);
            return;
        default:
            engine.adsr.perform_adsr_loop_release[safe_layer] = value;
            return;
    }
}

static int PerformAdsrStageMin(uint8_t stage)
{
    const uint8_t s = stage % static_cast<uint8_t>(kAdsrStageCount);
    if(s == 2u)
        return 0;
    if(s == 0u)
        return static_cast<int>(kPerformAdsrAttackMinMs);
    if(s == 3u)
        return static_cast<int>(kPerformAdsrReleaseMinMs);
    // decay (stage 1): same 1 ms floor as release
    return static_cast<int>(kPerformAdsrReleaseMinMs);
}

static int PerformAdsrStageMax(uint8_t stage)
{
    switch(stage % static_cast<uint8_t>(kAdsrStageCount))
    {
        case 0:
        case 3: return static_cast<int>(kPerformAdsrAttackReleaseMaxMs);
        case 1: return static_cast<int>(kPerformAdsrDecayMaxMs);
        case 2:
        default: return static_cast<int>(kPerformAdsrSustainMax);
    }
}

static uint8_t& PerformAdsrEnvX(AppEngineState& engine, uint8_t layer, uint8_t stage)
{
    const uint8_t safe_layer = layer & 1u;
    switch(stage % static_cast<uint8_t>(kAdsrStageCount))
    {
        case 0: return engine.adsr.perform_adsr_env_a_x[safe_layer];
        case 1: return engine.adsr.perform_adsr_env_d_x[safe_layer];
        default: return engine.adsr.perform_adsr_env_r_x[safe_layer];
    }
}

static uint8_t& PerformAdsrEnvSLevel(AppEngineState& engine, uint8_t layer)
{
    return engine.adsr.perform_adsr_env_s_level[layer & 1u];
}

bool PerformAdsr_OnEventExtEncoder(UiScreenCtx& ctx, const UiInputEvent& e)
{
    AppUiState& ui = *ctx.ui;
    AppEngineState& engine = *ctx.engine;
    AppRecordingState& recording = *ctx.recording;
    AppProjectState& project = *ctx.project;
    AppDiagnosticsState& diag = *ctx.diag;
    AppSharedState& shared = *ctx.shared;
    AppWorkerState& worker = *ctx.worker;

    const uint8_t layer = engine.perform_nav.perform_layer & 1u;
    uint8_t& adsr_row = PerformAdsrRow(engine, layer);
    if(engine.adsr.perform_adsr_type_focus)
    {
        int row = static_cast<int>(adsr_row % static_cast<uint8_t>(kAdsrRowCount));
        row += e.value;
        while(row < 0)
            row += kAdsrRowCount;
        while(row >= kAdsrRowCount)
            row -= kAdsrRowCount;
        adsr_row = static_cast<uint8_t>(row);

        uint8_t next_mode = engine.layer.engine_play_mode[layer] & 1u;
        bool changed = false;
        if(row == static_cast<int>(kAdsrRowOneShot))
        {
            if(next_mode != 0u)
            {
                next_mode = 0u;
                changed = true;
            }
        }
        else if(row == static_cast<int>(kAdsrRowLoop))
        {
            if(next_mode != 1u)
            {
                next_mode = 1u;
                changed = true;
            }
        }

        if(changed)
        {
            engine.layer.engine_play_mode[layer] = next_mode;
            PublishEngineLayerParams(ctx);
        }
        PerformAdsrEnsureValidFocus(engine, layer);
        ui.ui_dirty = true;
        return true;
    }

    if(engine.adsr.perform_adsr_wave_focus
       && (adsr_row % static_cast<uint8_t>(kAdsrRowCount)) == static_cast<uint8_t>(kAdsrRowLoop))
    {
        float& target = ctx.rshift ? engine.adsr.perform_adsr_loop_crossfade_shape[layer]
                                   : engine.adsr.perform_adsr_loop_crossfade[layer];
        const float step = ctx.rshift ? kPerformLoopCrossfadeShapeStep
                                      : kPerformLoopCrossfadeStep;
        const float min_value = ctx.rshift ? kPerformLoopCrossfadeShapeMin
                                           : kPerformLoopCrossfadeMin;
        const float max_value = ctx.rshift ? kPerformLoopCrossfadeShapeMax
                                           : kPerformLoopCrossfadeMax;
        float next = target + (static_cast<float>(e.value) * step);
        if(next < min_value)
            next = min_value;
        if(next > max_value)
            next = max_value;
        if(next == target)
            return false;
        target = next;
        PublishEngineLayerParams(ctx);
        ui.ui_dirty = true;
        return true;
    }

    if((adsr_row % static_cast<uint8_t>(kAdsrRowCount)) != static_cast<uint8_t>(kAdsrRowLoop))
    {
        if((adsr_row % static_cast<uint8_t>(kAdsrRowCount)) != static_cast<uint8_t>(kAdsrRowAdsr))
            return false;

        const uint8_t stage = engine.adsr.perform_adsr_stage_focus % static_cast<uint8_t>(kAdsrStageCount);
        if(stage == 2u)
        {
            uint8_t& level = PerformAdsrEnvSLevel(engine, layer);
            const int next_level = ClampInt(static_cast<int>(level) + e.value, 0, 100);
            if(next_level == static_cast<int>(level))
                return false;
            level = static_cast<uint8_t>(next_level);
            ui.ui_dirty = true;
            return true;
        }

        static constexpr int kAdsrEnvMinGap = 6;
        uint8_t& value = PerformAdsrEnvX(engine, layer, stage);
        const int a_x = static_cast<int>(engine.adsr.perform_adsr_env_a_x[layer]);
        const int d_x = static_cast<int>(engine.adsr.perform_adsr_env_d_x[layer]);
        const int r_x = static_cast<int>(engine.adsr.perform_adsr_env_r_x[layer]);
        int min_value = 0;
        int max_value = 100;
        if(stage == 0u)
        {
            max_value = d_x - kAdsrEnvMinGap;
        }
        else if(stage == 1u)
        {
            min_value = a_x + kAdsrEnvMinGap;
            max_value = r_x - kAdsrEnvMinGap;
        }
        else
        {
            min_value = d_x + kAdsrEnvMinGap;
        }

        const int next_value = ClampInt(static_cast<int>(value) + e.value, min_value, max_value);
        if(next_value == static_cast<int>(value))
            return false;
        value = static_cast<uint8_t>(next_value);
        ui.ui_dirty = true;
        return true;
    }

    const uint8_t stage = engine.adsr.perform_adsr_stage_focus % static_cast<uint8_t>(kAdsrStageCount);
    const uint16_t value = PerformAdsrStageValue(engine, layer, stage);
    const int min_value = PerformAdsrStageMin(stage);
    const int max_value = PerformAdsrStageMax(stage);
    const int next_value = ClampInt(static_cast<int>(value) + e.value, min_value, max_value);
    if(next_value == static_cast<int>(value))
        return false;

    SetPerformAdsrStageValue(engine, layer, stage, static_cast<uint16_t>(next_value));
    PublishEngineLayerParams(ctx);
    ui.ui_dirty = true;
    return true;
}
