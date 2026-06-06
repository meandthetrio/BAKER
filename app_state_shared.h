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
    struct SampleSharedState
    {
        struct PublishState
        {
            Sample sd_slots[kSdSampleSlots]{};
            std::atomic<uint8_t> sd_current_slot{0};
            std::atomic<uint8_t> sd_published_slot{0};
            std::atomic<uint8_t> sd_published_ready{0};
            std::atomic<uint32_t> sd_published_gen{0};
            std::atomic<uint32_t> sd_applied_gen{0};
            /// Set during SDRAM WAV `f_read` load only (not normalize). Audio/UI read for gating.
            std::atomic<uint8_t> sd_wav_load_busy{0};
        } publish{};

        struct EditState
        {
            SampleEdit sd_edit_slots[kSdSampleSlots]{};
            SampleEdit sd_edit_pending{};
            std::atomic<uint8_t> sd_edit_slot{0};
            std::atomic<uint8_t> sd_edit_ready{0};
            std::atomic<uint32_t> sd_edit_gen{0};
            std::atomic<uint32_t> sd_edit_applied_gen{0};
        } edit{};
    } sample{};

    struct RecordingBridgeState
    {
        // Normal physical-input recording bridge.
        std::atomic<uint8_t> rec_source_sel{0};
        std::atomic<uint8_t> rec_monitor_enable{0};
        std::atomic<uint8_t> rec_start_req{0};
        std::atomic<uint8_t> rec_stop_req{0};
        std::atomic<uint8_t> rec_active{0};
        std::atomic<uint8_t> rec_slot_pending{0};
        std::atomic<uint32_t> rec_pos{0};
        std::atomic<uint32_t> rec_length{0};
        std::atomic<uint32_t> rec_live_gen{0};
        // Dry record/review preview bridge. UI/main only requests start/stop;
        // audio thread owns active playback and frame position.
        std::atomic<uint8_t> preview_start_req{0};
        std::atomic<uint8_t> preview_stop_req{0};
        std::atomic<uint8_t> preview_active{0};
        std::atomic<uint32_t> preview_pos{0};
        // Engine Trim one-shot window-audition bridge (separate from the
        // render-review preview above so the two never collide). UI/main fills
        // win_preview_sample + start/end and posts start/stop; audio thread owns
        // active playback, plays [start,end) once, and auto-clears win_preview_active.
        std::atomic<uint8_t> win_preview_start_req{0};
        std::atomic<uint8_t> win_preview_stop_req{0};
        std::atomic<uint8_t> win_preview_active{0};
        std::atomic<uint32_t> win_preview_pos{0};
        Sample win_preview_sample{};
        uint32_t win_preview_start{0};
        uint32_t win_preview_end{0};
        // Internal render-capture bridge. UI/main owns requests and finalization;
        // audio thread owns active writes and frame-count publication.
        std::atomic<uint8_t> render_start_req{0};
        std::atomic<uint8_t> render_stop_req{0};
        std::atomic<uint8_t> render_active{0};
        std::atomic<uint8_t> render_done{0};
        std::atomic<uint32_t> render_frames{0};
        // `rec_*` mirrors below are reused by both normal recording and
        // internal render capture for the live waveform/review path. Callers
        // must ensure these flows never run at the same time.
        int16_t rec_live_min[128] = {};
        int16_t rec_live_max[128] = {};
        int16_t rec_live_last_col = -1;
        Sample rec_sample{};
        SampleEdit rec_edit{};
    } recording{};

    struct SdManageTrimState
    {
        Sample sample{};
        SampleEdit edit{};
    } sd_manage{};

    struct PerformanceSharedState
    {
        struct ModulationState
        {
            ModMatrixState mod_matrix{};
            ModRoute mod_routes_ui[kMaxModRoutes]{};
            uint8_t mod_route_selected = 0;
            std::atomic<uint8_t> lfo_wave{0};
        } modulation{};

        struct PLocksPatternState
        {
            PLocksState plocks{};
            Pattern plock_pattern{};
            bool plock_apply_enabled = false;
        } plocks{};

        struct SequencerTransportState
        {
            bool seq_running = false;
            uint32_t seq_bpm = 120;
            uint32_t seq_last_ms = 0;
            uint32_t seq_accum_ms = 0;
        } sequencer{};

        struct MacroSharedState
        {
            MacroState macro_ui{};
            MacroState macro_a{};
            MacroState macro_b{};
            std::atomic<uint8_t> macro_sel{0};
            std::atomic<uint32_t> macro_gen{0};
        } macros{};

        struct ExpressRuntimeState
        {
            std::atomic<uint8_t> enabled{0};
            std::atomic<uint8_t> midi_mod_seen{0};
            std::atomic<uint8_t> midi_mod_value{0};
        } express{};
    } performance{};

    AppSharedState()
    {
    }
};
