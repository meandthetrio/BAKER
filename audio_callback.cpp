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
#include "express_state.h"
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
static float Clamp01(float value)
{
    if(value < 0.0f)
        return 0.0f;
    if(value > 1.0f)
        return 1.0f;
    return value;
}

static float ExpressMapRangeFloat(float min_value, float max_value, float norm)
{
    return min_value + (max_value - min_value) * norm;
}

static void AudioCallback_ApplyExpressOverlay(const AppSharedState& shared,
                                              PerformParamsCurrent& params)
{
    const bool enabled = shared.performance.express.enabled.load(std::memory_order_acquire) != 0u;
    const bool midi_mod_seen
        = shared.performance.express.midi_mod_seen.load(std::memory_order_acquire) != 0u;
    if(!enabled || !midi_mod_seen)
        return;

    uint8_t midi_mod_value = shared.performance.express.midi_mod_value.load(std::memory_order_acquire);
    if(midi_mod_value > 127u)
        midi_mod_value = 127u;
    const float norm = static_cast<float>(midi_mod_value) / 127.0f;

    for(uint8_t layer = 0; layer < PerformParamsCurrent::kLayerCount; ++layer)
    {
        for(uint8_t row = 0; row < kExpressRowCount; ++row)
        {
            const uint8_t target = ExpressClampTarget(params.express_target[layer][row]);
            const float mapped = ExpressMapRangeFloat(static_cast<float>(params.express_min_value[layer][row]),
                                                     static_cast<float>(params.express_max_value[layer][row]),
                                                     norm);
            switch(target)
            {
                case kExpressNone:
                    break;
                case kExpressCutoff:
                    params.engine_filter_cutoff_hz[layer] = mapped;
                    break;
                case kExpressDrive:
                    params.engine_gain_db[layer] = mapped;
                    break;
                case kExpressResonance:
                    params.engine_filter_resonance[layer] = Clamp01(mapped * 0.01f);
                    break;
                case kExpressAttack:
                    params.engine_loop_attack_ms[layer] = mapped;
                    break;
                case kExpressSustain:
                    params.engine_loop_sustain_level[layer] = Clamp01(mapped * 0.01f);
                    break;
                case kExpressRelease:
                    params.engine_loop_release_ms[layer] = mapped;
                    break;
                case kExpressReverb:
                    params.reverb_mix = Clamp01(mapped * 0.01f);
                    params.reverb_on = (params.reverb_mix > 0.001f);
                    break;
                case kExpressPolyPorto:
                default:
                    break;
            }
        }
    }
}

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
static uint32_t s_rec_pos = 0;
static bool     s_preview_active = false;
static uint32_t s_preview_pos = 0;
static bool     s_render_active = false;
static uint32_t s_render_pos = 0;
static constexpr uint32_t kRecLiveWaveStride = 128u;

void AudioCallback_ResetCapturePreview(AppSharedState::RecordingBridgeState& recording)
{
    recording.rec_live_last_col = -1;
    for(int i = 0; i < 128; ++i)
    {
        recording.rec_live_min[i] = 0;
        recording.rec_live_max[i] = 0;
    }
}

void AudioCallback_BeginCapture(AppSharedState::RecordingBridgeState& recording, uint32_t& write_pos)
{
    write_pos = 0;
    recording.rec_pos.store(0, std::memory_order_release);
    recording.rec_length.store(0, std::memory_order_release);
    recording.rec_active.store(1, std::memory_order_release);
    AudioCallback_ResetCapturePreview(recording);
}

void AudioCallback_EndCapture(AppSharedState::RecordingBridgeState& recording, uint32_t write_pos)
{
    recording.rec_active.store(0, std::memory_order_release);
    recording.rec_length.store(write_pos, std::memory_order_release);
}

bool AudioCallback_WriteCaptureFrame(float src,
                                     AppSharedState::RecordingBridgeState& recording,
                                     uint32_t& write_pos,
                                     int16_t* dst)
{
    if(write_pos >= kSdSampleMaxFrames)
        return false;

    float clamped = src;
    if(clamped > 1.0f)
        clamped = 1.0f;
    if(clamped < -1.0f)
        clamped = -1.0f;
    const int16_t s16 = static_cast<int16_t>(clamped * 32767.0f);
    dst[write_pos] = s16;

    const int col = static_cast<int>((write_pos / kRecLiveWaveStride) % 128u);
    if(col >= 0 && col < 128)
    {
        if(col != recording.rec_live_last_col)
        {
            recording.rec_live_min[col] = s16;
            recording.rec_live_max[col] = s16;
            recording.rec_live_last_col = static_cast<int16_t>(col);
        }
        else
        {
            if(s16 < recording.rec_live_min[col])
                recording.rec_live_min[col] = s16;
            if(s16 > recording.rec_live_max[col])
                recording.rec_live_max[col] = s16;
        }
    }

    ++write_pos;
    return true;
}

void AudioCallback_ProcessRecording(AudioHandle::InputBuffer in, size_t size)
{
    if(g_app.shared.recording.rec_start_req.exchange(0, std::memory_order_acq_rel) != 0)
    {
        s_rec_source = g_app.shared.recording.rec_source_sel.load(std::memory_order_acquire) & 1u;
        s_rec_active = true;
        AudioCallback_BeginCapture(g_app.shared.recording, s_rec_pos);
    }

    if(g_app.shared.recording.rec_stop_req.exchange(0, std::memory_order_acq_rel) != 0 && s_rec_active)
    {
        s_rec_active = false;
        AudioCallback_EndCapture(g_app.shared.recording, s_rec_pos);
    }

    if(s_rec_active)
    {
        int16_t* dst = SdRecordBuffer();
        for(size_t i = 0; i < size; ++i)
        {
            const float src = (s_rec_source == static_cast<uint8_t>(RecordInputSource::Mic)) ? in[1][i]
                                                                                              : in[0][i];
            if(!AudioCallback_WriteCaptureFrame(src, g_app.shared.recording, s_rec_pos, dst))
            {
                s_rec_active = false;
                AudioCallback_EndCapture(g_app.shared.recording, s_rec_pos);
                break;
            }
        }
        g_app.shared.recording.rec_pos.store(s_rec_pos, std::memory_order_release);
        g_app.shared.recording.rec_live_gen.fetch_add(1, std::memory_order_acq_rel);
    }
}

void AudioCallback_ProcessRenderCapture(float* post_fx_left, size_t size)
{
    auto& recording = g_app.shared.recording;
    if(recording.render_start_req.exchange(0, std::memory_order_acq_rel) != 0)
    {
        s_render_active = true;
        recording.render_done.store(0, std::memory_order_release);
        recording.render_frames.store(0, std::memory_order_release);
        recording.render_active.store(1, std::memory_order_release);
        AudioCallback_BeginCapture(recording, s_render_pos);
    }

    if(recording.render_stop_req.exchange(0, std::memory_order_acq_rel) != 0 && s_render_active)
    {
        s_render_active = false;
        AudioCallback_EndCapture(recording, s_render_pos);
        recording.render_frames.store(s_render_pos, std::memory_order_release);
        recording.render_active.store(0, std::memory_order_release);
        recording.render_done.store(1, std::memory_order_release);
    }

    if(!s_render_active)
        return;

    int16_t* dst = SdRecordBuffer();
    for(size_t i = 0; i < size; ++i)
    {
        if(!AudioCallback_WriteCaptureFrame(post_fx_left[i], recording, s_render_pos, dst))
        {
            s_render_active = false;
            AudioCallback_EndCapture(recording, s_render_pos);
            recording.render_frames.store(s_render_pos, std::memory_order_release);
            recording.render_active.store(0, std::memory_order_release);
            recording.render_done.store(1, std::memory_order_release);
            break;
        }
    }
    recording.rec_pos.store(s_render_pos, std::memory_order_release);
    recording.render_frames.store(s_render_pos, std::memory_order_release);
    recording.rec_live_gen.fetch_add(1, std::memory_order_acq_rel);
}

void AudioCallback_ProcessRecordPreview(float* outL, float* outR, size_t size)
{
    auto& recording = g_app.shared.recording;
    if(recording.preview_stop_req.exchange(0, std::memory_order_acq_rel) != 0)
    {
        s_preview_active = false;
        s_preview_pos = 0;
        recording.preview_active.store(0, std::memory_order_release);
        recording.preview_pos.store(0, std::memory_order_release);
    }

    if(recording.preview_start_req.exchange(0, std::memory_order_acq_rel) != 0)
    {
        const Sample& sample = recording.rec_sample;
        if(sample.pcm != nullptr && sample.length > 0u)
        {
            s_preview_active = true;
            s_preview_pos = 0;
            recording.preview_active.store(1, std::memory_order_release);
            recording.preview_pos.store(0, std::memory_order_release);
        }
        else
        {
            s_preview_active = false;
            s_preview_pos = 0;
            recording.preview_active.store(0, std::memory_order_release);
            recording.preview_pos.store(0, std::memory_order_release);
        }
    }

    if(!s_preview_active)
        return;

    const Sample& sample = recording.rec_sample;
    if(sample.pcm == nullptr || sample.length == 0u)
    {
        s_preview_active = false;
        s_preview_pos = 0;
        recording.preview_active.store(0, std::memory_order_release);
        recording.preview_pos.store(0, std::memory_order_release);
        return;
    }

    for(size_t i = 0; i < size; ++i)
    {
        if(s_preview_pos >= sample.length)
        {
            s_preview_active = false;
            recording.preview_active.store(0, std::memory_order_release);
            break;
        }

        const float dry = static_cast<float>(sample.pcm[s_preview_pos]) / 32768.0f;
        float left = outL[i] + dry;
        float right = outR[i] + dry;
        if(left > 1.0f)
            left = 1.0f;
        else if(left < -1.0f)
            left = -1.0f;
        if(right > 1.0f)
            right = 1.0f;
        else if(right < -1.0f)
            right = -1.0f;
        outL[i] = left;
        outR[i] = right;
        ++s_preview_pos;
    }

    recording.preview_pos.store(s_preview_pos, std::memory_order_release);
    if(!s_preview_active)
        s_preview_pos = 0;
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

    // Macro snapshot + smoothing is owned by VoiceEngine; its RenderBlock
    // below updates g_voice.SmoothedMacros() for this block. Reading that
    // after RenderBlock is the single source of truth on the audio thread.

    g_voice.SetModParams(g_params.current.lfo_rate_hz,
                         g_params.current.lfo_depth,
                         g_params.current.env_attack_ms,
                         g_params.current.env_decay_ms,
                         g_params.current.env_amount);
    g_voice.SetLfoWave(g_app.shared.performance.modulation.lfo_wave.load(std::memory_order_relaxed));
    PerformParamsCurrent voice_params = g_params.current;
    AudioCallback_ApplyExpressOverlay(g_app.shared, voice_params);
    for(uint8_t layer = 0; layer < PerformParamsCurrent::kLayerCount; ++layer)
    {
        bool poly_porto_owned = false;
        for(uint8_t row = 0; row < kExpressRowCount; ++row)
        {
            if(ExpressTargetIsPolyPorto(voice_params.express_target[layer][row]))
            {
                poly_porto_owned = true;
                break;
            }
        }
        const bool midi_mod_seen
            = g_app.shared.performance.express.midi_mod_seen.load(std::memory_order_acquire) != 0u;
        uint8_t midi_mod_value
            = g_app.shared.performance.express.midi_mod_value.load(std::memory_order_acquire);
        if(midi_mod_value > 127u)
            midi_mod_value = 127u;
        const bool poly_porto_enabled = (g_app.shared.performance.express.enabled.load(std::memory_order_acquire) != 0u)
                                     && midi_mod_seen
                                     && midi_mod_value > 63u
                                     && poly_porto_owned;
        g_voice.SetPolyPortoEnabled(layer, poly_porto_enabled);
        g_voice.SetPolyPortoVoiceLimit(layer, voice_params.express_poly_porto_voice_limit[layer]);
        g_voice.SetPolyPortoSlideMs(layer, voice_params.express_poly_porto_slide_ms[layer]);
        g_voice.SetPolyPortoSourceRangeSemitones(
            layer, voice_params.express_poly_porto_source_range_semitones[layer]);
        g_voice.SetPolyPortoSourceMode(layer, voice_params.express_poly_porto_source_mode[layer]);
        g_voice.SetPolyPortoReleaseMs(layer, voice_params.express_poly_porto_release_ms[layer]);
        g_voice.SetEngineTuneSemitones(layer, voice_params.engine_tune_semitones[layer]);
        g_voice.SetEngineGainDb(layer, voice_params.engine_gain_db[layer]);
        g_voice.SetEngineDriveMode(layer, voice_params.engine_drive_mode[layer]);
        float layer_level = voice_params.engine_layer_master_level[layer];
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
        g_voice.SetEngineFilterCutoffHz(layer, voice_params.engine_filter_cutoff_hz[layer]);
        g_voice.SetEngineFilterResonance(layer, voice_params.engine_filter_resonance[layer]);
        g_voice.SetEngineLoopEnabled(layer, voice_params.engine_loop_mode[layer]);
        g_voice.SetLoopEnvelopeParams(layer,
                                      voice_params.engine_loop_attack_ms[layer],
                                      voice_params.engine_loop_decay_ms[layer],
                                      voice_params.engine_loop_sustain_level[layer],
                                      voice_params.engine_loop_release_ms[layer]);
        g_voice.SetLoopCrossfadeAmount(layer, voice_params.engine_loop_crossfade_amount[layer]);
        g_voice.SetLoopCrossfadeShape(layer, voice_params.engine_loop_crossfade_shape[layer]);
    }
    g_voice.ProcessEvents(g_evtq);
    g_voice.RenderBlock(out[0], out[1], size);

    PerformParamsCurrent fx_params = voice_params;
    float drive = fx_params.sat_drive;
    Macros_Apply(g_voice.SmoothedMacros(), nullptr, nullptr, nullptr, nullptr, &drive);
    fx_params.sat_drive = drive;
    const bool sd_wav_load_busy
        = (g_app.shared.sample.publish.sd_wav_load_busy.load(std::memory_order_acquire) != 0);
    g_audio.ProcessBlock(out[0], out[1], out[0], out[1], size, fx_params, sd_wav_load_busy);
    AudioCallback_ProcessRenderCapture(out[0], size);
    AudioCallback_ProcessRecordPreview(out[0], out[1], size);

    const bool monitor_on = (g_app.shared.recording.rec_monitor_enable.load(std::memory_order_acquire) != 0);
    uint32_t monitor_clamp_hits = 0u;
    if(monitor_on)
    {
        const uint8_t src = g_app.shared.recording.rec_source_sel.load(std::memory_order_acquire) & 1u;
        for(size_t i = 0; i < size; ++i)
        {
            const float mon = (src == static_cast<uint8_t>(RecordInputSource::Mic)) ? in[1][i] : in[0][i];
            float l = out[0][i] + mon;
            float r = out[1][i] + mon;
            if(l > 1.0f)
            {
                l = 1.0f;
                ++monitor_clamp_hits;
            }
            if(l < -1.0f)
            {
                l = -1.0f;
                ++monitor_clamp_hits;
            }
            if(r > 1.0f)
            {
                r = 1.0f;
                ++monitor_clamp_hits;
            }
            if(r < -1.0f)
            {
                r = -1.0f;
                ++monitor_clamp_hits;
            }
            out[0][i] = l;
            out[1][i] = r;
        }
    }
    if(monitor_clamp_hits > 0u)
        g_app.diag.monitor_clamp_hits.fetch_add(monitor_clamp_hits, std::memory_order_relaxed);

    float out_peak = 0.0f;
    for(size_t i = 0; i < size; ++i)
    {
        const float abs_l = std::fabs(out[0][i]);
        const float abs_r = std::fabs(out[1][i]);
        if(abs_l > out_peak)
            out_peak = abs_l;
        if(abs_r > out_peak)
            out_peak = abs_r;
    }
    DiagnosticsAccumulatePeakAtomic(
        g_app.diag.gain_probe_peak_bits[kDiagGainProbeOutFinal], out_peak);

    const uint32_t used = DWT->CYCCNT - start_cycles;
    g_app.diag.audio_cycles_last.store(used, std::memory_order_relaxed);
    const uint32_t prev_peak = g_app.diag.audio_cycles_peak.load(std::memory_order_relaxed);
    if(used > prev_peak)
        g_app.diag.audio_cycles_peak.store(used, std::memory_order_relaxed);

    const uint32_t budget = g_app.diag.audio_budget_cycles.load(std::memory_order_relaxed);
    if(budget > 0 && used > budget)
        g_app.diag.audio_late_count.fetch_add(1, std::memory_order_relaxed);
}
