#pragma once

#include <atomic>
#include <cstdint>

#include "ui_overlay.h"

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
};
