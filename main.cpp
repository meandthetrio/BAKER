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
#include "express_state.h"
#include "embedded_sample.h"
#include "embedded_long_sample.h"
#include "sd_sample_pool.h"
#include "audio_callback.h"
#include "mem_regions.h"
#include <cmath>
#include <cstring>

using namespace daisy;

// --- Hardware ---
static DaisyPod hw;
static MidiUsbHandler usb_midi;

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

static inline uint8_t RecordRenderPreviewMidiNote()
{
    const int note = 60 + static_cast<int>(g_app.ui.record_render_note_offset);
    if(note < 0)
        return 0u;
    if(note > 127)
        return 127u;
    return static_cast<uint8_t>(note);
}

static inline bool RecordRenderPreviewAvailable()
{
    const uint8_t note = RecordRenderPreviewMidiNote();
    for(uint8_t layer = 0; layer < 2u; ++layer)
    {
        const Sample& s = g_app.shared.sample.publish.sd_slots[layer];
        if(s.pcm == nullptr || s.length == 0)
            continue;
        if(LayerEligibleForNote(layer, note, 120u))
            return true;
    }
    return false;
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
    // Engine Trim screen ignores incoming MIDI notes (window-audition only).
    if(g_app.ui.ui_midi_gate_active)
        return;

    if(note_on.velocity == 0)
    {
        PushAudioEventFromMain(Event::NoteOffEvent(note_on.note));
        return;
    }

    const uint8_t vel_layer = Velocity_SelectLayer(note_on.velocity);
    g_app.diag.last_vel_layer.store(vel_layer, std::memory_order_relaxed);
    g_app.diag.last_velocity.store(note_on.velocity, std::memory_order_relaxed);

    // Seam-edit screen: force monophonic audition of the edited layer only. Cut any
    // sounding note first (mono / last-note priority), then play just that layer.
    const bool seam_audition = g_app.ui.ui_seam_audition_active;
    const uint8_t seam_layer = g_app.ui.ui_seam_audition_layer & 1u;
    if(seam_audition)
        PushAudioEventFromMain(Event::AllNotesOffEvent());

    for(uint8_t layer = 0; layer < 2; ++layer)
    {
        if(seam_audition && layer != seam_layer)
            continue;
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
    if(g_app.ui.ui_midi_gate_active)
        return;

    PushAudioEventFromMain(Event::NoteOffEvent(note_off.note));
}

static void HandleMidiControlChange(const ControlChangeEvent& cc)
{
    if(cc.control_number != 1u)
        return;

    uint8_t value = cc.value;
    if(value > 127u)
        value = 127u;
    g_app.shared.performance.express.midi_mod_value.store(value, std::memory_order_release);
    g_app.shared.performance.express.midi_mod_seen.store(1u, std::memory_order_release);
}

template <typename TMidiHandler>
static bool DrainMidiHandlerInput(TMidiHandler& midi, uint32_t now_ms)
{
    bool midi_activity = false;
    midi.Listen();
    while(midi.HasEvents())
    {
        MidiEvent msg = midi.PopEvent();
        g_app.diag.midi_rx_count.fetch_add(1, std::memory_order_relaxed);
        midi_activity = true;

        if(msg.type == MidiMessageType::NoteOn)
            HandleMidiNoteOn(msg.AsNoteOn());
        else if(msg.type == MidiMessageType::NoteOff)
            HandleMidiNoteOff(msg.AsNoteOff());
        else if(msg.type == MidiMessageType::ControlChange)
            HandleMidiControlChange(msg.AsControlChange());
    }
    if(midi_activity)
        g_app.ui.last_input_ms = now_ms;
    return midi_activity;
}

static bool DrainMidiInput(uint32_t now_ms)
{
    return DrainMidiHandlerInput(hw.midi, now_ms);
}

static bool DrainUsbMidiInput(uint32_t now_ms)
{
    return DrainMidiHandlerInput(usb_midi, now_ms);
}

static bool DoubleFlashOn(uint32_t now_ms, uint32_t cycle_ms)
{
    const uint32_t phase = now_ms % cycle_ms;
    return (phase < 90u) || (phase >= 170u && phase < 260u);
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

// Custom heap backing for newlib's malloc/operator new. The default heap
// section in the project linker script lives in AXI SRAM (left over after
// .data + .bss), which is far too small for the signalsmith-stretch
// std::vector allocations inside presetCheaper/presetDefault (~100-200 KB).
// Heap exhaustion under -fno-exceptions silently hardfaults rather than
// returning a clean error — observed as the bake screen freezing on the
// "dbg: pre-configure" diagnostic label.
//
// Override _sbrk to allocate from an 8 MB SDRAM buffer. newlib's _sbrk is
// a weak default and our definition wins at link time. ALL heap allocations
// in the project go to SDRAM after this — fine because we have 64 MB SDRAM
// and almost no heap pressure outside PSOLA. SDRAM access is slightly slower
// than AXI SRAM but the D-cache absorbs the difference for hot loops.
//
// Notes:
// - Heap grows in one direction only; no free-list. signalsmith allocates
//   once at presetCheaper time and reuses; this is fine for our use case.
// - .sdram_bss is NOLOAD: contents are undefined at boot. Allocators don't
//   require zeroed memory (calloc would, but malloc/new don't), so this is
//   safe.
// - Static constructors that allocate would hit this _sbrk before main()
//   runs hw.Init() — but the SDRAM controller is initialized by the H7
//   reset handler (libDaisy core/), before C++ static init, so writes are
//   valid by then.
static constexpr size_t kSdramHeapSize = 8u * 1024u * 1024u; // 8 MB
ADSR2_SECTION(".sdram_bss") __attribute__((aligned(8))) static uint8_t s_sdram_heap[kSdramHeapSize];
static size_t s_sdram_heap_used = 0;

extern "C" void* _sbrk(intptr_t incr)
{
    const size_t requested = (incr > 0) ? static_cast<size_t>(incr) : 0u;
    if((s_sdram_heap_used + requested) > sizeof(s_sdram_heap))
    {
        // Out of heap. newlib expects (void*)-1 on failure; malloc then
        // returns nullptr to the caller. Under -fno-exceptions, that's an
        // immediate hardfault inside std::vector — but at least we're not
        // silently writing past the buffer.
        return reinterpret_cast<void*>(-1);
    }
    void* prev = &s_sdram_heap[s_sdram_heap_used];
    s_sdram_heap_used += requested;
    return prev;
}

// Audio start/stop wrappers — `hw` is static to this TU so consumers (e.g.
// the bake worker that needs to suspend the audio interrupt during PSOLA)
// go through these. Trivial, but they keep the static linkage tight.
extern "C" void Pod_StopAudio()
{
    hw.StopAudio();
}

extern "C" void Pod_StartAudio()
{
    hw.StartAudio(AudioCallback);
}

// Synchronous full-frame OLED flush for callers (notably the bake worker)
// that block the main loop and still want the OLED to reflect their
// progress state. Renders the active screen and drains all 8 page transfers
// in one call (~22 ms blocking on I2C). Bypasses both the 60 Hz Tick gate
// and the 2 ms per-page transfer gate — those gates are only correct for
// the main loop's regular cadence, not for a blocking task pushing updates
// at its own rate.
extern "C" void Pod_ForceDisplayRefresh()
{
    g_app.ui.ui_dirty = true;
    g_render.RenderNowAndFlushFullFrame(g_app, g_params);
}

// MIDI panic: push an AllNotesOff event to the audio engine, which stops
// every active voice. Used by the SHIFT-menu Button1 binding so stuck
// notes (lost NoteOff, dropped events, controller weirdness) can be
// cleared without rebooting.
extern "C" void Pod_MidiPanic()
{
    PushAudioEventFromMain(Event::AllNotesOffEvent());
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
            for(uint8_t row = 0; row < kExpressRowCount; ++row)
            {
                t.express_target[layer][row] = g_app.engine.express.target[layer][row];
                t.express_min_value[layer][row] = g_app.engine.express.min_value[layer][row];
                t.express_max_value[layer][row] = g_app.engine.express.max_value[layer][row];
                ExpressClampRow(t.express_target[layer][row],
                                t.express_min_value[layer][row],
                                t.express_max_value[layer][row]);
            }
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
    g_audio.BindDiagnostics(&g_app.diag);
    g_ui.Init(hw);
    g_render.Init(&display, hw);
    g_app.ui.ui_nav.top = 0;
    g_app.ui.ui_nav.stack[0] = UiScreenId::Start;
    g_app.ui.ui_active_screen = UiScreenId::Start;
    MidiUsbHandler::Config usb_midi_config;
    usb_midi_config.transport_config.periph = MidiUsbTransport::Config::EXTERNAL;
    usb_midi.Init(usb_midi_config);
    hw.midi.StartReceive();
    usb_midi.StartReceive();
    g_voice.Init(g_sample_rate_hz, hw.AudioBlockSize());
    g_voice.BindDiagnostics(&g_app.diag);
    g_voice.BindSharedState(&g_app.shared);
    g_voice.SetModMatrix(&g_app.shared.performance.modulation.mod_matrix);
    g_voice.SetPLocks(&g_app.shared.performance.plocks.plocks);
    g_voice.SetMacros(&g_app.shared.performance.macros.macro_a,
                      &g_app.shared.performance.macros.macro_b,
                      &g_app.shared.performance.macros.macro_sel,
                      &g_app.shared.performance.macros.macro_gen);
    const Sample* bank[kRecordPreviewSampleIndex + 1] = {
        &g_app.shared.sample.publish.sd_slots[0],
        &g_app.shared.sample.publish.sd_slots[1],
        &g_app.shared.recording.rec_sample,
    };
    g_voice.SetSampleBank(bank, kRecordPreviewSampleIndex + 1);
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

    while(1)
    {
        uint32_t now_ms = System::GetNow();
        static bool prev_wav_load_busy = false;
        static bool prev_sd_manage_trim_save_busy = false;
        static uint32_t load_success_flash_until_ms = 0;

        const uint32_t dt_ms = now_ms - last_ms;
        last_ms = now_ms;
        ctrl_accum_ms += static_cast<int32_t>(dt_ms);
        if(ctrl_accum_ms < 0)
            ctrl_accum_ms = 0;
        if(ctrl_accum_ms > 20)
            ctrl_accum_ms = 20;

        const bool midi_activity = DrainMidiInput(now_ms);
        const bool usb_midi_activity = DrainUsbMidiInput(now_ms);

        const bool midi_busy = midi_activity || usb_midi_activity || hw.midi.HasEvents()
                               || usb_midi.HasEvents();
        RunControlTicks(now_ms);

        const uint32_t loop_mode = g_app.diag.loop_mode.load(std::memory_order_relaxed);
        g_voice.SetLoopMode(loop_mode == 0 ? LoopMode::Forward : LoopMode::PingPong);

        // UI tick owns UI state + drawing; never polls hardware directly.
        g_ui.UiTick(g_app, g_params, g_evtq, now_ms);
        // LED2 indicator:
        // - SD BROWSE: solid GREEN.
        // - RECORD > REVIEW: solid GREEN (POD2 preview available).
        // - RECORD > RENDER: solid GREEN when render preview is available.
        // - PERFORM A/B pages: solid BLUE.
        // - otherwise off.
        const bool sd_browse_active = (g_app.ui.ui_active_screen == UiScreenId::SdBrowse);
        const bool record_review_active = (g_app.ui.ui_active_screen == UiScreenId::Record)
                                          && (g_app.recording.record_state == RecordUiState::Review);
        const bool record_render_review_active
            = (g_app.ui.ui_active_screen == UiScreenId::RecordRenderReview)
              && (g_app.shared.recording.rec_sample.pcm != nullptr)
              && (g_app.shared.recording.rec_sample.length > 0u);
        const bool record_render_preview_active
            = (g_app.ui.ui_active_screen == UiScreenId::RecordRenderMenu)
              && RecordRenderPreviewAvailable();
        const bool perform_ab_active
            = (g_app.ui.ui_active_screen == UiScreenId::PerformEngine)
              || (g_app.ui.ui_active_screen == UiScreenId::PerformAdsr)
              || (g_app.ui.ui_active_screen == UiScreenId::PerformEmphasis)
              || (g_app.ui.ui_active_screen == UiScreenId::PerformExpress);
        // Engine Trim (engine-slot) screen: LED2 green when idle, red while the
        // one-shot window audition is playing.
        const bool engine_trim_active
            = (g_app.ui.ui_active_screen == UiScreenId::PerformWaveEdit)
              && (g_app.ui.wave_edit_source == WaveEditSource::PerformSlot);
        const bool win_preview_playing
            = (g_app.shared.recording.win_preview_active.load(std::memory_order_acquire) != 0u);
        const bool firmware_pairing_hold_active
            = g_app.ui.shift_menu_firmware_update_active && g_app.ui.shift_menu_bootloader_armed;
        const bool boot_splash_active = g_render.IsBootSplashActive();
        static constexpr uint32_t kRecordCountdownMs = 4000u;
        const bool record_countdown_active
            = (g_app.ui.ui_active_screen == UiScreenId::Record)
              && (g_app.recording.record_state == RecordUiState::Countdown);
        const bool record_recording_active
            = (g_app.ui.ui_active_screen == UiScreenId::Record)
              && (g_app.recording.record_state == RecordUiState::Recording);
        const bool wav_load_busy
            = (g_app.shared.sample.publish.sd_wav_load_busy.load(std::memory_order_acquire) != 0u);
        const bool sd_manage_trim_save_busy = g_app.ui.sd_manage_trim_save_busy;
        if(prev_wav_load_busy && !wav_load_busy)
            load_success_flash_until_ms = now_ms + 1000u;
        prev_wav_load_busy = wav_load_busy;
        if(prev_sd_manage_trim_save_busy && !sd_manage_trim_save_busy
           && std::strncmp(g_app.ui.sd.save_status, "SAVED", 5) == 0)
        {
            load_success_flash_until_ms = now_ms + 1000u;
        }
        prev_sd_manage_trim_save_busy = sd_manage_trim_save_busy;
        const bool load_success_flash_active = (now_ms < load_success_flash_until_ms);

        // Smooth 1Hz red pulse on both LEDs while user holds R encoder in firmware pairing.
        float pairing_red = 0.0f;
        if(firmware_pairing_hold_active)
        {
            const uint32_t phase_ms = now_ms % 1000u;
            float t = static_cast<float>(phase_ms) / 1000.0f; // 0..1
            float tri = (t < 0.5f) ? (t * 2.0f) : ((1.0f - t) * 2.0f); // 0..1..0
            tri *= tri; // ease for smoother pulse
            pairing_red = 0.08f + 0.92f * tri;
        }

        if(boot_splash_active)
        {
            // Same ping-pong behavior as WAV load, but cyan instead of yellow.
            const bool left_on = ((now_ms / 70u) & 1u) == 0u;
            if(left_on)
            {
                hw.led1.Set(0.0f, 1.0f, 1.0f); // cyan
                hw.led2.Set(0.0f, 0.0f, 0.0f);
            }
            else
            {
                hw.led1.Set(0.0f, 0.0f, 0.0f);
                hw.led2.Set(0.0f, 1.0f, 1.0f); // cyan
            }
        }
        else if(firmware_pairing_hold_active)
        {
            hw.led1.Set(pairing_red, 0.0f, 0.0f);
            hw.led2.Set(pairing_red, 0.0f, 0.0f);
        }
        else if(record_countdown_active)
        {
            const uint32_t elapsed = now_ms - g_app.recording.record_countdown_start_ms;
            const bool blink_on = (elapsed < kRecordCountdownMs) && ((elapsed % 1000u) < 220u);
            const float r = blink_on ? 1.0f : 0.0f;
            hw.led1.Set(r, 0.0f, 0.0f);
            hw.led2.Set(r, 0.0f, 0.0f);
        }
        else if(record_recording_active)
        {
            // TEMP: blank the LEDs while the mic is actually recording.
            // Flip this back to the call below to restore the red indicators.
#if 0
            hw.led1.Set(1.0f, 0.0f, 0.0f);
            hw.led2.Set(1.0f, 0.0f, 0.0f);
#else
            hw.led1.Set(0.0f, 0.0f, 0.0f);
            hw.led2.Set(0.0f, 0.0f, 0.0f);
#endif
        }
        else if(wav_load_busy || sd_manage_trim_save_busy)
        {
            // Pod channel order on this hardware maps green to the 3rd component.
            // Fast ping-pong yellow: alternate LED1/LED2, yellow = red + green.
            const bool left_on = ((now_ms / 70u) & 1u) == 0u;
            if(left_on)
            {
                hw.led1.Set(1.0f, 0.0f, 1.0f); // yellow
                hw.led2.Set(0.0f, 0.0f, 0.0f);
            }
            else
            {
                hw.led1.Set(0.0f, 0.0f, 0.0f);
                hw.led2.Set(1.0f, 0.0f, 1.0f); // yellow
            }
        }
        else if(load_success_flash_active)
        {
            const bool on = DoubleFlashOn(now_ms, 620u);
            const float g = on ? 1.0f : 0.0f;
            hw.led1.Set(0.0f, 0.0f, g);
            hw.led2.Set(0.0f, 0.0f, g);
        }
        else if(sd_browse_active || record_review_active || record_render_review_active
                || record_render_preview_active)
        {
            hw.led1.Set(0.0f, 0.0f, 0.0f);
            hw.led2.Set(0.0f, 0.0f, 1.0f);
        }
        else if(engine_trim_active)
        {
            hw.led1.Set(0.0f, 0.0f, 0.0f);
            if(win_preview_playing)
                hw.led2.Set(1.0f, 0.0f, 0.0f); // red while one-shot playing
            else
                hw.led2.Set(0.0f, 0.0f, 1.0f); // green idle (3rd component = green on this hw)
        }
        else if(g_app.ui.bake_browser_open
                && g_app.ui.ui_active_screen == UiScreenId::SdManageMenu)
        {
            // Bake-mode sample picker: LED2 signals Button2 = sample preview.
            // Green = idle (press to play), red = playing (press to stop).
            // Same green/red mapping as the engine-trim audition above.
            hw.led1.Set(0.0f, 0.0f, 0.0f);
            const bool bake_preview_playing
                = (g_app.shared.bake_preview.active.load(std::memory_order_acquire) != 0u);
            if(bake_preview_playing)
                hw.led2.Set(1.0f, 0.0f, 0.0f); // red while preview playing
            else
                hw.led2.Set(0.0f, 0.0f, 1.0f); // green idle
        }
        else if(perform_ab_active)
        {
            hw.led1.Set(0.0f, 0.0f, 0.0f);
            hw.led2.Set(0.0f, 1.0f, 0.0f);
        }
        else
        {
            hw.led1.Set(0.0f, 0.0f, 0.0f);
            hw.led2.Set(0.0f, 0.0f, 0.0f);
        }

        hw.UpdateLeds();

        g_render.Tick(g_app, g_params);
        g_render.TickOledTransfer(now_ms, midi_busy);
    }
}
