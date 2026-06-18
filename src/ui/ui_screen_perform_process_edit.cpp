#include "ui_screen_perform_process_internal.h"

#include "ui_screens_internal.h"

#include "app_state_ui.h"
#include "app_state_engine.h"
#include "app_state_recording.h"
#include "app_state_project.h"
#include "app_state_diagnostics.h"
#include "app_state_shared.h"
#include "app_state_worker.h"
#include "params.h"
#include "tilt_eq.h"
#include "ui_input.h"

#include <atomic>

static float UiAccelFromDtMs(uint32_t dt_ms)
{
    if(dt_ms <= 20u) return 8.0f;
    if(dt_ms <= 40u) return 5.0f;
    if(dt_ms <= 70u) return 3.0f;
    if(dt_ms <= 120u) return 2.0f;
    return 1.0f;
}

float UiDeltaNormAccelerated(int enc_delta, uint32_t t_ms, uint32_t& last_t_ms, float base_step)
{
    const uint32_t dt_ms = (last_t_ms == 0u) ? 999u : (t_ms - last_t_ms);
    last_t_ms = t_ms;
    return static_cast<float>(enc_delta) * base_step * UiAccelFromDtMs(dt_ms);
}

uint8_t ProcessDetailParamCount(uint8_t fx_id, uint8_t sat_mode)
{
    switch(fx_id)
    {
        // SAT is mode-dependent: TAPE = SAT BUMP TONE BIAS + mode toggle (5);
        // BIT = RESO SMPL + mode toggle (3).
        case 0: return (sat_mode == 0) ? 5 : 3;
        case 1: return 1; // EQ: graph only (no classic detail)
        case 2: return 4; // DELAY: LTM RTM FBK + mode slot
        case 3: return 5; // REVERB: Pre Dmp Dcy Mod + mode slot
        default: return 3;
    }
}

void ProcessHandleLayerToggle(UiScreenCtx& ctx)
{
    AppUiState& ui = *ctx.ui;
    AppEngineState& engine = *ctx.engine;
    AppSharedState& shared = *ctx.shared;
    engine.perform_nav.perform_layer = 0u; // single layer: A/B toggle disabled
    const uint8_t layer = engine.perform_nav.perform_layer & 1u;
    shared.sample.publish.sd_current_slot.store(layer, std::memory_order_release);
    PublishEngineLayerParams(ctx);
    ui.ui_dirty = true;
}

void ProcessHandleLayerMuteToggle(UiScreenCtx& ctx, uint8_t layer)
{
    AppUiState& ui = *ctx.ui;
    AppEngineState& engine = *ctx.engine;
    PerformParamsTargets& t = ctx.params->EditTargets();
    if(ctx.rshift)
    {
        // RSHIFT + click => snap to UNITY.
        t.engine_layer_master_level[layer] = 1.0f;
        engine.process.perform_process_vol_unmuted_level[layer] = 1.0f;
        engine.process.perform_process_vol_muted[layer] = false;
        engine.process.perform_process_vol_pct[layer] = 100u;
    }
    else
    {
        // Click => mute toggle for selected voice.
        if(!engine.process.perform_process_vol_muted[layer])
        {
            float saved = t.engine_layer_master_level[layer];
            if(saved < 0.001f)
                saved = 1.0f;
            engine.process.perform_process_vol_unmuted_level[layer] = saved;
            engine.process.perform_process_vol_muted[layer] = true;
            t.engine_layer_master_level[layer] = 0.0f;
            engine.process.perform_process_vol_pct[layer] = 0u;
        }
        else
        {
            float restore = engine.process.perform_process_vol_unmuted_level[layer];
            if(restore < 0.0f)
                restore = 0.0f;
            if(restore > kProcessLayerLevelUiMax)
                restore = kProcessLayerLevelUiMax;
            engine.process.perform_process_vol_muted[layer] = false;
            t.engine_layer_master_level[layer] = restore;
            engine.process.perform_process_vol_pct[layer] = static_cast<uint16_t>(restore * 100.0f + 0.5f);
        }
    }
    ctx.params->PublishTargets();
    ui.ui_dirty = true;
}

void ProcessHandleLayerVolumeEdit(UiScreenCtx& ctx, const UiInputEvent& e, uint8_t layer)
{
    AppUiState& ui = *ctx.ui;
    AppEngineState& engine = *ctx.engine;
    if(engine.process.perform_process_vol_muted[layer])
    {
        // Do not unmute on encoder turn; mute state toggles only on R-click.
        ui.ui_dirty = true;
        return;
    }

    PerformParamsTargets& t = ctx.params->EditTargets();

    // Match SETTINGS volume acceleration + range exactly.
    static uint32_t s_last_t_ms = 0u;
    const uint32_t now_ms = e.t_ms;
    const uint32_t dt_ms = (s_last_t_ms == 0u) ? 999u : (now_ms - s_last_t_ms);
    s_last_t_ms = now_ms;

    float accel = 1.0f;
    if(dt_ms <= 25u)
        accel = 10.0f;
    else if(dt_ms <= 50u)
        accel = 6.0f;
    else if(dt_ms <= 90u)
        accel = 3.0f;
    else if(dt_ms <= 140u)
        accel = 2.0f;

    const float base_step = 0.01f;
    float next = t.engine_layer_master_level[layer] + static_cast<float>(e.value) * base_step * accel;
    if(next < 0.0f)
        next = 0.0f;
    if(next > kProcessLayerLevelUiMax)
        next = kProcessLayerLevelUiMax;
    t.engine_layer_master_level[layer] = next;
    engine.process.perform_process_vol_unmuted_level[layer] = next;
    engine.process.perform_process_vol_pct[layer] = static_cast<uint16_t>(next * 100.0f + 0.5f);
    ctx.params->PublishTargets();
    ui.ui_dirty = true;
}

void ProcessEditEqGraph(UiScreenCtx& ctx, float delta)
{
    PerformParamsTargets& t = ctx.params->EditTargets();
    if(ctx.rshift)
    {
        // RShift + Ext: Q for both bells (0.5 .. 1.7)
        t.eq_q = ClampEqQ(t.eq_q + delta * (kTiltEqQMax - kTiltEqQMin) * 1.25f);
    }
    else
    {
        t.eq_tilt_db = ClampEqTiltDb(t.eq_tilt_db + delta * 18.0f);
    }
}

static bool ProcessEditSatDetail(PerformParamsTargets& t,
                                 const UiInputEvent& e,
                                 uint8_t pidx,
                                 float delta)
{
    const bool    tape      = (t.sat_mode == 0);
    const uint8_t mode_pidx = tape ? 4u : 2u; // mode toggle is the last param slot

    if(pidx == mode_pidx)
    {
        const int dir   = (e.value > 0) ? 1 : -1;
        int       steps = (e.value > 0) ? e.value : -e.value;
        while(steps-- > 0)
            t.sat_mode = (dir > 0) ? ((t.sat_mode + 1u) & 1u) : ((t.sat_mode == 0) ? 1u : 0u);
        return true;
    }

    if(tape)
    {
        // TAPE: SAT(drive) BUMP TONE BIAS. BIAS is bipolar (-1..1); the fader
        // shows it as 0..1, so traverse 2x per detent to span the full range.
        if(pidx == 0)
            t.sat_drive = Clamp01(t.sat_drive + delta);
        else if(pidx == 1)
            t.sat_bump = Clamp01(t.sat_bump + delta);
        else if(pidx == 2)
            t.sat_tone = Clamp01(t.sat_tone + delta);
        else if(pidx == 3)
        {
            float b = t.sat_bias + delta * 2.0f;
            if(b < -1.0f) b = -1.0f;
            if(b > 1.0f) b = 1.0f;
            t.sat_bias = b;
        }
        return true;
    }

    // BIT mode: RESO (discrete 3-state) + SMPL.
    if(pidx == 0)
    {
        const int dir = (e.value > 0) ? 1 : -1;
        int idx = 0;
        if(t.sat_bit_reso >= 0.75f)
            idx = 2;
        else if(t.sat_bit_reso >= 0.25f)
            idx = 1;
        idx += dir;
        if(idx < 0) idx = 0;
        if(idx > 2) idx = 2;
        if(idx == 0) t.sat_bit_reso = 0.0f;      // CRUSH
        if(idx == 1) t.sat_bit_reso = 0.5f;      // STATIC
        if(idx == 2) t.sat_bit_reso = 1.0f;      // HISS
        return true;
    }
    if(pidx == 1)
    {
        t.sat_bit_smpl = Clamp01(t.sat_bit_smpl + delta);
        return true;
    }

    return false;
}

static bool ProcessEditDelayDetail(UiScreenCtx& ctx,
                                   PerformParamsTargets& t,
                                   const UiInputEvent& e,
                                   uint8_t pidx,
                                   float delta)
{
    if(pidx == 0)
    {
        if(ctx.rshift)
        {
            t.delay_time_l = Clamp01(t.delay_time_l + delta);
            t.delay_time_r = Clamp01(t.delay_time_r + delta);
        }
        else
        {
            t.delay_time_l = Clamp01(t.delay_time_l + delta);
        }
        return true;
    }

    if(pidx == 1)
    {
        if(ctx.rshift)
        {
            t.delay_time_l = Clamp01(t.delay_time_l + delta);
            t.delay_time_r = Clamp01(t.delay_time_r + delta);
        }
        else
        {
            t.delay_time_r = Clamp01(t.delay_time_r + delta);
        }
        return true;
    }

    if(pidx == 2)
    {
        t.delay_feedback = Clamp01(t.delay_feedback + delta);
        return true;
    }

    if(pidx == 3)
    {
        const int dir = (e.value > 0) ? 1 : -1;
        int mode = static_cast<int>(t.delay_fader_mode & 0x01u);
        int steps = (e.value > 0) ? e.value : -e.value;
        while(steps-- > 0)
            mode = (dir > 0) ? ((mode + 1) & 0x01) : ((mode == 0) ? 1 : 0);
        t.delay_fader_mode = static_cast<uint8_t>(mode);
        return true;
    }

    return false;
}

static bool ProcessEditReverbDetail(PerformParamsTargets& t,
                                    const UiInputEvent& e,
                                    uint8_t pidx,
                                    float delta)
{
    if(pidx == 0)
    {
        t.reverb_pre = Clamp01(t.reverb_pre + delta);
        return true;
    }

    if(pidx == 1)
    {
        t.reverb_damp = Clamp01(t.reverb_damp + delta);
        return true;
    }

    if(pidx == 2)
    {
        t.reverb_decay = Clamp01(t.reverb_decay + delta);
        return true;
    }

    if(pidx == 3)
    {
        t.reverb_mod = Clamp01(t.reverb_mod + delta);
        return true;
    }

    if(pidx == 4)
    {
        const int dir = (e.value > 0) ? 1 : -1;
        int mode = static_cast<int>(t.reverb_fader_mode & 0x01u);
        int steps = (e.value > 0) ? e.value : -e.value;
        while(steps-- > 0)
            mode = (dir > 0) ? ((mode + 1) & 0x01) : ((mode == 0) ? 1 : 0);
        t.reverb_fader_mode = static_cast<uint8_t>(mode);
        return true;
    }

    return false;
}

bool ProcessEditFxDetail(UiScreenCtx& ctx,
                         const UiInputEvent& e,
                         uint8_t fx_id,
                         uint8_t pidx,
                         float delta)
{
    PerformParamsTargets& t = ctx.params->EditTargets();
    switch(fx_id)
    {
        case 0: return ProcessEditSatDetail(t, e, pidx, delta);
        case 2: return ProcessEditDelayDetail(ctx, t, e, pidx, delta);
        case 3:
        default: return ProcessEditReverbDetail(t, e, pidx, delta);
    }
}
