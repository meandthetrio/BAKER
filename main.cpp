#include "daisy_pod.h"
#include "daisy_core.h"
#include "stm32h7xx.h"

// OLED driver (SSD130x family) used in libDaisy examples
#include "dev/oled_ssd130x.h"
#include "hid/midi.h"

// --- NEW: layered headers ---
#include "app_state.h"
#include "params.h"
#include "audio_engine.h"
#include "ui_logic.h"
#include "ui_render.h"
#include "event_queue.h"
#include "voice_engine.h"
#include "keygroups.h"
#include "velocity_layers.h"
#include "mod_matrix.h"
#include "plocks.h"
#include "macros.h"
#include "embedded_sample.h"
#include "embedded_long_sample.h"
#include "sd_sample_pool.h"
#include <cmath>

using namespace daisy;

// --- Hardware ---
static DaisyPod hw;

// --- OLED Types ---
using PodDisplay = OledDisplay<SSD130xI2c128x64Driver>;
static PodDisplay display;

// --- UI timing ---
static uint32_t last_ms = 0;
static int32_t  ctrl_accum_ms = 0;
static float    g_sample_rate_hz = 48000.0f;
static uint32_t ctrl_ticks_accum = 0;
static uint32_t ctrl_window_start_ms = 0;
static constexpr float kMacroSmoothSec = 0.005f;

// --- NEW: layered globals ---
static AppState g_app;
static Params g_params;
static AudioEngine g_audio;
static UILogic     g_ui;
static UIRender    g_render;
static VoiceEngine g_voice;

// --- Event queue (MAIN -> AUDIO) ---
static EventQueueSPSC g_evtq;

static void EnableCycleCounter()
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static inline bool LayerEligibleForNote(uint8_t layer, uint8_t note, uint8_t vel)
{
    (void)vel;
    if(layer >= PerformParamsTargets::kLayerCount)
        return false;

    const PerformParamsTargets& t = g_params.TargetsForUI();
    return KeyzoneContainsNote(t.perform_keyzone_lo_note[layer],
                               t.perform_keyzone_hi_note[layer],
                               note);
}

// --- Audio: passthrough (now via AudioEngine) ---
static void AudioCallback(AudioHandle::InputBuffer  in,
                          AudioHandle::OutputBuffer out,
                          size_t                    size)
{
    const uint32_t start_cycles = DWT->CYCCNT;
    static bool s_rec_active = false;
    static uint8_t s_rec_source = 0;
    static uint8_t s_rec_slot = 0;
    static uint32_t s_rec_pos = 0;
    static constexpr uint32_t kRecLiveWaveStride = 128u;

    const uint8_t ready = g_app.shared.sd_published_ready.load(std::memory_order_acquire);
    const uint32_t pub_gen = g_app.shared.sd_published_gen.load(std::memory_order_acquire);
    const uint32_t applied_gen = g_app.shared.sd_applied_gen.load(std::memory_order_acquire);
    if(ready && pub_gen != applied_gen)
    {
        uint8_t slot = g_app.shared.sd_published_slot.load(std::memory_order_acquire);
        if(slot >= kSdSampleSlots)
            slot = 0;
        g_voice.SetSample(&g_app.shared.sd_slots[slot]);
        g_app.shared.sd_applied_gen.store(pub_gen, std::memory_order_release);
        g_app.shared.sd_current_slot.store(slot, std::memory_order_release);
        g_app.shared.sd_published_ready.store(0, std::memory_order_release);
    }

    const uint8_t edit_ready = g_app.shared.sd_edit_ready.load(std::memory_order_acquire);
    const uint32_t edit_gen = g_app.shared.sd_edit_gen.load(std::memory_order_acquire);
    const uint32_t edit_applied = g_app.shared.sd_edit_applied_gen.load(std::memory_order_acquire);
    if(edit_ready && edit_gen != edit_applied)
    {
        uint8_t slot = g_app.shared.sd_edit_slot.load(std::memory_order_acquire);
        if(slot >= kSdSampleSlots)
            slot = 0;
        g_voice.SetSampleEdit(g_app.shared.sd_edit_pending, &g_app.shared.sd_slots[slot]);
        g_app.shared.sd_edit_applied_gen.store(edit_gen, std::memory_order_release);
        g_app.shared.sd_edit_ready.store(0, std::memory_order_release);
    }

    if(g_app.shared.rec_start_req.exchange(0, std::memory_order_acq_rel) != 0)
    {
        s_rec_source = g_app.shared.rec_source_sel.load(std::memory_order_acquire) & 1u;
        s_rec_slot = g_app.shared.rec_slot_pending.load(std::memory_order_acquire) & 1u;
        s_rec_pos = 0;
        s_rec_active = true;
        g_app.shared.rec_pos.store(0, std::memory_order_release);
        g_app.shared.rec_length.store(0, std::memory_order_release);
        g_app.shared.rec_active.store(1, std::memory_order_release);
        g_app.shared.rec_live_last_col = -1;
        for(int i = 0; i < 128; ++i)
        {
            g_app.shared.rec_live_min[i] = 0;
            g_app.shared.rec_live_max[i] = 0;
        }
    }

    if(g_app.shared.rec_stop_req.exchange(0, std::memory_order_acq_rel) != 0 && s_rec_active)
    {
        s_rec_active = false;
        g_app.shared.rec_active.store(0, std::memory_order_release);
        g_app.shared.rec_length.store(s_rec_pos, std::memory_order_release);
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
                g_app.shared.rec_active.store(0, std::memory_order_release);
                g_app.shared.rec_length.store(s_rec_pos, std::memory_order_release);
                break;
            }

            const float src = (s_rec_source == static_cast<uint8_t>(RecordInputSource::Mic))
                                  ? in[1][i]
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
                if(col != g_app.shared.rec_live_last_col)
                {
                    g_app.shared.rec_live_min[col] = s16;
                    g_app.shared.rec_live_max[col] = s16;
                    g_app.shared.rec_live_last_col = static_cast<int16_t>(col);
                }
                else
                {
                    if(s16 < g_app.shared.rec_live_min[col])
                        g_app.shared.rec_live_min[col] = s16;
                    if(s16 > g_app.shared.rec_live_max[col])
                        g_app.shared.rec_live_max[col] = s16;
                }
            }

            ++s_rec_pos;
        }
        g_app.shared.rec_pos.store(s_rec_pos, std::memory_order_release);
        g_app.shared.rec_live_gen.fetch_add(1, std::memory_order_acq_rel);
    }

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
    const uint32_t macro_gen = g_app.shared.macro_gen.load(std::memory_order_acquire);
    if(macro_gen != s_macro_gen_seen)
    {
        const uint8_t sel = g_app.shared.macro_sel.load(std::memory_order_acquire) & 1u;
        s_active_macros = (sel == 0) ? g_app.shared.macro_a : g_app.shared.macro_b;
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
    g_voice.SetLfoWave(g_app.shared.lfo_wave.load(std::memory_order_relaxed));
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
            if(t_boost < 0.0f) t_boost = 0.0f;
            if(t_boost > 1.0f) t_boost = 1.0f;
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
        g_voice.SetLoopCrossfadeAmount(layer,
                                       g_params.current.engine_loop_crossfade_amount[layer]);
        g_voice.SetLoopCrossfadeShape(layer,
                                      g_params.current.engine_loop_crossfade_shape[layer]);
    }
    g_voice.ProcessEvents(g_evtq);
    g_voice.RenderBlock(out[0], out[1], size);

    // FX / master level after voices (in-place).
    PerformParamsCurrent fx_params = g_params.current;
    float drive = fx_params.sat_drive;
    Macros_Apply(s_macro_smoothed, nullptr, nullptr, nullptr, nullptr, &drive);
    fx_params.sat_drive = drive;
    g_audio.ProcessBlock(out[0], out[1], out[0], out[1], size, fx_params);

    const bool monitor_on = (g_app.shared.rec_monitor_enable.load(std::memory_order_acquire) != 0);
    if(monitor_on)
    {
        const uint8_t src = g_app.shared.rec_source_sel.load(std::memory_order_acquire) & 1u;
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

static void InitOled()
{
    PodDisplay::Config disp_cfg;

    // Configure SSD130x I2C transport (this matches this libDaisy header)
    auto& tcfg = disp_cfg.driver_config.transport_config;

    tcfg.i2c_address       = 0x3C; // common SSD1306/SSD1309 address
    tcfg.i2c_config.periph = I2CHandle::Config::Peripheral::I2C_1;
    tcfg.i2c_config.mode   = I2CHandle::Config::Mode::I2C_MASTER;
    tcfg.i2c_config.speed  = I2CHandle::Config::Speed::I2C_400KHZ;

    // Daisy Pod wiring (per hardware map): SCL = D11, SDA = D12
    tcfg.i2c_config.pin_config.scl = hw.seed.GetPin(11); // D11
    tcfg.i2c_config.pin_config.sda = hw.seed.GetPin(12); // D12

    display.Init(disp_cfg);

    display.Fill(false);
    display.Update();
}

static inline void PushAudioEventFromMain(const Event& evt)
{
    if(g_evtq.Push(evt))
        g_app.diag.events_pushed.fetch_add(1, std::memory_order_relaxed);
    else
    {
        g_app.diag.queue_overflows.fetch_add(1, std::memory_order_relaxed);
        g_app.ui.ui_dirty = true;
    }
}

static void HandleMidiNoteOn(const NoteOnEvent& note_on)
{
    if(note_on.velocity == 0)
    {
        PushAudioEventFromMain(Event::NoteOffEvent(note_on.note));
        return;
    }

    const uint8_t vel_layer = Velocity_SelectLayer(note_on.velocity);
    g_app.diag.last_vel_layer.store(vel_layer, std::memory_order_relaxed);
    g_app.diag.last_velocity.store(note_on.velocity, std::memory_order_relaxed);

    for(uint8_t layer = 0; layer < 2; ++layer)
    {
        const Sample& s = g_app.shared.sd_slots[layer];
        if(s.pcm == nullptr || s.length == 0)
            continue;
        if(!LayerEligibleForNote(layer, note_on.note, note_on.velocity))
            continue;

        Event evt = Event::NoteOnEvent(note_on.note, note_on.velocity);
        evt.value = static_cast<uint32_t>(layer)
                    | (static_cast<uint32_t>(vel_layer) << 8);
        g_app.diag.last_sample_index.store(layer, std::memory_order_relaxed);
        g_app.ui.ui_dirty = true;
        PushAudioEventFromMain(evt);
    }
}

static void HandleMidiNoteOff(const NoteOffEvent& note_off)
{
    PushAudioEventFromMain(Event::NoteOffEvent(note_off.note));
}

static bool DrainMidiInput(uint32_t now_ms)
{
    bool midi_activity = false;
    hw.midi.Listen();
    while(hw.midi.HasEvents())
    {
        MidiEvent msg = hw.midi.PopEvent();
        g_app.diag.midi_rx_count.fetch_add(1, std::memory_order_relaxed);
        midi_activity = true;

        if(msg.type == MidiMessageType::NoteOn)
            HandleMidiNoteOn(msg.AsNoteOn());
        else if(msg.type == MidiMessageType::NoteOff)
            HandleMidiNoteOff(msg.AsNoteOff());
    }
    if(midi_activity)
        g_app.ui.last_input_ms = now_ms;
    return midi_activity;
}

static void RunControlTicks(uint32_t now_ms)
{
    const int kMaxCtrlTicksPerLoop = 3;
    int ticks_to_run = ctrl_accum_ms;
    if(ticks_to_run > kMaxCtrlTicksPerLoop)
        ticks_to_run = kMaxCtrlTicksPerLoop;

    // Control tick owns hardware input scanning.
    for(int i = 0; i < ticks_to_run; ++i)
    {
        // Predictable scan cadence so edges can't be "missed" between reads.
        g_ui.ControlTick(hw, g_app, g_params, g_evtq);

        ctrl_ticks_accum++;
        if(ctrl_window_start_ms == 0)
            ctrl_window_start_ms = now_ms;

        if((now_ms - ctrl_window_start_ms) >= 1000)
        {
            g_app.ui.ctrl_hz = ctrl_ticks_accum;
            ctrl_ticks_accum = 0;
            ctrl_window_start_ms = now_ms;
            g_app.ui.ui_dirty = true;
        }

        ctrl_accum_ms -= 1;
    }
}

int main(void)
{
    hw.Init();
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    hw.SetAudioBlockSize(48);
    g_sample_rate_hz = hw.AudioSampleRate();

    EnableCycleCounter();

    InitOled();

    // --- NEW: init layered stubs ---
    g_params.Init();
    {
        // Baker boot defaults: LFO disabled unless explicitly enabled later.
        auto& t = g_params.EditTargets();
        t.lfo_rate_hz = 0.0f;
        t.lfo_depth = 0.0f;
        t.engine_filter_cutoff_hz[0] = 20000.0f;
        t.engine_filter_cutoff_hz[1] = 20000.0f;
        t.engine_filter_resonance[0] = 0.0f;
        t.engine_filter_resonance[1] = 0.0f;
        for(uint8_t layer = 0; layer < PerformParamsTargets::kLayerCount; ++layer)
        {
            float loop_attack_ms = static_cast<float>(g_app.engine.perform_adsr_loop_attack[layer]);
            if(loop_attack_ms < 1.0f)
                loop_attack_ms = 1.0f;
            if(loop_attack_ms > 1000.0f)
                loop_attack_ms = 1000.0f;
            float loop_release_ms = static_cast<float>(g_app.engine.perform_adsr_loop_release[layer]);
            if(loop_release_ms < 1.0f)
                loop_release_ms = 1.0f;
            if(loop_release_ms > 1000.0f)
                loop_release_ms = 1000.0f;
            t.engine_drive_mode[layer] = g_app.engine.engine_drive_mode[layer] & 1u;
            t.engine_loop_mode[layer] = (g_app.engine.engine_play_mode[layer] != 0);
            t.engine_loop_attack_ms[layer] = loop_attack_ms;
            t.engine_loop_decay_ms[layer] = static_cast<float>(g_app.engine.perform_adsr_loop_decay[layer]);
            t.engine_loop_sustain_level[layer]
                = static_cast<float>(g_app.engine.perform_adsr_loop_sustain[layer]) * 0.01f;
            t.engine_loop_release_ms[layer] = loop_release_ms;
            t.engine_loop_crossfade_amount[layer] = g_app.engine.perform_adsr_loop_crossfade[layer];
            t.engine_loop_crossfade_shape[layer] = g_app.engine.perform_adsr_loop_crossfade_shape[layer];
            t.perform_keyzone_lo_note[layer] = g_app.engine.perform_keyzone_lo_note[layer];
            t.perform_keyzone_hi_note[layer] = g_app.engine.perform_keyzone_hi_note[layer];
        }
        g_params.PublishTargets();
    }
    ModMatrix_InitDefaults(g_app.shared.mod_matrix, g_app.shared.mod_routes_ui);
    g_app.shared.mod_route_selected = 0;
    Macros_InitState(g_app.shared.macro_ui);
    Macros_Publish(g_app, g_app.shared.macro_ui);
    g_app.shared.seq_running = false;
    g_app.shared.plock_apply_enabled = false;
    g_app.shared.lfo_wave.store(0, std::memory_order_relaxed);
    PLocks_InitPattern(g_app.shared.plock_pattern);
    if(g_app.shared.plock_apply_enabled)
        PLocks_PublishCurrentStep(g_app.shared.plocks, g_app.shared.plock_pattern);
    g_audio.Init(hw.AudioSampleRate(), hw.AudioBlockSize());
    g_ui.Init(hw);
    g_render.Init(&display, hw);
    g_app.ui.ui_nav.top = 0;
    g_app.ui.ui_nav.stack[0] = UiScreenId::Start;
    g_app.ui.ui_active_screen = UiScreenId::Start;
    hw.midi.StartReceive();
    g_voice.Init(g_sample_rate_hz, hw.AudioBlockSize());
    g_voice.SetModMatrix(&g_app.shared.mod_matrix);
    g_voice.SetPLocks(&g_app.shared.plocks);
    g_voice.SetMacros(&g_app.shared.macro_a, &g_app.shared.macro_b, &g_app.shared.macro_sel, &g_app.shared.macro_gen);
    const Sample* bank[2] = {&g_app.shared.sd_slots[0], &g_app.shared.sd_slots[1]};
    g_voice.SetSampleBank(bank, 2);
    g_voice.SetSample(&g_app.shared.sd_slots[0]);
    g_voice.BindDebug(&g_app.diag.events_popped,
                      &g_app.diag.voices_active,
                      &g_app.diag.voice_steals,
                      &g_app.diag.last_voice_packed,
                      &g_app.diag.last_stolen_voice_index,
                      &g_app.diag.last_stolen_start_id,
                      &g_app.diag.last_new_start_id,
                      &g_app.diag.clip_count,
                      &g_app.diag.fadeouts_started,
                      &g_app.diag.last_lfo,
                      &g_app.diag.last_env,
                      &g_app.diag.lfo_rate_dbg,
                      &g_app.diag.lfo_depth_dbg,
                      &g_app.diag.playhead_frame[0],
                      &g_app.diag.playhead_frame[1],
                      &g_app.diag.playhead_active[0],
                      &g_app.diag.playhead_active[1]);

    const float block_seconds
        = static_cast<float>(hw.AudioBlockSize()) / hw.AudioSampleRate();
    uint32_t cpu_hz = SystemCoreClock;
    if(cpu_hz == 0)
        cpu_hz = 480000000;
    const uint32_t budget_cycles = static_cast<uint32_t>(cpu_hz * block_seconds);
    g_app.diag.audio_budget_cycles.store(budget_cycles, std::memory_order_relaxed);

    hw.StartAudio(AudioCallback);

    last_ms = System::GetNow();
    uint32_t last_peak_reset_ms = last_ms;

    while(1)
    {
        uint32_t now_ms = System::GetNow();

        const uint32_t dt_ms = now_ms - last_ms;
        last_ms = now_ms;
        ctrl_accum_ms += static_cast<int32_t>(dt_ms);
        if(ctrl_accum_ms < 0)
            ctrl_accum_ms = 0;
        if(ctrl_accum_ms > 20)
            ctrl_accum_ms = 20;

        const bool midi_activity = DrainMidiInput(now_ms);

        const bool midi_busy = midi_activity || hw.midi.HasEvents();
        RunControlTicks(now_ms);

        const uint32_t loop_mode = g_app.diag.loop_mode.load(std::memory_order_relaxed);
        g_voice.SetLoopMode(loop_mode == 0 ? LoopMode::Forward : LoopMode::PingPong);

        if((now_ms - last_peak_reset_ms) >= 100)
        {
            g_app.diag.audio_cycles_peak.store(0, std::memory_order_relaxed);
            last_peak_reset_ms = now_ms;
        }

        // UI tick owns UI state + drawing; never polls hardware directly.
        g_ui.UiTick(g_app, g_params, g_evtq, now_ms);
        // LED2 indicator:
        // - SD BROWSE: solid GREEN.
        // - RECORD > REVIEW: solid GREEN (POD2 preview available).
        // - PERFORM A/B pages: solid BLUE.
        // - otherwise off.
        const bool sd_browse_active = (g_app.ui.ui_active_screen == UiScreenId::SdBrowse);
        const bool record_review_active = (g_app.ui.ui_active_screen == UiScreenId::Record)
                                          && (g_app.recording.record_state == RecordUiState::Review);
        const bool perform_ab_active
            = (g_app.ui.ui_active_screen == UiScreenId::PerformEngine)
              || (g_app.ui.ui_active_screen == UiScreenId::PerformKeyzone)
              || (g_app.ui.ui_active_screen == UiScreenId::PerformAdsr)
              || (g_app.ui.ui_active_screen == UiScreenId::PerformEmphasis);

        if(sd_browse_active || record_review_active)
        {
            hw.led2.Set(0.0f, 1.0f, 0.0f);
        }
        else if(perform_ab_active)
        {
            hw.led2.Set(0.0f, 0.0f, 1.0f);
        }
        else
        {
            hw.led2.Set(0.0f, 0.0f, 0.0f);
        }

        hw.UpdateLeds();

        g_render.Tick(g_app, g_params);
        g_render.TickOledTransfer(now_ms, midi_busy);
    }
}
