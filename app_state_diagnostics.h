#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "ui_overlay.h"

enum DiagGainProbe : uint8_t
{
    kDiagGainProbeAPre = 0,
    kDiagGainProbeAPost,
    kDiagGainProbeBPre,
    kDiagGainProbeBPost,
    kDiagGainProbeSumPreFx,
    kDiagGainProbeFxPreMaster,
    kDiagGainProbeOutFinal,
    kDiagGainProbeCount
};

enum DiagAudioBucket : uint8_t
{
    kDiagAudioBucketCallbackTotal = 0,
    kDiagAudioBucketCallbackPreVoice,
    kDiagAudioBucketPreParamsTick,
    kDiagAudioBucketPreParamPush,
    kDiagAudioBucketPreEvents,
    kDiagAudioBucketVoiceRender,
    kDiagAudioBucketFxTotal,
    kDiagAudioBucketSat,
    kDiagAudioBucketEq,
    kDiagAudioBucketDelay,
    kDiagAudioBucketReverb,
    kDiagAudioBucketMaster,
    kDiagAudioBucketRenderCapture,
    kDiagAudioBucketRecordPreview,
    kDiagAudioBucketMonitor,
    kDiagAudioBucketFinalPeak,
    kDiagAudioBucketCount
};

enum DiagVoiceBucket : uint8_t
{
    kDiagVoiceBucketRenderTotal = 0,
    kDiagVoiceBucketFixedSetup,
    kDiagVoiceBucketClearBuffers,
    kDiagVoiceBucketActiveVoicesTotal,
    kDiagVoiceBucketNormalVoices,
    kDiagVoiceBucketStealVoices,
    kDiagVoiceBucketFetch,
    kDiagVoiceBucketEnvMix,
    kDiagVoiceBucketLayerMix,
    kDiagVoiceBucketLayerEmphasis,
    // P-investigate: finer split of the per-voice block-start setup, to confirm
    // where the "~8% per-voice setup" actually goes before optimizing.
    //   VoiceSetup = ResolveEffectivePlaybackRegion_ + slot lookups + seam +
    //                loop-fade threshold precompute (everything before the env
    //                pre-sim and the render loop).
    //   EnvPresim  = the block-start StepEnvelope pre-simulation loop.
    kDiagVoiceBucketVoiceSetup,
    kDiagVoiceBucketEnvPresim,
    // P-investigate: split the fast-path fetch loop. FetchSeamCycles = cycles
    // spent in the loop-seam crossfade branch; FetchSeamCount = number of
    // samples that took that branch this block (summed across voices). Lets us
    // separate seam-crossfade cost from the plain inlined read.
    kDiagVoiceBucketFetchSeamCycles,
    kDiagVoiceBucketFetchSeamCount,
    kDiagVoiceBucketCount
};

static constexpr float kDiagGainFloorDb = -99.9f;

inline uint32_t DiagnosticsFloatToBits(float value)
{
    uint32_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

inline float DiagnosticsBitsToFloat(uint32_t bits)
{
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

inline void DiagnosticsAccumulatePeakAtomic(std::atomic<uint32_t>& peak_bits, float magnitude)
{
    if(!(magnitude > 0.0f))
        return;

    const uint32_t candidate = DiagnosticsFloatToBits(magnitude);
    uint32_t current = peak_bits.load(std::memory_order_relaxed);
    while(current < candidate
          && !peak_bits.compare_exchange_weak(
              current, candidate, std::memory_order_relaxed, std::memory_order_relaxed))
    {
    }
}

inline float DiagnosticsPeakToDbfs(float magnitude)
{
    if(!(magnitude > 0.0f))
        return kDiagGainFloorDb;

    float db = 20.0f * std::log10(magnitude);
    if(db < kDiagGainFloorDb)
        db = kDiagGainFloorDb;
    return db;
}

inline float DiagnosticsReadAndResetPeakDbfs(std::atomic<uint32_t>& peak_bits)
{
    return DiagnosticsPeakToDbfs(
        DiagnosticsBitsToFloat(peak_bits.exchange(0u, std::memory_order_relaxed)));
}

// Non-functional overlay instrumentation and runtime counters/debug values.
struct AppDiagnosticsState
{
    // Main-thread overlay timing and render instrumentation.
    UiOverlayState overlay{};
    uint16_t render_ms = 0;
    uint16_t render_hi_ms = 0;
    uint32_t render_skips = 0;
    uint32_t render_frames = 0;
    uint32_t render_cooldown_until_ms = 0;

    // Audio/runtime counters surfaced for debug and status rendering only.
    std::atomic<uint32_t> events_pushed{0};
    std::atomic<uint32_t> events_popped{0};
    std::atomic<uint32_t> queue_overflows{0};
    std::atomic<uint32_t> midi_rx_count{0};
    std::atomic<uint32_t> loop_mode{0};
    std::atomic<uint32_t> clip_count{0};
    std::atomic<uint32_t> fadeouts_started{0};
    std::atomic<uint32_t> voices_active{0};
    std::atomic<uint32_t> voices_peak_1s{0};
    std::atomic<uint32_t> voice_steals{0};
    std::atomic<uint32_t> last_stolen_voice_index{0};
    std::atomic<uint32_t> last_stolen_start_id{0};
    std::atomic<uint32_t> last_new_start_id{0};
    std::atomic<uint32_t> audio_cycles_last{0};
    std::atomic<uint32_t> audio_cycles_peak{0};
    std::atomic<uint32_t> audio_budget_cycles{0};
    std::atomic<uint32_t> audio_late_count{0};
    std::atomic<uint32_t> audio_bucket_cycles_last[kDiagAudioBucketCount]{};
    std::atomic<uint32_t> audio_bucket_cycles_peak[kDiagAudioBucketCount]{};
    std::atomic<uint32_t> voice_bucket_cycles_last[kDiagVoiceBucketCount]{};
    std::atomic<uint32_t> voice_bucket_cycles_peak[kDiagVoiceBucketCount]{};
    std::atomic<uint32_t> last_voice_packed{0};
    std::atomic<uint32_t> last_sample_index{0};
    std::atomic<uint32_t> last_vel_layer{0};
    std::atomic<uint32_t> last_velocity{0};
    std::atomic<int32_t> last_lfo{0};
    std::atomic<int32_t> last_env{0};
    std::atomic<uint32_t> lfo_rate_dbg{0};
    std::atomic<uint32_t> lfo_depth_dbg{0};
    std::atomic<uint32_t> playhead_frame[2]{{0}, {0}};
    std::atomic<uint32_t> playhead_active[2]{{0}, {0}};
    std::atomic<uint32_t> gain_probe_peak_bits[kDiagGainProbeCount]{{0},
                                                                    {0},
                                                                    {0},
                                                                    {0},
                                                                    {0},
                                                                    {0},
                                                                    {0}};
    float gain_probe_display_db[kDiagGainProbeCount]{kDiagGainFloorDb,
                                                     kDiagGainFloorDb,
                                                     kDiagGainFloorDb,
                                                     kDiagGainFloorDb,
                                                     kDiagGainFloorDb,
                                                     kDiagGainFloorDb,
                                                     kDiagGainFloorDb};
    std::atomic<uint32_t> sat_softclip_hits{0};
    std::atomic<uint32_t> master_softclip_hits{0};
    std::atomic<uint32_t> monitor_clamp_hits{0};
};

inline void DiagnosticsStoreCycleBucket(AppDiagnosticsState& diagnostics,
                                        DiagAudioBucket       bucket,
                                        uint32_t              cycles)
{
    const uint8_t index = static_cast<uint8_t>(bucket);
    diagnostics.audio_bucket_cycles_last[index].store(cycles, std::memory_order_relaxed);
    const uint32_t prev_peak
        = diagnostics.audio_bucket_cycles_peak[index].load(std::memory_order_relaxed);
    if(cycles > prev_peak)
        diagnostics.audio_bucket_cycles_peak[index].store(cycles, std::memory_order_relaxed);
}

inline void DiagnosticsResetAudioCyclePeaks(AppDiagnosticsState& diagnostics)
{
    diagnostics.audio_cycles_peak.store(0u, std::memory_order_relaxed);
    diagnostics.audio_late_count.store(0u, std::memory_order_relaxed);
    for(uint8_t i = 0; i < kDiagAudioBucketCount; ++i)
        diagnostics.audio_bucket_cycles_peak[i].store(0u, std::memory_order_relaxed);
}

inline void DiagnosticsStoreVoiceCycleBucket(AppDiagnosticsState& diagnostics,
                                             DiagVoiceBucket      bucket,
                                             uint32_t             cycles)
{
    const uint8_t index = static_cast<uint8_t>(bucket);
    diagnostics.voice_bucket_cycles_last[index].store(cycles, std::memory_order_relaxed);
    const uint32_t prev_peak
        = diagnostics.voice_bucket_cycles_peak[index].load(std::memory_order_relaxed);
    if(cycles > prev_peak)
        diagnostics.voice_bucket_cycles_peak[index].store(cycles, std::memory_order_relaxed);
}

inline void DiagnosticsResetVoiceCyclePeaks(AppDiagnosticsState& diagnostics)
{
    for(uint8_t i = 0; i < kDiagVoiceBucketCount; ++i)
        diagnostics.voice_bucket_cycles_peak[i].store(0u, std::memory_order_relaxed);
}
