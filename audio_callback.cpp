#include "daisy_pod.h"
#include "daisy_core.h"
#include "stm32h7xx.h"

#include "app_state.h"
#include "params.h"
#include "audio_engine.h"
#include "event_queue.h"
#include "voice_engine.h"
#include "mod_matrix.h"
#include "plocks.h"
#include "macros.h"
#include "sd_sample_pool.h"

#include <cmath>

using namespace daisy;

extern AppState g_app;
extern Params g_params;
extern AudioEngine g_audio;
extern VoiceEngine g_voice;
extern EventQueueSPSC g_evtq;
extern float g_sample_rate_hz;

namespace
{
constexpr float kMacroSmoothSec = 0.005f;

void AudioCallback_ApplySdSampleHandoffs(VoiceEngine& voice, AppSharedState& shared)
{
    const uint8_t ready = shared.sample.publish.sd_published_ready.load(std::memory_order_acquire);
    const uint32_t pub_gen = shared.sample.publish.sd_published_gen.load(std::memory_order_acquire);
    const uint32_t applied_gen = shared.sample.publish.sd_applied_gen.load(std::memory_order_acquire);
    if(ready && pub_gen != applied_gen)
    {
        uint8_t slot = shared.sample.publish.sd_published_slot.load(std::memory_order_acquire);
        if(slot >= kSdSampleSlots)
            slot = 0;
        voice.SetSample(&shared.sample.publish.sd_slots[slot]);
        shared.sample.publish.sd_applied_gen.store(pub_gen, std::memory_order_release);
        shared.sample.publish.sd_current_slot.store(slot, std::memory_order_release);
        shared.sample.publish.sd_published_ready.store(0, std::memory_order_release);
    }

    const uint8_t edit_ready = shared.sample.edit.sd_edit_ready.load(std::memory_order_acquire);
    const uint32_t edit_gen = shared.sample.edit.sd_edit_gen.load(std::memory_order_acquire);
    const uint32_t edit_applied = shared.sample.edit.sd_edit_applied_gen.load(std::memory_order_acquire);
    if(edit_ready && edit_gen != edit_applied)
    {
        uint8_t slot = shared.sample.edit.sd_edit_slot.load(std::memory_order_acquire);
        if(slot >= kSdSampleSlots)
            slot = 0;
        voice.SetSampleEdit(shared.sample.edit.sd_edit_pending, &shared.sample.publish.sd_slots[slot]);
        shared.sample.edit.sd_edit_applied_gen.store(edit_gen, std::memory_order_release);
        shared.sample.edit.sd_edit_ready.store(0, std::memory_order_release);
    }
}

static bool     s_rec_active = false;
static uint8_t  s_rec_source = 0;
static uint8_t  s_rec_slot = 0;
static uint32_t s_rec_pos = 0;
static constexpr uint32_t kRecLiveWaveStride = 128u;

void AudioCallback_ProcessRecording(AudioHandle::InputBuffer in, size_t size)
{
    if(g_app.shared.recording.rec_start_req.exchange(0, std::memory_order_acq_rel) != 0)
    {
        s_rec_source = g_app.shared.recording.rec_source_sel.load(std::memory_order_acquire) & 1u;
        s_rec_slot = g_app.shared.recording.rec_slot_pending.load(std::memory_order_acquire) & 1u;
        s_rec_pos = 0;
        s_rec_active = true;
        g_app.shared.recording.rec_pos.store(0, std::memory_order_release);
        g_app.shared.recording.rec_length.store(0, std::memory_order_release);
        g_app.shared.recording.rec_active.store(1, std::memory_order_release);
        g_app.shared.recording.rec_live_last_col = -1;
        for(int i = 0; i < 128; ++i)
        {
            g_app.shared.recording.rec_live_min[i] = 0;
            g_app.shared.recording.rec_live_max[i] = 0;
        }
    }

    if(g_app.shared.recording.rec_stop_req.exchange(0, std::memory_order_acq_rel) != 0 && s_rec_active)
    {
        s_rec_active = false;
        g_app.shared.recording.rec_active.store(0, std::memory_order_release);
        g_app.shared.recording.rec_length.store(s_rec_pos, std::memory_order_release);
    }

    if(s_rec_active)
    {
        int16_t* dst = SdSampleBuffer(s_rec_slot);
        const uint32_t max_frames = kSdSampleMaxFrames;
        for(size_t i = 0; i < size; ++i)
        {
            if(s_rec_pos >= max_frames)
            {
                s_rec_active = false;
                g_app.shared.recording.rec_active.store(0, std::memory_order_release);
                g_app.shared.recording.rec_length.store(s_rec_pos, std::memory_order_release);
                break;
            }

            const float src = (s_rec_source == static_cast<uint8_t>(RecordInputSource::Mic)) ? in[1][i]
                                                                                              : in[0][i];
            float clamped = src;
            if(clamped > 1.0f)
                clamped = 1.0f;
            if(clamped < -1.0f)
                clamped = -1.0f;
            const int16_t s16 = static_cast<int16_t>(clamped * 32767.0f);
            dst[s_rec_pos] = s16;

            const int col = static_cast<int>((s_rec_pos / kRecLiveWaveStride) % 128u);
            if(col >= 0 && col < 128)
            {
                if(col != g_app.shared.recording.rec_live_last_col)
                {
                    g_app.shared.recording.rec_live_min[col] = s16;
                    g_app.shared.recording.rec_live_max[col] = s16;
                    g_app.shared.recording.rec_live_last_col = static_cast<int16_t>(col);
                }
                else
                {
                    if(s16 < g_app.shared.recording.rec_live_min[col])
                        g_app.shared.recording.rec_live_min[col] = s16;
                    if(s16 > g_app.shared.recording.rec_live_max[col])
                        g_app.shared.recording.rec_live_max[col] = s16;
                }
            }

            ++s_rec_pos;
        }
        g_app.shared.recording.rec_pos.store(s_rec_pos, std::memory_order_release);
        g_app.shared.recording.rec_live_gen.fetch_add(1, std::memory_order_acq_rel);
    }
}
} // namespace

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    const uint32_t start_cycles = DWT->CYCCNT;

    AudioCallback_ApplySdSampleHandoffs(g_voice, g_app.shared);
    AudioCallback_ProcessRecording(in, size);

    g_params.AudioBlockTick(g_sample_rate_hz, size);

    static MacroState s_active_macros{};
    static MacroState s_macro_smoothed{};
    static uint32_t   s_macro_gen_seen = 0;
    static bool       s_macro_init = false;
    if(!s_macro_init)
    {
        Macros_InitState(s_active_macros);
        Macros_InitState(s_macro_smoothed);
        s_macro_init = true;
    }
    const uint32_t macro_gen = g_app.shared.performance.macros.macro_gen.load(std::memory_order_acquire);
    if(macro_gen != s_macro_gen_seen)
    {
        const uint8_t sel = g_app.shared.performance.macros.macro_sel.load(std::memory_order_acquire) & 1u;
        s_active_macros = (sel == 0) ? g_app.shared.performance.macros.macro_a : g_app.shared.performance.macros.macro_b;
        s_macro_gen_seen = macro_gen;
    }
    const float dt_block_sec = static_cast<float>(size) / g_sample_rate_hz;
    float macro_coeff = 1.0f - std::exp(-dt_block_sec / kMacroSmoothSec);
    if(macro_coeff < 0.0f)
        macro_coeff = 0.0f;
    if(macro_coeff > 1.0f)
        macro_coeff = 1.0f;
    Macros_Smooth(s_macro_smoothed, s_active_macros, macro_coeff);

    g_voice.SetModParams(g_params.current.lfo_rate_hz,
                         g_params.current.lfo_depth,
                         g_params.current.env_attack_ms,
                         g_params.current.env_decay_ms,
                         g_params.current.env_amount);
    g_voice.SetLfoWave(g_app.shared.performance.modulation.lfo_wave.load(std::memory_order_relaxed));
    for(uint8_t layer = 0; layer < PerformParamsCurrent::kLayerCount; ++layer)
    {
        g_voice.SetEngineTuneSemitones(layer, g_params.current.engine_tune_semitones[layer]);
        g_voice.SetEngineGainDb(layer, g_params.current.engine_gain_db[layer]);
        g_voice.SetEngineDriveMode(layer, g_params.current.engine_drive_mode[layer]);
        float layer_level = g_params.current.engine_layer_master_level[layer];
        if(layer_level < 0.0f)
            layer_level = 0.0f;
        if(layer_level > 2.0f)
            layer_level = 2.0f;
        static constexpr float kPolyHeadroomScale = 0.15f;
        static constexpr float kBypassGain = 1.0f / kPolyHeadroomScale;
        float t_boost = 0.0f;
        if(layer_level > 1.0f)
        {
            t_boost = layer_level - 1.0f;
            if(t_boost < 0.0f)
                t_boost = 0.0f;
            if(t_boost > 1.0f)
                t_boost = 1.0f;
        }
        const float bypass_comp = 1.0f + t_boost * (kBypassGain - 1.0f);
        g_voice.SetEngineLayerScale(layer, layer_level * bypass_comp);
        g_voice.SetEngineFilterCutoffHz(layer, g_params.current.engine_filter_cutoff_hz[layer]);
        g_voice.SetEngineFilterResonance(layer, g_params.current.engine_filter_resonance[layer]);
        g_voice.SetEngineLoopEnabled(layer, g_params.current.engine_loop_mode[layer]);
        g_voice.SetLoopEnvelopeParams(layer,
                                      g_params.current.engine_loop_attack_ms[layer],
                                      g_params.current.engine_loop_decay_ms[layer],
                                      g_params.current.engine_loop_sustain_level[layer],
                                      g_params.current.engine_loop_release_ms[layer]);
        g_voice.SetLoopCrossfadeAmount(layer, g_params.current.engine_loop_crossfade_amount[layer]);
        g_voice.SetLoopCrossfadeShape(layer, g_params.current.engine_loop_crossfade_shape[layer]);
    }
    g_voice.ProcessEvents(g_evtq);
    g_voice.RenderBlock(out[0], out[1], size);

    PerformParamsCurrent fx_params = g_params.current;
    float drive = fx_params.sat_drive;
    Macros_Apply(s_macro_smoothed, nullptr, nullptr, nullptr, nullptr, &drive);
    fx_params.sat_drive = drive;
    g_audio.ProcessBlock(out[0], out[1], out[0], out[1], size, fx_params);

    const bool monitor_on = (g_app.shared.recording.rec_monitor_enable.load(std::memory_order_acquire) != 0);
    if(monitor_on)
    {
        const uint8_t src = g_app.shared.recording.rec_source_sel.load(std::memory_order_acquire) & 1u;
        for(size_t i = 0; i < size; ++i)
        {
            const float mon = (src == static_cast<uint8_t>(RecordInputSource::Mic)) ? in[1][i] : in[0][i];
            float l = out[0][i] + mon;
            float r = out[1][i] + mon;
            if(l > 1.0f)
                l = 1.0f;
            if(l < -1.0f)
                l = -1.0f;
            if(r > 1.0f)
                r = 1.0f;
            if(r < -1.0f)
                r = -1.0f;
            out[0][i] = l;
            out[1][i] = r;
        }
    }

    const uint32_t used = DWT->CYCCNT - start_cycles;
    g_app.diag.audio_cycles_last.store(used, std::memory_order_relaxed);
    const uint32_t prev_peak = g_app.diag.audio_cycles_peak.load(std::memory_order_relaxed);
    if(used > prev_peak)
        g_app.diag.audio_cycles_peak.store(used, std::memory_order_relaxed);

    const uint32_t budget = g_app.diag.audio_budget_cycles.load(std::memory_order_relaxed);
    if(budget > 0 && used > budget)
        g_app.diag.audio_late_count.fetch_add(1, std::memory_order_relaxed);
}
