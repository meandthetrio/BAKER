#pragma once

#include <atomic>
#include <cstdint>

#include "bk_file_format.h"
#include "craft/craft_params.h"
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
            // Per-layer "loop crossfade is baked into this slot's PCM" flag. Written by
            // the worker (bake on load) and the seam-edit screen; read every block by
            // the audio thread to suppress the runtime seam/boundary fade (no double
            // crossfade). Uses a shared atomic rather than the params path because the
            // worker cannot publish params targets.
            std::atomic<uint8_t> sd_layer_seam_baked[kSdSampleSlots]{};
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
        // Always-on input-level meter for the armed/ready screen. Holds the
        // latest audio-block peak (float magnitude, stored as bits) for the
        // currently selected source so the UI can show whether the incoming
        // signal sits in a healthy recording range.
        std::atomic<uint32_t> rec_input_level_bits{0};
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

    // Bake-screen sample preview bridge. UI/main posts start/stop; worker fills
    // `sample` (pcm + length) before posting start_req with release ordering so
    // the audio thread sees a valid Sample before it activates. Audio thread
    // owns active playback, advances `pos`, and auto-clears `active` at end.
    // When active, the bake-preview audio path overwrites outL/outR (voice
    // engine output is replaced, not mixed) — a raw dry one-shot to master.
    struct BakePreviewBridge
    {
        std::atomic<uint8_t>  start_req{0};
        std::atomic<uint8_t>  stop_req{0};
        std::atomic<uint8_t>  active{0};
        std::atomic<uint32_t> pos{0};
        Sample                sample{};
        // CRAFT live audition: when craft_chain_active != 0 at start_req time,
        // the audio path runs craft_cfg over the preview block (WYSIWYG with the
        // render). 0 = dry preview (bake-pick / auto-preview-on-load). craft_cfg
        // is written by the UI before start_req (release) and read by the audio
        // thread on start_req (acquire) — same handoff as `sample`.
        std::atomic<uint8_t>     craft_chain_active{0};
        craft::CraftChainConfig  craft_cfg{};
        // Seqlock guarding live edits to craft_cfg while an audition is playing,
        // so knob moves are heard mid-playthrough. UI bumps +1 (odd=writing),
        // writes craft_cfg, bumps +1 (even=stable); audio re-applies on change.
        std::atomic<uint32_t>    craft_cfg_seq{0};
    } bake_preview{};

    // Layer B .bk multisample slot. Filled synchronously by the .bk loader
    // (BkLayer_LoadIntoLayerB) on Perform → Engine → Layer B → Load → pick
    // .bk. The 85 per-slice Sample handles point into the SDRAM .bk PCM
    // buffer (SdBkLayerBBuffer); each carries root_key == its MIDI note so
    // the voice engine plays at native pitch (ratio = 1.0). Voice NoteOn
    // event handler reads `loaded` first; if true and source_layer == 1,
    // picks slice_sample[note - hdr.lo_note] in lieu of the normal sample.
    // Set/cleared from main thread before any potentially-triggering events
    // are queued, so a plain bool is sufficient (no atomics needed).
    struct BkLayerSlot
    {
        bk::BkFileHeader hdr{};
        Sample           slice_sample[bk::kSliceCount]{};
        bool             loaded{false};
    } bk_layer_b{};

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

    // Global "auto-normalize loaded samples" setting (UI → audio). 1 = on.
    // Default on. Read by the voice engine each block to gate per-sample norm_gain.
    std::atomic<uint8_t> settings_normalize_enabled{1};

    AppSharedState()
    {
    }
};
