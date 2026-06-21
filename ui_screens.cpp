#include "ui_screens.h"
#include "ui_screens_internal.h"
#include "app_state.h"
#include "express_state.h"
#include "params.h"
#include "oled_pager.h"
#include "sample_edit.h"
#include "sample_style.h"

#include <cstdio>

static constexpr uint8_t kPerformLayerCount = 2;
static constexpr uint16_t kPerformAdsrAttackMinMs   = 2u;
static constexpr uint16_t kPerformAdsrReleaseMinMs  = 1u;
static constexpr uint16_t kPerformAdsrAttackReleaseMaxMs = 4000u;
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

void ExtractBaseName(const char* path, char* out, size_t out_n)
{
    if(!out || out_n == 0)
        return;
    out[0] = '\0';
    if(!path || path[0] == '\0')
        return;
    BuildSampleDisplayName(path, out, out_n);
}

static uint8_t ClampDriveMode(int value)
{
    if(value <= 0)
        return 0u;
    return 1u;
}

void PublishEngineLayerParams(UiScreenCtx& ctx)
{
    if(!ctx.engine || !ctx.params)
        return;

    AppEngineState& engine = *ctx.engine;
    const uint8_t layer = engine.perform_nav.perform_layer & 1u;
    PerformParamsTargets& t = ctx.params->EditTargets();
    t.engine_tune_semitones[layer] = static_cast<float>(engine.layer.engine_tune_semitones[layer])
                                     + (static_cast<float>(engine.layer.engine_tune_cents[layer]) * 0.01f);
    t.engine_gain_db[layer] = static_cast<float>(engine.layer.engine_gain_db[layer]);
    t.engine_loop_mode[layer] = (engine.layer.engine_play_mode[layer] != 0);
    for(uint8_t i = 0; i < kPerformLayerCount; ++i)
    {
        const uint16_t clamped_attack = static_cast<uint16_t>(
            ClampInt(static_cast<int>(engine.adsr.perform_adsr_loop_attack[i]),
                     static_cast<int>(kPerformAdsrAttackMinMs),
                     static_cast<int>(kPerformAdsrAttackReleaseMaxMs)));
        const uint16_t clamped_release = static_cast<uint16_t>(
            ClampInt(static_cast<int>(engine.adsr.perform_adsr_loop_release[i]),
                     static_cast<int>(kPerformAdsrReleaseMinMs),
                     static_cast<int>(kPerformAdsrAttackReleaseMaxMs)));
        const uint8_t clamped_decay = static_cast<uint8_t>(
            ClampInt(static_cast<int>(engine.adsr.perform_adsr_loop_decay[i]), 1, static_cast<int>(kPerformAdsrDecayMaxMs)));
        const uint8_t clamped_sustain = static_cast<uint8_t>(
            ClampInt(static_cast<int>(engine.adsr.perform_adsr_loop_sustain[i]), 0, static_cast<int>(kPerformAdsrSustainMax)));
        engine.adsr.perform_adsr_loop_attack[i] = clamped_attack;
        engine.adsr.perform_adsr_loop_decay[i] = clamped_decay;
        engine.adsr.perform_adsr_loop_sustain[i] = clamped_sustain;
        engine.adsr.perform_adsr_loop_release[i] = clamped_release;
        t.engine_drive_mode[i] = ClampDriveMode(static_cast<int>(engine.layer.engine_drive_mode[i]));
        t.engine_filter_mode[i] = static_cast<uint8_t>(
            ClampInt(static_cast<int>(engine.layer.engine_filter_mode[i]), 0, 2));
        t.perform_keyzone_lo_note[i] = engine.keyzone.perform_keyzone_lo_note[i];
        t.perform_keyzone_hi_note[i] = engine.keyzone.perform_keyzone_hi_note[i];
        t.engine_loop_attack_ms[i] = static_cast<float>(clamped_attack);
        t.engine_loop_decay_ms[i] = static_cast<float>(clamped_decay);
        t.engine_loop_sustain_level[i] = static_cast<float>(clamped_sustain) * 0.01f;
        t.engine_loop_release_ms[i] = static_cast<float>(clamped_release);
        t.engine_loop_attack_curve[i] = engine.adsr.perform_adsr_attack_curve[i] & 1u;
        t.engine_loop_release_curve[i] = engine.adsr.perform_adsr_release_curve[i] & 1u;
        t.engine_loop_crossfade_amount[i] = engine.adsr.perform_adsr_loop_crossfade[i];
        t.engine_loop_crossfade_shape[i] = engine.adsr.perform_adsr_loop_crossfade_shape[i];
        for(uint8_t row = 0; row < kExpressRowCount; ++row)
        {
            t.express_target[i][row] = engine.express.target[i][row];
            t.express_min_value[i][row] = engine.express.min_value[i][row];
            t.express_max_value[i][row] = engine.express.max_value[i][row];
            ExpressClampRow(t.express_target[i][row],
                            t.express_min_value[i][row],
                            t.express_max_value[i][row]);
        }
        t.express_poly_porto_voice_limit[i] = engine.express.poly_porto_voice_limit[i];
        t.express_poly_porto_slide_ms[i] = engine.express.poly_porto_slide_ms[i];
        t.express_poly_porto_source_range_semitones[i]
            = engine.express.poly_porto_source_range_semitones[i];
        t.express_poly_porto_source_mode[i] = engine.express.poly_porto_source_mode[i];
        t.express_poly_porto_release_ms[i] = engine.express.poly_porto_release_ms[i];
        ExpressClampPolyPortoConfig(t.express_poly_porto_voice_limit[i],
                                    t.express_poly_porto_slide_ms[i],
                                    t.express_poly_porto_source_range_semitones[i],
                                    t.express_poly_porto_source_mode[i],
                                    t.express_poly_porto_release_ms[i]);
    }
    // Velocity-mod lanes are global (not per-layer); copy both lanes straight
    // through. The UI already clamps amount to each target's range, so no
    // re-clamp needed here.
    for(uint8_t lane = 0; lane < PerformParamsTargets::kVelModLaneCount; ++lane)
    {
        t.velmod_target[lane]    = engine.velmod.target_idx[lane];
        t.velmod_amount[lane]    = engine.velmod.amount[lane];
        t.velmod_threshold[lane] = engine.velmod.threshold[lane];
        t.velmod_shape[lane]     = engine.velmod.shape[lane];
        t.velmod_source[lane]    = engine.velmod.source[lane];
    }
    t.perform_keyzone_is_split = engine.keyzone.perform_keyzone_is_split;
    t.perform_keytrack_tilt = engine.keyzone.perform_keytrack_tilt;
    t.perform_keytrack_amount_db = engine.keyzone.perform_keytrack_amount_db;
    t.perform_keytrack_mid_note = engine.keyzone.perform_keytrack_mid_note;
    ctx.params->PublishTargets();
}

bool ExpressUiEnabled(const AppSharedState& shared)
{
    return shared.performance.express.enabled.load(std::memory_order_acquire) != 0u;
}

bool ExpressUiFlashLocked(uint32_t now_ms)
{
    return (now_ms % 1000u) < 160u;
}

bool ExpressUiTargetLocked(const AppSharedState& shared,
                           const AppEngineState& engine,
                           uint8_t layer,
                           uint8_t target)
{
    if(!ExpressUiEnabled(shared))
        return false;
    if(ExpressTargetIsGlobal(target))
        return ExpressAnyLayerOwnsTarget(engine.express.target, target);
    return ExpressLayerOwnsTarget(engine.express.target, layer, target);
}

void EngineRefreshLoadedMetadata(AppUiState& ui, AppEngineState& engine, AppSharedState& shared)
{
    const uint32_t applied_gen = shared.sample.publish.sd_applied_gen.load(std::memory_order_relaxed);
    if(applied_gen == engine.layer.engine_seen_applied_gen)
        return;

    engine.layer.engine_seen_applied_gen = applied_gen;
    const uint8_t slot = shared.sample.publish.sd_current_slot.load(std::memory_order_relaxed) & 1u;
    const Sample& s = shared.sample.publish.sd_slots[slot];
    if(s.pcm != nullptr && s.length > 0)
    {
        std::snprintf(engine.layer.engine_sample_path[slot],
                      sizeof(engine.layer.engine_sample_path[slot]),
                      "%s",
                      ui.sd.last_loaded_path);
        ExtractBaseName(ui.sd.last_loaded_path,
                        engine.layer.engine_sample_name[slot],
                        sizeof(engine.layer.engine_sample_name[slot]));
    }
    engine.layer.engine_load_target_layer = 0xFFu;
    engine.layer.engine_load_from_perform = false;
    ui.ui_dirty = true;
}

int WaveformColumns(const int16_t* pcm,
                    uint32_t start,
                    uint32_t end,
                    int cols,
                    int16_t* col_min,
                    int16_t* col_max)
{
    // Fills col_min/col_max with the per-column min/max over [start,end) and
    // returns the window peak magnitude (1..32768) for display auto-fit. One
    // pass over the window — callers reuse the column buffers to draw.
    if(cols < 1 || pcm == nullptr || end <= start)
        return 1;
    const uint32_t frames = end - start;
    int peak = 1;
    for(int px = 0; px < cols; ++px)
    {
        uint32_t seg0
            = start + static_cast<uint32_t>((static_cast<uint64_t>(frames) * static_cast<uint32_t>(px)) / static_cast<uint32_t>(cols));
        uint32_t seg1
            = start + static_cast<uint32_t>((static_cast<uint64_t>(frames) * static_cast<uint32_t>(px + 1)) / static_cast<uint32_t>(cols));
        if(seg1 <= seg0)
            seg1 = seg0 + 1;
        if(seg1 > end)
            seg1 = end;
        int16_t mn = 32767;
        int16_t mx = -32768;
        for(uint32_t i = seg0; i < seg1; ++i)
        {
            const int16_t v = pcm[i];
            if(v < mn)
                mn = v;
            if(v > mx)
                mx = v;
        }
        col_min[px] = mn;
        col_max[px] = mx;
        const int a = (mx < 0) ? -mx : mx;
        const int b = (mn < 0) ? -mn : mn;
        if(a > peak)
            peak = a;
        if(b > peak)
            peak = b;
    }
    return peak;
}

void DrawWaveformPreview(OledPager& d,
                         const Sample& sample,
                         const SampleEdit* edit,
                         int x,
                         int y,
                         int w,
                         int h,
                         bool on,
                         bool outline_only,
                         bool dotted_border,
                         bool draw_border)
{
    if(w < 3 || h < 3)
        return;

    const int x0 = x;
    const int y0 = y;
    const int x1 = x + w - 1;
    const int y1 = y + h - 1;
    if(draw_border)
    {
        if(dotted_border)
            DrawDottedRect(d, x0, y0, x1, y1, on);
        else
            d.DrawRect(x0, y0, x1, y1, on, false);
    }

    if(sample.pcm == nullptr || sample.length == 0)
        return;

    uint32_t start = 0;
    uint32_t end = sample.length;
    if(edit)
    {
        SampleEdit e = *edit;
        SampleEdit_Clamp(e, sample.length);
        start = e.start_frame;
        end = e.end_frame;
    }
    if(end <= start + 1)
        return;

    int draw_w = w - 2;
    if(draw_w > 128)
        draw_w = 128;
    const int mid = y0 + h / 2;
    const int amp_h = (h - 2) / 2;

    static int16_t col_min[128];
    static int16_t col_max[128];
    const int peak = WaveformColumns(sample.pcm, start, end, draw_w, col_min, col_max);
    // Auto-fit: map the loudest column to ~95% of the half-height so the
    // waveform always fills the preview with a small margin top and bottom,
    // never jamming the edge (independent of the audio normalization setting).
    const float disp = 0.95f * static_cast<float>(amp_h) / static_cast<float>(peak);

    bool have_prev = false;
    int prev_x = 0;
    int prev_top = 0;
    int prev_bot = 0;
    for(int px = 0; px < draw_w; ++px)
    {
        const int y_top = mid - static_cast<int>(static_cast<float>(col_max[px]) * disp);
        const int y_bot = mid - static_cast<int>(static_cast<float>(col_min[px]) * disp);
        const int xx = x0 + 1 + px;
        int top = y_top;
        int bot = y_bot;
        if(top < y0 + 1) top = y0 + 1;
        if(bot > y1 - 1) bot = y1 - 1;
        if(bot < top)
            bot = top;

        if(outline_only)
        {
            if(have_prev)
            {
                d.DrawLine(prev_x, prev_top, xx, top, on);
                d.DrawLine(prev_x, prev_bot, xx, bot, on);
            }
            else
            {
                d.DrawPixel(xx, top, on);
                d.DrawPixel(xx, bot, on);
            }
            have_prev = true;
            prev_x = xx;
            prev_top = top;
            prev_bot = bot;
            continue;
        }

        for(int yy = top; yy <= bot; ++yy)
            d.DrawPixel(xx, yy, on);
    }
}

UiScreenId UiNav_Active(const UiNav& nav)
{
    return nav.stack[nav.top];
}
