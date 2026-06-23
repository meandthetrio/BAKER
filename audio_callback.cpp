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
#include "build_config.h"
#include "express_state.h"
#include "sd_sample_pool.h"
#include "craft/craft_chain.h"

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
// Mic input (in[1]) digital gain. This used to be a -10 dB pad to normalize the
// hotter condenser to line level, but a *post-ADC* pad can't add converter
// headroom: it only made the level meter read ~10 dB low, so chasing the "good"
// range drove the ADC into clipping (and stored a clipped signal sitting ~10 dB
// down, which also made the waveform look small). Unity keeps the meter honest
// so "good" on the meter means a clean, un-clipped converter level. Set the
// recording level with the source/mic distance, not a digital pad.
static constexpr float kMicInputGain = 1.0f;

// Review/record preview plays the raw recorded PCM straight to the output,
// bypassing the voice + master gain chain. At unity a near-full-scale sample
// hits the headphone amp at 0 dBFS (clean but painfully loud), so pad it down
// to a safe monitoring level.
static constexpr float kRecordPreviewGain = 0.2511886f; // 10^(-12/20)

// Live input monitor passthrough pad (line and mic). The monitor mixes the raw
// input straight to the output for level-setting; at unity a hot source is loud
// in the cans, so pad it to a comfortable monitoring level. This is a
// monitoring-only attenuation and does not affect the recorded/metered level.
static constexpr float kInputMonitorGain = 0.2511886f; // 10^(-12/20)

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
static bool     s_win_preview_active = false;
static uint32_t s_win_preview_pos = 0;
static float    s_win_preview_gain = 0.0f;
static bool     s_win_preview_stopping = false;

// Bake-screen sample preview (raw dry one-shot triggered by Button2 on the SD
// Manager in bake-pick mode). Owns its own static state mirroring win_preview.
// Unlike win_preview, this path OVERWRITES outL/outR — voice + FX output is
// discarded for the duration of the preview so the user hears the .wav raw.
static bool     s_bake_preview_active   = false;
static uint32_t s_bake_preview_pos      = 0;
static float    s_bake_preview_gain     = 0.0f;
static bool     s_bake_preview_stopping = false;
// CRAFT live audition: when the preview is triggered with the chain active, the
// loaded sample is processed through this chain block-by-block (WYSIWYG with the
// offline render, which uses the same CraftChain code). Dry preview otherwise.
static craft::CraftChain s_bake_preview_craft_chain;
static bool              s_bake_preview_use_chain = false;
static uint32_t          s_bake_preview_craft_seq = 0; // last-applied craft_cfg seqlock value
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
        float    rec_peak = 0.0f;
        for(size_t i = 0; i < size; ++i)
        {
            const float src = (s_rec_source == static_cast<uint8_t>(RecordInputSource::Mic))
                                  ? in[1][i] * kMicInputGain
                                  : in[0][i];
            const float a = std::fabs(src);
            if(a > rec_peak)
                rec_peak = a;
            if(!AudioCallback_WriteCaptureFrame(src, g_app.shared.recording, s_rec_pos, dst))
            {
                s_rec_active = false;
                AudioCallback_EndCapture(g_app.shared.recording, s_rec_pos);
                break;
            }
        }
        g_app.shared.recording.rec_pos.store(s_rec_pos, std::memory_order_release);
        g_app.shared.recording.rec_live_gen.fetch_add(1, std::memory_order_acq_rel);
        DiagnosticsAccumulatePeakAtomic(
            g_app.diag.gain_probe_peak_bits[kDiagGainProbeRecordPeak], rec_peak);
    }
    else
    {
        // Not capturing: publish a live input-level meter for the armed/ready
        // screen. Uses the same source mapping as the capture path so the bar
        // reflects exactly what would be recorded.
        const uint8_t src
            = g_app.shared.recording.rec_source_sel.load(std::memory_order_acquire) & 1u;
        float lvl_peak = 0.0f;
        for(size_t i = 0; i < size; ++i)
        {
            const float v = (src == static_cast<uint8_t>(RecordInputSource::Mic))
                                ? in[1][i] * kMicInputGain
                                : in[0][i];
            const float a = std::fabs(v);
            if(a > lvl_peak)
                lvl_peak = a;
        }
        g_app.shared.recording.rec_input_level_bits.store(
            DiagnosticsFloatToBits(lvl_peak), std::memory_order_release);
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

        const float dry
            = (static_cast<float>(sample.pcm[s_preview_pos]) / 32768.0f) * kRecordPreviewGain;
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

// Engine Trim one-shot: plays the selected window [start,end) of a snapshotted
// Sample exactly once (non-looping, even when engine loop mode is on), additive
// into the output. Auto-clears win_preview_active at the window end so the UI/LED
// can revert without any polling logic.
void AudioCallback_ProcessWindowPreview(float* outL, float* outR, size_t size)
{
    // ~1 ms declick at 48 kHz: ramp-in from window start, position-based fade toward
    // the window end, and a ramp-out on stop (Button2 toggle-off) so neither the
    // onset, the natural end, nor an early stop produces a click.
    constexpr uint32_t kWinFadeFrames = 48u;
    constexpr float    kWinFadeStep = 1.0f / static_cast<float>(kWinFadeFrames);

    auto& recording = g_app.shared.recording;
    if(recording.win_preview_stop_req.exchange(0, std::memory_order_acq_rel) != 0)
    {
        // Begin a fade-out rather than a hard cut; deactivation happens once the
        // envelope reaches zero in the loop below.
        if(s_win_preview_active)
            s_win_preview_stopping = true;
        else
            recording.win_preview_active.store(0, std::memory_order_release);
    }

    if(recording.win_preview_start_req.exchange(0, std::memory_order_acq_rel) != 0)
    {
        const Sample& sample = recording.win_preview_sample;
        uint32_t start = recording.win_preview_start;
        uint32_t end = recording.win_preview_end;
        if(sample.pcm != nullptr && sample.length > 0u && end > start && start < sample.length)
        {
            s_win_preview_active = true;
            s_win_preview_stopping = false;
            s_win_preview_gain = 0.0f;
            s_win_preview_pos = start;
            recording.win_preview_active.store(1, std::memory_order_release);
            recording.win_preview_pos.store(start, std::memory_order_release);
        }
        else
        {
            s_win_preview_active = false;
            s_win_preview_stopping = false;
            s_win_preview_pos = 0;
            recording.win_preview_active.store(0, std::memory_order_release);
            recording.win_preview_pos.store(0, std::memory_order_release);
        }
    }

    if(!s_win_preview_active)
        return;

    const Sample& sample = recording.win_preview_sample;
    uint32_t end = recording.win_preview_end;
    if(sample.pcm == nullptr || sample.length == 0u)
    {
        s_win_preview_active = false;
        s_win_preview_stopping = false;
        s_win_preview_pos = 0;
        recording.win_preview_active.store(0, std::memory_order_release);
        return;
    }
    if(end > sample.length)
        end = sample.length;

    for(size_t i = 0; i < size; ++i)
    {
        if(s_win_preview_pos >= end)
        {
            s_win_preview_active = false;
            recording.win_preview_active.store(0, std::memory_order_release);
            break;
        }

        // Envelope: ramp-in toward 1.0, or ramp-out toward 0.0 when stopping.
        const float target = s_win_preview_stopping ? 0.0f : 1.0f;
        if(s_win_preview_gain < target)
        {
            s_win_preview_gain += kWinFadeStep;
            if(s_win_preview_gain > target)
                s_win_preview_gain = target;
        }
        else if(s_win_preview_gain > target)
        {
            s_win_preview_gain -= kWinFadeStep;
            if(s_win_preview_gain < target)
                s_win_preview_gain = target;
        }
        if(s_win_preview_stopping && s_win_preview_gain <= 0.0f)
        {
            s_win_preview_active = false;
            recording.win_preview_active.store(0, std::memory_order_release);
            break;
        }

        // Position-based fade toward the window end (reaches ~0 at `end`).
        const uint32_t remaining = end - s_win_preview_pos;
        const float pos_gain
            = (remaining < kWinFadeFrames) ? (static_cast<float>(remaining) * kWinFadeStep) : 1.0f;

        const float dry
            = (static_cast<float>(sample.pcm[s_win_preview_pos]) / 32768.0f) * kRecordPreviewGain
              * s_win_preview_gain * pos_gain;
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
        ++s_win_preview_pos;
    }

    recording.win_preview_pos.store(s_win_preview_pos, std::memory_order_release);
    if(!s_win_preview_active)
    {
        s_win_preview_pos = 0;
        s_win_preview_stopping = false;
        s_win_preview_gain = 0.0f;
    }
}

// Bake-screen sample preview. UI/main posts start/stop; worker fills
// shared.bake_preview.sample (pcm + length) before posting start_req with
// release ordering. Audio thread plays [0, length) once at unity, OVERWRITING
// outL/outR (voice + FX output discarded) so the audition is truly raw dry.
// 1 ms ramp-in on activation, position-based fade in the last ~1 ms before
// end, ramp-out on stop_req. Auto-clears bake_preview.active at end of sample
// so the LED/UI revert without polling.
void AudioCallback_ProcessBakePreview(float* outL, float* outR, size_t size)
{
    constexpr uint32_t kFadeFrames = 48u;
    constexpr float    kFadeStep   = 1.0f / static_cast<float>(kFadeFrames);

    auto& bake = g_app.shared.bake_preview;

    if(bake.stop_req.exchange(0, std::memory_order_acq_rel) != 0)
    {
        if(s_bake_preview_active)
            s_bake_preview_stopping = true;
        else
            bake.active.store(0, std::memory_order_release);
    }

    if(bake.start_req.exchange(0, std::memory_order_acq_rel) != 0)
    {
        const Sample& sample = bake.sample;
        if(sample.pcm != nullptr && sample.length > 0u)
        {
            s_bake_preview_active   = true;
            s_bake_preview_stopping = false;
            s_bake_preview_gain     = 0.0f;
            s_bake_preview_pos      = 0;
            // Snapshot the CRAFT chain (config published before start_req).
            s_bake_preview_use_chain = (bake.craft_chain_active.load(std::memory_order_acquire) != 0u);
            if(s_bake_preview_use_chain)
            {
                s_bake_preview_craft_chain.ApplyConfig(bake.craft_cfg, g_sample_rate_hz);
                s_bake_preview_craft_seq = bake.craft_cfg_seq.load(std::memory_order_acquire);
            }
            bake.active.store(1, std::memory_order_release);
            bake.pos.store(0, std::memory_order_release);
        }
        else
        {
            s_bake_preview_active   = false;
            s_bake_preview_stopping = false;
            s_bake_preview_pos      = 0;
            bake.active.store(0, std::memory_order_release);
        }
    }

    if(!s_bake_preview_active)
        return;

    const Sample&  sample = bake.sample;
    const uint32_t end    = sample.length;
    if(sample.pcm == nullptr || end == 0u)
    {
        s_bake_preview_active   = false;
        s_bake_preview_stopping = false;
        s_bake_preview_pos      = 0;
        bake.active.store(0, std::memory_order_release);
        return;
    }

    // Pick up live param edits made during this playthrough (seqlock read): if
    // the config changed, re-apply coeffs without resetting state (smooth).
    if(s_bake_preview_use_chain)
    {
        const uint32_t s1 = bake.craft_cfg_seq.load(std::memory_order_acquire);
        if((s1 & 1u) == 0u && s1 != s_bake_preview_craft_seq)
        {
            const craft::CraftChainConfig local = bake.craft_cfg;
            const uint32_t s2 = bake.craft_cfg_seq.load(std::memory_order_acquire);
            if(s2 == s1)
            {
                s_bake_preview_craft_chain.UpdateParams(local);
                s_bake_preview_craft_seq = s1;
            }
        }
    }

    // When auditioning through the CRAFT chain, process the playable portion of
    // this block up front so the per-sample fade loop reads the degraded signal.
    // Block size is 48; cap defensively. craftbuf[k] maps to sample.pcm[pos0+k].
    float          craftbuf[64];
    const uint32_t pos0 = s_bake_preview_pos;
    if(s_bake_preview_use_chain)
    {
        const uint32_t remain  = end - pos0;
        uint32_t       craft_n = (size < remain) ? static_cast<uint32_t>(size) : remain;
        if(craft_n > 64u)
            craft_n = 64u;
        for(uint32_t k = 0; k < craft_n; ++k)
            craftbuf[k] = static_cast<float>(sample.pcm[pos0 + k]) * (1.0f / 32768.0f);
        s_bake_preview_craft_chain.Process(craftbuf, craft_n);
    }

    for(size_t i = 0; i < size; ++i)
    {
        if(s_bake_preview_pos >= end)
        {
            s_bake_preview_active = false;
            bake.active.store(0, std::memory_order_release);
            break;
        }

        const float target = s_bake_preview_stopping ? 0.0f : 1.0f;
        if(s_bake_preview_gain < target)
        {
            s_bake_preview_gain += kFadeStep;
            if(s_bake_preview_gain > target) s_bake_preview_gain = target;
        }
        else if(s_bake_preview_gain > target)
        {
            s_bake_preview_gain -= kFadeStep;
            if(s_bake_preview_gain < target) s_bake_preview_gain = target;
        }
        if(s_bake_preview_stopping && s_bake_preview_gain <= 0.0f)
        {
            s_bake_preview_active = false;
            bake.active.store(0, std::memory_order_release);
            break;
        }

        const uint32_t remaining = end - s_bake_preview_pos;
        const float pos_gain
            = (remaining < kFadeFrames) ? (static_cast<float>(remaining) * kFadeStep) : 1.0f;

        const float src = s_bake_preview_use_chain
                              ? craftbuf[s_bake_preview_pos - pos0]
                              : (static_cast<float>(sample.pcm[s_bake_preview_pos]) / 32768.0f);
        const float dry = src * s_bake_preview_gain * pos_gain;
        // Overwrite (not mix): voice + FX output is replaced by the preview.
        outL[i] = dry;
        outR[i] = dry;
        ++s_bake_preview_pos;
    }

    bake.pos.store(s_bake_preview_pos, std::memory_order_release);
    if(!s_bake_preview_active)
    {
        s_bake_preview_pos      = 0;
        s_bake_preview_stopping = false;
        s_bake_preview_gain     = 0.0f;
    }
}
} // namespace

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    const uint32_t start_cycles = DWT->CYCCNT;
    uint32_t       bucket_start = 0u;

    AudioCallback_ApplySdSampleHandoffs(g_voice, g_app.shared);

    // Only scan the input buffers for peak when the diagnostics overlay is up.
    // The probe is a per-sample abs+compare across both channels; ~1% CPU in
    // hot scenarios (10 voices + full FX), and the values are only ever read
    // by the overlay. Race on `visible` is benign: at worst we run one extra
    // block after the overlay closes.
    if(g_app.diag.overlay.visible)
    {
        float in_peak_l = 0.0f;
        float in_peak_r = 0.0f;
        for(size_t i = 0; i < size; ++i)
        {
            const float al = std::fabs(in[0][i]);
            const float ar = std::fabs(in[1][i] * kMicInputGain);
            if(al > in_peak_l)
                in_peak_l = al;
            if(ar > in_peak_r)
                in_peak_r = ar;
        }
        DiagnosticsAccumulatePeakAtomic(
            g_app.diag.gain_probe_peak_bits[kDiagGainProbeInputL], in_peak_l);
        DiagnosticsAccumulatePeakAtomic(
            g_app.diag.gain_probe_peak_bits[kDiagGainProbeInputR], in_peak_r);
    }

    AudioCallback_ProcessRecording(in, size);

    const uint32_t pre_tick_start = DWT->CYCCNT;
    g_params.AudioBlockTick(g_sample_rate_hz, size);

    // Macro snapshot + smoothing is owned by VoiceEngine; its RenderBlock
    // below updates g_voice.SmoothedMacros() for this block. Reading that
    // after RenderBlock is the single source of truth on the audio thread.

    // --- Param-push gating ---------------------------------------------------
    // Re-pushing all engine params into the voice engine every block is wasteful
    // when nothing changed. Two gates:
    //   static_push  : a short settle window after any UI edit (publish gen bump),
    //                  long enough to cover the 5 ms one-pole smoothing.
    //   express_push : whenever the mod-wheel/express overlay is live (it can move
    //                  cutoff/gain/resonance/env/poly-porto every block).
    // The "static" group (tune, drive mode, layer scale, loop enable, crossfade,
    // seam-baked, mod params) is pushed only on static_push; the "express" group is
    // pushed on either, so plain UI edits to its members are still covered.
    static uint32_t s_applied_publish_gen = 0u;
    static uint32_t s_param_push_settle_blocks = 0u;
    const uint32_t publish_gen = g_params.PublishGen();
    if(publish_gen != s_applied_publish_gen)
    {
        s_applied_publish_gen = publish_gen;
        s_param_push_settle_blocks = 64u; // ~64 ms; covers the 5 ms smoothing settle
    }
    const bool static_push = (s_param_push_settle_blocks > 0u);
    if(s_param_push_settle_blocks > 0u)
        --s_param_push_settle_blocks;
    const bool express_active
        = (g_app.shared.performance.express.enabled.load(std::memory_order_acquire) != 0u)
          && (g_app.shared.performance.express.midi_mod_seen.load(std::memory_order_acquire) != 0u);
    // express_active is driven by shared atomics (mod wheel / enable), not by the
    // publish generation, so flush one extra push on any transition — in particular
    // express_active going false must push the express group once to clear it.
    static bool s_prev_express_active = false;
    const bool express_changed = (express_active != s_prev_express_active);
    s_prev_express_active = express_active;
    const bool express_push = static_push || express_active || express_changed;

    if(static_push)
    {
        g_voice.SetModParams(g_params.current.lfo_rate_hz,
                             g_params.current.lfo_depth,
                             g_params.current.env_attack_ms,
                             g_params.current.env_decay_ms,
                             g_params.current.env_amount);
        g_voice.SetLfoWave(
            g_app.shared.performance.modulation.lfo_wave.load(std::memory_order_relaxed));
    }
    // voice_params (with express overlay) is built every block because the FX stage
    // below reuses it (fx_params = voice_params); only the engine setters are gated.
    PerformParamsCurrent voice_params = g_params.current;
    AudioCallback_ApplyExpressOverlay(g_app.shared, voice_params);
    const uint32_t pre_tick_cycles = DWT->CYCCNT - pre_tick_start;

    const uint32_t pre_push_start = DWT->CYCCNT;
    // Single layer: only slot 0's params are pushed into the (1-layer) engine.
    // The manifest/params still carry 2 slots (collapsed in Phase D), so cap the
    // push to the engine's layer count to avoid writing engine arrays out of bounds.
    for(uint8_t layer = 0; layer < 1; ++layer)
    {
        if(express_push)
        {
            // Express-reachable group: the mod wheel can move these every block.
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
                = g_app.shared.performance.express.midi_mod_seen.load(std::memory_order_acquire)
                  != 0u;
            uint8_t midi_mod_value
                = g_app.shared.performance.express.midi_mod_value.load(std::memory_order_acquire);
            if(midi_mod_value > 127u)
                midi_mod_value = 127u;
            const bool poly_porto_enabled
                = (g_app.shared.performance.express.enabled.load(std::memory_order_acquire) != 0u)
                  && midi_mod_seen && midi_mod_value > 63u && poly_porto_owned;
            g_voice.SetPolyPortoEnabled(layer, poly_porto_enabled);
            g_voice.SetPolyPortoVoiceLimit(layer, voice_params.express_poly_porto_voice_limit[layer]);
            g_voice.SetPolyPortoSlideMs(layer, voice_params.express_poly_porto_slide_ms[layer]);
            g_voice.SetPolyPortoSourceRangeSemitones(
                layer, voice_params.express_poly_porto_source_range_semitones[layer]);
            g_voice.SetPolyPortoSourceMode(layer, voice_params.express_poly_porto_source_mode[layer]);
            g_voice.SetPolyPortoReleaseMs(layer, voice_params.express_poly_porto_release_ms[layer]);
            g_voice.SetLoopEnvelopeParams(layer,
                                          voice_params.engine_loop_attack_ms[layer],
                                          voice_params.engine_loop_decay_ms[layer],
                                          voice_params.engine_loop_sustain_level[layer],
                                          voice_params.engine_loop_release_ms[layer]);
            g_voice.SetLoopEnvelopeCurves(layer,
                                          voice_params.engine_loop_attack_curve[layer] != 0u,
                                          voice_params.engine_loop_release_curve[layer] != 0u);
        }
        if(static_push)
        {
            // Static group: only changes on a UI edit.
            g_voice.SetEngineTuneSemitones(layer, voice_params.engine_tune_semitones[layer]);
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
            g_voice.SetLoopCrossfadeAmount(layer, voice_params.engine_loop_crossfade_amount[layer]);
            g_voice.SetLoopCrossfadeShape(layer, voice_params.engine_loop_crossfade_shape[layer]);
        }
        // Loop-enable and the seam-baked flag must NOT be gated behind
        // static_push: on project load the param publish happens before the
        // async sample restore finishes, so the brief settle window can lapse
        // before playback begins — leaving a loop layer audibly stuck in
        // one-shot until the user toggles it. Both are trivially cheap (a bool
        // assign), so push them every block to stay in lockstep with the
        // published value regardless of sample-load timing.
        g_voice.SetEngineLoopEnabled(layer, voice_params.engine_loop_mode[layer]);
        {
            const uint8_t baked = g_app.shared.sample.publish.sd_layer_seam_baked[layer]
                                      .load(std::memory_order_acquire);
            g_voice.SetLayerSeamBaked(layer, baked != 0u);
        }
    }
    // Global auto-normalize gate (cheap bool); push every block so a toggle takes
    // effect immediately without rescanning samples.
    g_voice.SetNormalizeEnabled(
        g_app.shared.settings_normalize_enabled.load(std::memory_order_acquire) != 0u);
    // Velocity-mod lanes are global (not per-layer); push both once. Must
    // precede ProcessEvents so note-ons fired this block read the current
    // config in StartVoice_.
    for(uint8_t lane = 0; lane < PerformParamsCurrent::kVelModLaneCount; ++lane)
    {
        g_voice.SetVelMod(lane,
                          voice_params.velmod_target[lane],
                          voice_params.velmod_amount[lane],
                          voice_params.velmod_threshold[lane],
                          voice_params.velmod_shape[lane],
                          voice_params.velmod_source[lane]);
    }
    // Keytrack volume (global): per-note gain across C1..C8, applied at note-on.
    g_voice.SetKeytrack(voice_params.perform_keytrack_tilt,
                        voice_params.perform_keytrack_amount_db,
                        voice_params.perform_keytrack_mid_note);
    const uint32_t pre_push_cycles = DWT->CYCCNT - pre_push_start;

    const uint32_t pre_events_start = DWT->CYCCNT;
    g_voice.ProcessEvents(g_evtq);
    const uint32_t pre_events_cycles = DWT->CYCCNT - pre_events_start;

    DiagnosticsStoreCycleBucket(g_app.diag,
                                kDiagAudioBucketCallbackPreVoice,
                                DWT->CYCCNT - start_cycles);
    DiagnosticsStoreCycleBucket(g_app.diag, kDiagAudioBucketPreParamsTick, pre_tick_cycles);
    DiagnosticsStoreCycleBucket(g_app.diag, kDiagAudioBucketPreParamPush, pre_push_cycles);
    DiagnosticsStoreCycleBucket(g_app.diag, kDiagAudioBucketPreEvents, pre_events_cycles);

    bucket_start = DWT->CYCCNT;
    g_voice.RenderBlock(out[0], out[1], size);
    DiagnosticsStoreCycleBucket(
        g_app.diag, kDiagAudioBucketVoiceRender, DWT->CYCCNT - bucket_start);

    PerformParamsCurrent fx_params = voice_params;
#if MOD_SYSTEM_ENABLED
    float drive = fx_params.sat_drive;
    Macros_Apply(g_voice.SmoothedMacros(), nullptr, nullptr, nullptr, nullptr, &drive);
    fx_params.sat_drive = drive;
#endif
    const bool sd_wav_load_busy
        = (g_app.shared.sample.publish.sd_wav_load_busy.load(std::memory_order_acquire) != 0);
    // Velmod 2b: hand the voice engine's per-effect send buses to the FX chain.
    // A null pointer means that effect got no send this block (so it isn't run
    // or injected just for a send).
    const float* send_buses[3]
        = {g_voice.SendBusActive(0) ? g_voice.SendBus(0) : nullptr,
           g_voice.SendBusActive(1) ? g_voice.SendBus(1) : nullptr,
           g_voice.SendBusActive(2) ? g_voice.SendBus(2) : nullptr};
    const bool sends_active = g_voice.SendsActiveLastBlock();
    bucket_start = DWT->CYCCNT;
    g_audio.ProcessBlock(out[0], out[1], out[0], out[1], size, fx_params, sd_wav_load_busy,
                         send_buses, sends_active);
    DiagnosticsStoreCycleBucket(g_app.diag, kDiagAudioBucketFxTotal, DWT->CYCCNT - bucket_start);

    bucket_start = DWT->CYCCNT;
    AudioCallback_ProcessRenderCapture(out[0], size);
    DiagnosticsStoreCycleBucket(
        g_app.diag, kDiagAudioBucketRenderCapture, DWT->CYCCNT - bucket_start);

    bucket_start = DWT->CYCCNT;
    AudioCallback_ProcessRecordPreview(out[0], out[1], size);
    AudioCallback_ProcessWindowPreview(out[0], out[1], size);
    // Bake-screen preview runs LAST among the preview/capture paths so it
    // overwrites voice + FX output cleanly. Sits before the monitor-input mix
    // intentionally — monitor (mic/line passthrough) is a separate concern
    // from the dry audition.
    AudioCallback_ProcessBakePreview(out[0], out[1], size);
    DiagnosticsStoreCycleBucket(
        g_app.diag, kDiagAudioBucketRecordPreview, DWT->CYCCNT - bucket_start);

    const bool monitor_on = (g_app.shared.recording.rec_monitor_enable.load(std::memory_order_acquire) != 0);
    uint32_t monitor_clamp_hits = 0u;
    bucket_start = DWT->CYCCNT;
    if(monitor_on)
    {
        const uint8_t src = g_app.shared.recording.rec_source_sel.load(std::memory_order_acquire) & 1u;
        for(size_t i = 0; i < size; ++i)
        {
            const float mon = (src == static_cast<uint8_t>(RecordInputSource::Mic))
                                  ? in[1][i] * kInputMonitorGain
                                  : in[0][i] * kInputMonitorGain;
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
    DiagnosticsStoreCycleBucket(g_app.diag, kDiagAudioBucketMonitor, DWT->CYCCNT - bucket_start);

    bucket_start = DWT->CYCCNT;
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
    DiagnosticsStoreCycleBucket(
        g_app.diag, kDiagAudioBucketFinalPeak, DWT->CYCCNT - bucket_start);

    const uint32_t used = DWT->CYCCNT - start_cycles;
    g_app.diag.audio_cycles_last.store(used, std::memory_order_relaxed);
    const uint32_t prev_peak = g_app.diag.audio_cycles_peak.load(std::memory_order_relaxed);
    if(used > prev_peak)
        g_app.diag.audio_cycles_peak.store(used, std::memory_order_relaxed);
    DiagnosticsStoreCycleBucket(g_app.diag, kDiagAudioBucketCallbackTotal, used);

    const uint32_t budget = g_app.diag.audio_budget_cycles.load(std::memory_order_relaxed);
    if(budget > 0 && used > budget)
        g_app.diag.audio_late_count.fetch_add(1, std::memory_order_relaxed);
}
