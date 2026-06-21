#pragma once

#include <cstdint>

#include "keygroups.h"
#include "sample_edit.h"
#include "storage_limits.h"
#include "express_state.h"

// App-owned engine/editor state and perform editing state.
struct AppEngineState
{
    struct LayerState
    {
        // Per-layer sample/editor metadata surfaced to the UI.
        int8_t  engine_tune_semitones[2] = {0, 0};
        int8_t  engine_tune_cents[2] = {0, 0};
        int16_t engine_gain_db[2] = {0, 0};
        uint8_t engine_drive_mode[2] = {0u, 0u};
        uint8_t engine_filter_mode[2] = {0u, 0u}; // 0=LP, 1=HP, 2=BP
        uint8_t engine_play_mode[2] = {1, 0};
        char    engine_sample_path[2][kSdPathMax] = {};
        char    engine_sample_name[2][kSdNameMax] = {};
        uint8_t engine_load_target_layer = 0xFFu;
        bool    engine_load_from_perform = false;
        uint32_t engine_seen_applied_gen = 0;
        uint32_t engine_header_invert_until_ms = 0;
    } layer{};

    struct PerformNavigationState
    {
        // PERFORM screen editor state and submenu-local cursors.
        uint8_t perform_menu_index = 0;
        uint8_t perform_layer = 0;
        uint8_t perform_engine_row = 0;
        uint8_t perform_express_focus = 0;
    } perform_nav{};

    struct PerformWaveEditState
    {
        uint8_t perform_wave_edit_cursor = 0;
        SampleEdit perform_wave_edit_entry[kSdSampleSlots]{};
        bool    perform_wave_edit_has_entry = false;
    } wave_edit{};

    // Keytrack volume tilt range. The UI rectangle tilts from flat (0) to a
    // diagonal where one side is fully lowered at +/- this magnitude.
    static constexpr int8_t kPerformKeytrackTiltMax = 12;
    // Keytrack amount range: 0..this magnitude in dB, in 1 dB steps. The curve
    // pivots at the mid note (0 dB), reaching +amount at one keyboard extreme and
    // -amount at the other, so amount represents a +/- swing.
    static constexpr int8_t kPerformKeytrackAmountMaxDb = 12;
    // Keytrack mid (pivot) note: the 0 dB point the curve bends around. Editable
    // C3..C6; default F#4. The curve tents from here to +/-amount at C1 / C8.
    static constexpr uint8_t kPerformKeytrackMidNoteMin     = 48; // C3
    static constexpr uint8_t kPerformKeytrackMidNoteMax     = 84; // C6
    static constexpr uint8_t kPerformKeytrackMidNoteDefault = 66; // F#4

    struct PerformKeyzoneState
    {
        uint8_t perform_keyzone_lo_note[2] = {12u, 12u};   // C0 — full range default
        uint8_t perform_keyzone_hi_note[2] = {108u, 108u}; // C8 — full range default
        bool    perform_keyzone_is_split = false;
        // Keytrack volume tilt (UI-only for now; not yet applied to audio).
        // Bipolar -kPerformKeytrackTiltMax..+max. 0 = flat (full volume across
        // the keyboard). >0 lowers the high-note (right) side; <0 lowers the
        // low-note (left) side. Edited on the PerformKeytrack subscreen.
        int8_t  perform_keytrack_tilt = 0;
        // Keytrack amount: the +/- dB swing each extreme of the tilt diagonal
        // reaches at full tilt (0..12). The diagonal pivots at the mid note
        // (0 dB): one side boosts +amount, the other cuts -amount.
        int8_t  perform_keytrack_amount_db = 0;
        // Keytrack mid (pivot) note where the curve crosses 0 dB. C3..C6.
        uint8_t perform_keytrack_mid_note = kPerformKeytrackMidNoteDefault;
    } keyzone{};

    // Velocity-mod / mod-block UI (ported from oled_ui_sim). Audio routing TBD.
    // target_idx indexes into the shared 8-entry list (see ui_screen_perform_velmod.cpp):
    //   0=----, 1=volume, 2=attack, 3=sustain, 4=release, 5=rev send, 6=delay send, 7=sat send
    // amount is bipolar -10..+10 for modifying targets, unipolar 0..+10 for send
    // targets (5/6/7). 0 always gates the lane. Each step = 10% of target's range.
    // shape: 0=knee (linear ramp threshold→127), 1=gate (binary at threshold).
    struct PerformVelModState
    {
        uint8_t threshold[2]   = {0u, 0u};   // 0..127 (vel) or MIDI note 24..108 (note)
        int8_t  amount[2]      = {0, 0};     // -10..+10 (clamped per target type)
        uint8_t target_idx[2]  = {0u, 0u};   // index into kVelModTargetList
        uint8_t shape[2]       = {1u, 1u};   // 0=knee, 1=gate (default gate)
        uint8_t source[2]      = {0u, 0u};   // 0=>vel 1=<vel 2=>note 3=<note
        bool    threshold_linked = false;    // when true, threshold edits apply to both lanes
    } velmod{};

    struct PerformAdsrState
    {
        uint8_t perform_adsr_row[2] = {1u, 1u};
        bool    perform_adsr_type_focus = false;
        bool    perform_adsr_wave_focus = false;
        uint8_t perform_adsr_stage_focus = 0;
        uint16_t perform_adsr_loop_attack[2] = {5u, 5u};
        uint8_t perform_adsr_loop_decay[2] = {20u, 20u};
        uint8_t perform_adsr_loop_sustain[2] = {100u, 100u};
        uint16_t perform_adsr_loop_release[2] = {50u, 50u};
        // Attack/Release curve per layer: 0 = exponential (default), 1 = logarithmic.
        // Toggled by RShift + REnc while the A or R stage is focused.
        uint8_t  perform_adsr_attack_curve[2] = {0u, 0u};
        uint8_t  perform_adsr_release_curve[2] = {0u, 0u};
        float   perform_adsr_loop_crossfade[2] = {0.0625f, 0.0625f};
        float   perform_adsr_loop_crossfade_shape[2] = {0.0f, 0.0f};
        // Set true by the loader once a slot's loop crossfade has been baked into
        // its PCM (see SdBakedBuffer). Flows to the engine so the runtime seam /
        // boundary fade are suppressed for that layer. Re-evaluated on every load.
        bool    perform_adsr_loop_seam_baked[2] = {false, false};
        uint8_t perform_adsr_env_a_x[2] = {13u, 13u};
        uint8_t perform_adsr_env_d_x[2] = {38u, 38u};
        uint8_t perform_adsr_env_r_x[2] = {89u, 89u};
        uint8_t perform_adsr_env_s_level[2] = {50u, 50u};
    } adsr{};

    struct PerformExpressState
    {
        static constexpr uint8_t kLayerCount = 2;
        static constexpr uint8_t kRowCount = 3;

        // 0=CUTOFF, 1=DRIVE, 2=RESONANCE, 3=ATTACK, 4=SUSTAIN, 5=RELEASE,
        // 6=REVERB, 7=POLYPORTO, 8=NONE.
        uint8_t  target[kLayerCount][kRowCount]
            = {{kExpressNone, kExpressNone, kExpressNone},
               {kExpressNone, kExpressNone, kExpressNone}};
        uint16_t min_value[kLayerCount][kRowCount] = {{0u, 0u, 0u}, {0u, 0u, 0u}};
        uint16_t max_value[kLayerCount][kRowCount] = {{0u, 0u, 0u}, {0u, 0u, 0u}};
        uint8_t  poly_porto_voice_limit[kLayerCount]
            = {kExpressPolyPortoVoicesDefault, kExpressPolyPortoVoicesDefault};
        uint16_t poly_porto_slide_ms[kLayerCount]
            = {kExpressPolyPortoSlideDefaultMs, kExpressPolyPortoSlideDefaultMs};
        uint8_t  poly_porto_source_range_semitones[kLayerCount]
            = {kExpressPolyPortoRangeDefaultSemitones, kExpressPolyPortoRangeDefaultSemitones};
        uint8_t  poly_porto_source_mode[kLayerCount]
            = {kExpressPolyPortoSourceClosest, kExpressPolyPortoSourceClosest};
        uint16_t poly_porto_release_ms[kLayerCount]
            = {kExpressPolyPortoReleaseDefaultMs, kExpressPolyPortoReleaseDefaultMs};
        bool     perform_express_detail_active = false;
        uint8_t  perform_express_detail_field = 0;
    } express{};

    struct PerformProcessState
    {
        uint8_t perform_process_fx_cursor = 0;
        uint8_t perform_process_fx_order[4] = {0, 2, 3, 1};
        uint8_t perform_process_main_cursor = 2;
        uint16_t perform_process_vol_pct[2] = {100u, 100u};
        bool    perform_process_vol_muted[2] = {false, false};
        float   perform_process_vol_unmuted_level[2] = {1.0f, 1.0f};
        bool    perform_process_detail_active = false;
        bool    perform_process_eq_graph_active = false;
        uint8_t perform_process_detail_param[4] = {0, 0, 0, 0};
        uint8_t fx_field_cursor = 0;
        uint8_t mod_field_cursor = 0;
    } process{};
};
