#pragma once

#include <atomic>
#include <cstdint>

#include "macros.h"
#include "mod_matrix.h"
#include "plocks.h"
#include "sample_edit.h"
#include "sampler_sample.h"
#include "storage_limits.h"

/*
`AppSharedState` is only for handoff-sensitive, publish/apply-boundary, or
otherwise cross-thread/cross-phase sensitive state.

Do not put a field in `shared` just because multiple systems touch it.
UI-only state belongs in `ui`.
Project save/load coordination belongs in `project` unless it is truly
handoff-sensitive shared state.
Worker request/progress bookkeeping belongs in `worker` unless it is truly
handoff-sensitive shared state.
Engine/editor state belongs in `engine` unless it is truly publish/apply-boundary
or otherwise sensitive shared state.

Keep this struct intentionally narrow. It is the highest-risk ownership bucket
and must not become a convenience dumping ground.
*/
struct AppSharedState
{
    struct SamplePublishState
    {
        Sample sd_slots[kSdSampleSlots]{};
        std::atomic<uint8_t> sd_current_slot{0};
        std::atomic<uint8_t> sd_published_slot{0};
        std::atomic<uint8_t> sd_published_ready{0};
        std::atomic<uint32_t> sd_published_gen{0};
        std::atomic<uint32_t> sd_applied_gen{0};
        SampleEdit sd_edit_slots[kSdSampleSlots]{};
        SampleEdit sd_edit_pending{};
        std::atomic<uint8_t> sd_edit_slot{0};
        std::atomic<uint8_t> sd_edit_ready{0};
        std::atomic<uint32_t> sd_edit_gen{0};
        std::atomic<uint32_t> sd_edit_applied_gen{0};
    } sample{};

    struct RecordingBridgeState
    {
        std::atomic<uint8_t> rec_source_sel{0};
        std::atomic<uint8_t> rec_monitor_enable{0};
        std::atomic<uint8_t> rec_start_req{0};
        std::atomic<uint8_t> rec_stop_req{0};
        std::atomic<uint8_t> rec_active{0};
        std::atomic<uint8_t> rec_slot_pending{0};
        std::atomic<uint32_t> rec_pos{0};
        std::atomic<uint32_t> rec_length{0};
        std::atomic<uint32_t> rec_live_gen{0};
        int16_t rec_live_min[128] = {};
        int16_t rec_live_max[128] = {};
        int16_t rec_live_last_col = -1;
    } recording{};

    struct PerformanceSharedState
    {
        ModMatrixState mod_matrix{};
        ModRoute mod_routes_ui[kMaxModRoutes]{};
        uint8_t mod_route_selected = 0;
        PLocksState plocks{};
        Pattern plock_pattern{};
        bool seq_running = false;
        bool plock_apply_enabled = false;
        std::atomic<uint8_t> lfo_wave{0};
        uint32_t seq_bpm = 120;
        uint32_t seq_last_ms = 0;
        uint32_t seq_accum_ms = 0;
        MacroState macro_ui{};
        MacroState macro_a{};
        MacroState macro_b{};
        std::atomic<uint8_t> macro_sel{0};
        std::atomic<uint32_t> macro_gen{0};
    } performance{};

    // Temporary compatibility aliases while call sites migrate to named lanes.
    Sample (&sd_slots)[kSdSampleSlots];
    std::atomic<uint8_t>& sd_current_slot;
    std::atomic<uint8_t>& sd_published_slot;
    std::atomic<uint8_t>& sd_published_ready;
    std::atomic<uint32_t>& sd_published_gen;
    std::atomic<uint32_t>& sd_applied_gen;
    SampleEdit (&sd_edit_slots)[kSdSampleSlots];
    SampleEdit& sd_edit_pending;
    std::atomic<uint8_t>& sd_edit_slot;
    std::atomic<uint8_t>& sd_edit_ready;
    std::atomic<uint32_t>& sd_edit_gen;
    std::atomic<uint32_t>& sd_edit_applied_gen;

    std::atomic<uint8_t>& rec_source_sel;
    std::atomic<uint8_t>& rec_monitor_enable;
    std::atomic<uint8_t>& rec_start_req;
    std::atomic<uint8_t>& rec_stop_req;
    std::atomic<uint8_t>& rec_active;
    std::atomic<uint8_t>& rec_slot_pending;
    std::atomic<uint32_t>& rec_pos;
    std::atomic<uint32_t>& rec_length;
    std::atomic<uint32_t>& rec_live_gen;
    int16_t (&rec_live_min)[128];
    int16_t (&rec_live_max)[128];
    int16_t& rec_live_last_col;

    ModMatrixState& mod_matrix;
    ModRoute (&mod_routes_ui)[kMaxModRoutes];
    uint8_t& mod_route_selected;
    PLocksState& plocks;
    Pattern& plock_pattern;
    bool& seq_running;
    bool& plock_apply_enabled;
    std::atomic<uint8_t>& lfo_wave;
    uint32_t& seq_bpm;
    uint32_t& seq_last_ms;
    uint32_t& seq_accum_ms;
    MacroState& macro_ui;
    MacroState& macro_a;
    MacroState& macro_b;
    std::atomic<uint8_t>& macro_sel;
    std::atomic<uint32_t>& macro_gen;

    AppSharedState()
    : sd_slots(sample.sd_slots),
      sd_current_slot(sample.sd_current_slot),
      sd_published_slot(sample.sd_published_slot),
      sd_published_ready(sample.sd_published_ready),
      sd_published_gen(sample.sd_published_gen),
      sd_applied_gen(sample.sd_applied_gen),
      sd_edit_slots(sample.sd_edit_slots),
      sd_edit_pending(sample.sd_edit_pending),
      sd_edit_slot(sample.sd_edit_slot),
      sd_edit_ready(sample.sd_edit_ready),
      sd_edit_gen(sample.sd_edit_gen),
      sd_edit_applied_gen(sample.sd_edit_applied_gen),
      rec_source_sel(recording.rec_source_sel),
      rec_monitor_enable(recording.rec_monitor_enable),
      rec_start_req(recording.rec_start_req),
      rec_stop_req(recording.rec_stop_req),
      rec_active(recording.rec_active),
      rec_slot_pending(recording.rec_slot_pending),
      rec_pos(recording.rec_pos),
      rec_length(recording.rec_length),
      rec_live_gen(recording.rec_live_gen),
      rec_live_min(recording.rec_live_min),
      rec_live_max(recording.rec_live_max),
      rec_live_last_col(recording.rec_live_last_col),
      mod_matrix(performance.mod_matrix),
      mod_routes_ui(performance.mod_routes_ui),
      mod_route_selected(performance.mod_route_selected),
      plocks(performance.plocks),
      plock_pattern(performance.plock_pattern),
      seq_running(performance.seq_running),
      plock_apply_enabled(performance.plock_apply_enabled),
      lfo_wave(performance.lfo_wave),
      seq_bpm(performance.seq_bpm),
      seq_last_ms(performance.seq_last_ms),
      seq_accum_ms(performance.seq_accum_ms),
      macro_ui(performance.macro_ui),
      macro_a(performance.macro_a),
      macro_b(performance.macro_b),
      macro_sel(performance.macro_sel),
      macro_gen(performance.macro_gen)
    {
    }
};
