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
#include "audio_callback.h"
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
float g_sample_rate_hz = 48000.0f;
static uint32_t ctrl_ticks_accum = 0;
static uint32_t ctrl_window_start_ms = 0;
// --- NEW: layered globals (`AudioCallback` in `audio_callback.cpp` links against these definitions) ---
AppState g_app;
Params g_params;
AudioEngine g_audio;
UILogic     g_ui;
UIRender    g_render;
VoiceEngine g_voice;

// --- Event queue (MAIN -> AUDIO) ---
EventQueueSPSC g_evtq;

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
        const Sample& s = g_app.shared.sample.publish.sd_slots[layer];
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
            float loop_attack_ms = static_cast<float>(g_app.engine.adsr.perform_adsr_loop_attack[layer]);
            if(loop_attack_ms < 2.0f)
                loop_attack_ms = 2.0f;
            if(loop_attack_ms > 1000.0f)
                loop_attack_ms = 1000.0f;
            float loop_release_ms = static_cast<float>(g_app.engine.adsr.perform_adsr_loop_release[layer]);
            if(loop_release_ms < 1.0f)
                loop_release_ms = 1.0f;
            if(loop_release_ms > 1000.0f)
                loop_release_ms = 1000.0f;
            t.engine_drive_mode[layer] = g_app.engine.layer.engine_drive_mode[layer] & 1u;
            t.engine_loop_mode[layer] = (g_app.engine.layer.engine_play_mode[layer] != 0);
            t.engine_loop_attack_ms[layer] = loop_attack_ms;
            t.engine_loop_decay_ms[layer] = static_cast<float>(g_app.engine.adsr.perform_adsr_loop_decay[layer]);
            t.engine_loop_sustain_level[layer]
                = static_cast<float>(g_app.engine.adsr.perform_adsr_loop_sustain[layer]) * 0.01f;
            t.engine_loop_release_ms[layer] = loop_release_ms;
            t.engine_loop_crossfade_amount[layer] = g_app.engine.adsr.perform_adsr_loop_crossfade[layer];
            t.engine_loop_crossfade_shape[layer] = g_app.engine.adsr.perform_adsr_loop_crossfade_shape[layer];
            t.perform_keyzone_lo_note[layer] = g_app.engine.keyzone.perform_keyzone_lo_note[layer];
            t.perform_keyzone_hi_note[layer] = g_app.engine.keyzone.perform_keyzone_hi_note[layer];
        }
        g_params.PublishTargets();
    }
    ModMatrix_InitDefaults(g_app.shared.performance.modulation.mod_matrix,
                           g_app.shared.performance.modulation.mod_routes_ui);
    g_app.shared.performance.modulation.mod_route_selected = 0;
    Macros_InitState(g_app.shared.performance.macros.macro_ui);
    Macros_Publish(g_app, g_app.shared.performance.macros.macro_ui);
    g_app.shared.performance.sequencer.seq_running = false;
    g_app.shared.performance.plocks.plock_apply_enabled = false;
    g_app.shared.performance.modulation.lfo_wave.store(0, std::memory_order_relaxed);
    PLocks_InitPattern(g_app.shared.performance.plocks.plock_pattern);
    if(g_app.shared.performance.plocks.plock_apply_enabled)
        PLocks_PublishCurrentStep(g_app.shared.performance.plocks.plocks,
                                  g_app.shared.performance.plocks.plock_pattern);
    g_audio.Init(hw.AudioSampleRate(), hw.AudioBlockSize());
    g_ui.Init(hw);
    g_render.Init(&display, hw);
    g_app.ui.ui_nav.top = 0;
    g_app.ui.ui_nav.stack[0] = UiScreenId::Start;
    g_app.ui.ui_active_screen = UiScreenId::Start;
    hw.midi.StartReceive();
    g_voice.Init(g_sample_rate_hz, hw.AudioBlockSize());
    g_voice.SetModMatrix(&g_app.shared.performance.modulation.mod_matrix);
    g_voice.SetPLocks(&g_app.shared.performance.plocks.plocks);
    g_voice.SetMacros(&g_app.shared.performance.macros.macro_a,
                      &g_app.shared.performance.macros.macro_b,
                      &g_app.shared.performance.macros.macro_sel,
                      &g_app.shared.performance.macros.macro_gen);
    const Sample* bank[2] = {&g_app.shared.sample.publish.sd_slots[0], &g_app.shared.sample.publish.sd_slots[1]};
    g_voice.SetSampleBank(bank, 2);
    g_voice.SetSample(&g_app.shared.sample.publish.sd_slots[0]);
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
              || (g_app.ui.ui_active_screen == UiScreenId::VelocityMod)
              || (g_app.ui.ui_active_screen == UiScreenId::VelocityMod2)
              || (g_app.ui.ui_active_screen == UiScreenId::ModBlockA)
              || (g_app.ui.ui_active_screen == UiScreenId::ModBlockB)
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
