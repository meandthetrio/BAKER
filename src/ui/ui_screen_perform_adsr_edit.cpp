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
static constexpr uint16_t kPerformAdsrAttackReleaseMaxMs = 4000u;
static constexpr uint16_t kPerformAdsrDecayMaxMs = 1000u;
static constexpr uint16_t kPerformAdsrSustainMax = 100u;

static int ClampInt(int v, int lo, int hi)
{
    if(v < lo)
        return lo;
    if(v > hi)
        return hi;
    return v;
}

// REnc value-edit acceleration. A fast spin should cover the full (now 4000 ms)
// attack/release range without endless clicking, while slow deliberate clicks
// still nudge by 1. The elapsed time between successive value-edit detents picks
// a tier (0 = fine, 1 = moderate, 2 = fast); a per-range step table turns the
// tier into a delta. Called exactly once per numeric edit event so the static
// timestamp tracks real inter-detent timing (event t_ms is the capture time, so
// queue batching does not distort it).
static int PerformAdsrAccelTier(uint32_t t_ms)
{
    static uint32_t s_last_ms = 0;
    const uint32_t dt = t_ms - s_last_ms; // unsigned; first call -> huge -> fine
    s_last_ms = t_ms;
    if(dt <= 22u)
        return 2; // rapid spin
    if(dt <= 55u)
        return 1; // moderate
    return 0;     // fine / single clicks
}

// Per-detent step for a wide ms range (attack/release, 0..4000): fast spin jumps
// by 100 ms so the full range is a turn or two away.
static int PerformAdsrMsStepForTier(int tier)
{
    static const int kStep[3] = {1, 10, 100};
    return kStep[(tier < 0) ? 0 : (tier > 2 ? 2 : tier)];
}

// Per-detent step for a narrow 0..100 range (sustain, decay, env handles): fast
// spin jumps by 10.
static int PerformAdsrUnitStepForTier(int tier)
{
    static const int kStep[3] = {1, 5, 10};
    return kStep[(tier < 0) ? 0 : (tier > 2 ? 2 : tier)];
}

// Net signed delta for an encoder event after applying the tier step. |e.value|
// > 1 means multiple detents landed in one poll (already a fast gesture), so the
// per-detent step is multiplied by the magnitude.
static int PerformAdsrAccelDelta(int enc_value, int step)
{
    const int magnitude = (enc_value < 0) ? -enc_value : enc_value;
    const int dir = (enc_value < 0) ? -1 : 1;
    return dir * step * magnitude;
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
            engine.adsr.perform_adsr_loop_decay[safe_layer] = value;
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
    AppSharedState& shared = *ctx.shared;

    const uint8_t layer = engine.perform_nav.perform_layer & 1u;
    uint8_t& adsr_row = PerformAdsrRow(engine, layer);
    if(engine.adsr.perform_adsr_type_focus)
    {
        // Only two playback types remain: loop and adsr (one-shot was removed —
        // adsr with sustain-loop off reproduces it). Any encoder turn toggles
        // between the two; a stale value (e.g. a migrated one-shot) snaps to adsr.
        uint8_t cur = (adsr_row == static_cast<uint8_t>(kAdsrRowLoop))
                          ? static_cast<uint8_t>(kAdsrRowLoop)
                          : static_cast<uint8_t>(kAdsrRowAdsr);
        if(e.value != 0)
            cur = (cur == static_cast<uint8_t>(kAdsrRowLoop))
                      ? static_cast<uint8_t>(kAdsrRowAdsr)
                      : static_cast<uint8_t>(kAdsrRowLoop);
        adsr_row = cur;
        // loop -> SDRAM loop on; adsr -> loop off (positional envelope owns it).
        engine.layer.engine_play_mode[layer]
            = (cur == static_cast<uint8_t>(kAdsrRowLoop)) ? 1u : 0u;
        PublishEngineLayerParams(ctx);
        PerformAdsrEnsureValidFocus(engine, shared, layer);
        ui.ui_dirty = true;
        return true;
    }

    if(engine.adsr.perform_adsr_wave_focus
       && (adsr_row % static_cast<uint8_t>(kAdsrRowCount)) == static_cast<uint8_t>(kAdsrRowLoop))
    {
        // Seam length/curve editing has moved to the dedicated seam-edit screen
        // (REnc click on the focused wav preview opens it). REnc rotate here is a
        // no-op; the ADSR preview shows the saved seam length read-only.
        return false;
    }

    if((adsr_row % static_cast<uint8_t>(kAdsrRowCount)) != static_cast<uint8_t>(kAdsrRowLoop))
    {
        if((adsr_row % static_cast<uint8_t>(kAdsrRowCount)) != static_cast<uint8_t>(kAdsrRowAdsr))
            return false;

        const uint8_t stage = engine.adsr.perform_adsr_stage_focus % static_cast<uint8_t>(kAdsrStageCount);
        if(stage == 2u)
        {
            uint8_t& level = PerformAdsrEnvSLevel(engine, layer);
            const int level_step = PerformAdsrUnitStepForTier(PerformAdsrAccelTier(e.t_ms));
            const int next_level = ClampInt(
                static_cast<int>(level) + PerformAdsrAccelDelta(e.value, level_step), 0, 100);
            if(next_level == static_cast<int>(level))
                return false;
            level = static_cast<uint8_t>(next_level);
            // Push to the audio engine so the sustain level (and the rest of the
            // positional ADSR-mode envelope) tracks the handle in real time.
            PublishEngineLayerParams(ctx);
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

        const int env_step = PerformAdsrUnitStepForTier(PerformAdsrAccelTier(e.t_ms));
        const int next_value = ClampInt(
            static_cast<int>(value) + PerformAdsrAccelDelta(e.value, env_step), min_value, max_value);
        if(next_value == static_cast<int>(value))
            return false;
        value = static_cast<uint8_t>(next_value);
        // Push handle moves (A/D/R positions) to the audio engine so the
        // positional ADSR-mode envelope updates in real time.
        PublishEngineLayerParams(ctx);
        ui.ui_dirty = true;
        return true;
    }

    const uint8_t stage = engine.adsr.perform_adsr_stage_focus % static_cast<uint8_t>(kAdsrStageCount);
    if(!PerformAdsrStageFocusable(shared, engine, layer, adsr_row, stage))
    {
        ui.ui_dirty = true;
        return true;
    }
    const uint16_t value = PerformAdsrStageValue(engine, layer, stage);
    const int min_value = PerformAdsrStageMin(stage);
    const int max_value = PerformAdsrStageMax(stage);
    const int tier = PerformAdsrAccelTier(e.t_ms);
    // Attack (0)/release (3) span 0..4000 ms and decay (1) spans 0..1000 ms ->
    // all use the coarse 100 ms fast step; sustain (2) is 0..100 -> unit step.
    const bool wide_stage = (stage == 0u || stage == 1u || stage == 3u);
    const int step = wide_stage ? PerformAdsrMsStepForTier(tier)
                                : PerformAdsrUnitStepForTier(tier);
    const int next_value = ClampInt(
        static_cast<int>(value) + PerformAdsrAccelDelta(e.value, step), min_value, max_value);
    if(next_value == static_cast<int>(value))
        return false;

    SetPerformAdsrStageValue(engine, layer, stage, static_cast<uint16_t>(next_value));
    PublishEngineLayerParams(ctx);
    ui.ui_dirty = true;
    return true;
}
