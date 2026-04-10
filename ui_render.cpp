#include "ui_render.h"
#include "ui_screens.h"
#include "ui_layout.h"
#include "ui_overlay.h"

using namespace daisy;

static constexpr uint16_t kRenderBudgetMs = 4;
static constexpr uint16_t kCooldownMs = 50;

void UIRender::Init(PodDisplay* display, DaisyPod& hw)
{
    (void)display;
    oled_pager_.Init(hw);
    last_ui_ms_  = 0;
    ui_ticks_accum_ = 0;
    ui_window_start_ms_ = 0;
    last_stats_ms_ = 0;
    last_events_pushed_   = 0;
    last_events_popped_   = 0;
    last_queue_overflows_ = 0;
    last_rx_mod_          = 0;
    last_stolen_voice_index_ = 0;
    last_stolen_start_id_    = 0;
    last_new_start_id_       = 0;
    last_cpu_pct_            = 0;
    last_audio_late_         = 0;
    last_loop_mode_          = 0;
    last_clip_count_         = 0;
    last_voices_active_   = 0;
    last_voices_peak_1s_  = 0;
    last_voice_steals_    = 0;
    last_voice_packed_    = 0;
    last_sample_index_    = 0;
    last_fadeouts_started_ = 0;
    last_vel_layer_       = 0;
    last_lfo_             = 0;
    last_env_             = 0;
    last_lfo_rate_dbg_    = 0;
    last_lfo_depth_dbg_   = 0;
    last_playhead_frame_[0] = 0;
    last_playhead_frame_[1] = 0;
    last_playhead_active_[0] = 0;
    last_playhead_active_[1] = 0;
}

void UIRender::Render(const AppState& app, const Params& params)
{
    UiScreenCtx ctx{};
    ctx.app = const_cast<AppState*>(&app);
    ctx.params = const_cast<Params*>(&params);
    ctx.display = &oled_pager_;
    ctx.now_ms = System::GetNow();
    const bool shift_held = app.ui.ui_lshift_held || app.ui.ui_rshift_held;
    ctx.shift = shift_held;
    ctx.lshift = app.ui.ui_lshift_held;
    ctx.rshift = app.ui.ui_rshift_held;
    UiRouter_Render(ctx);

    if(app.render.overlay.visible)
    {
        const UiLayout layout = UiLayout_Default();
        UiOverlay_Render(app, params, layout, oled_pager_);
        const char* hint = app.input.value_edit.active ? "SHIFT:OVER P2:CANC"
                                                  : "SHIFT:OVER P2:BACK";
        UiDraw_Footer(oled_pager_, layout, hint);
    }
}

void UIRender::TickOledTransfer(uint32_t now_ms, bool midi_busy)
{
    oled_pager_.TickTransferOnePage(now_ms, midi_busy);
}

void UIRender::Tick(AppState& app, const Params& params)
{
    if(!oled_pager_.IsReady())
        return;

    const uint32_t now_ms = System::GetNow();

    // 60Hz timer gate
    if((now_ms - last_ui_ms_) < 16)
        return;
    last_ui_ms_ = now_ms;

    if(ui_window_start_ms_ == 0)
        ui_window_start_ms_ = now_ms;
    ui_ticks_accum_++;
    if((now_ms - ui_window_start_ms_) >= 1000)
    {
        app.input.ui_hz = ui_ticks_accum_;
        ui_ticks_accum_ = 0;
        ui_window_start_ms_ = now_ms;
        app.ui.ui_dirty = true;
    }

    if(UiNav_Active(app.ui.ui_nav) == UiScreenId::PerformWaveEdit)
    {
        for(uint8_t layer = 0; layer < 2; ++layer)
        {
            const uint32_t frame = app.playhead_frame[layer].load(std::memory_order_relaxed);
            const uint32_t active = app.playhead_active[layer].load(std::memory_order_relaxed);
            if(frame != last_playhead_frame_[layer] || active != last_playhead_active_[layer])
            {
                app.ui.ui_dirty = true;
                last_playhead_frame_[layer] = frame;
                last_playhead_active_[layer] = active;
            }
        }
    }

    uint32_t pushed = 0, popped = 0, ovf = 0, vact = 0, vstl = 0, vpack = 0;
    uint32_t rx_mod = last_rx_mod_;
    uint32_t lsv = last_stolen_voice_index_;
    uint32_t old_id = last_stolen_start_id_;
    uint32_t new_id = last_new_start_id_;
    uint32_t cpu_pct = last_cpu_pct_;
    uint32_t late_cnt = last_audio_late_;
    uint32_t loop_mode = last_loop_mode_;
    uint32_t clip_cnt = last_clip_count_;
    uint32_t kg_idx = last_sample_index_;
    uint32_t fadeouts = last_fadeouts_started_;
    uint32_t vel_layer = last_vel_layer_;
    int32_t lfo_val = last_lfo_;
    int32_t env_val = last_env_;
    uint32_t lfo_rate_dbg = last_lfo_rate_dbg_;
    uint32_t lfo_depth_dbg = last_lfo_depth_dbg_;
    bool     stats_loaded = false;

    const bool stats_due = (now_ms - last_stats_ms_) >= 100;
    if(stats_due)
    {
        pushed = app.events_pushed.load(std::memory_order_relaxed);
        popped = app.events_popped.load(std::memory_order_relaxed);
        ovf    = app.queue_overflows.load(std::memory_order_relaxed);
        const uint32_t rx = app.midi_rx_count.load(std::memory_order_relaxed);
        rx_mod = rx % 1000;
        lsv    = app.last_stolen_voice_index.load(std::memory_order_relaxed);
        old_id = app.last_stolen_start_id.load(std::memory_order_relaxed);
        new_id = app.last_new_start_id.load(std::memory_order_relaxed);
        const uint32_t peak_cycles = app.audio_cycles_peak.load(std::memory_order_relaxed);
        const uint32_t budget_cycles = app.audio_budget_cycles.load(std::memory_order_relaxed);
        cpu_pct = 0;
        if(budget_cycles > 0)
            cpu_pct = (peak_cycles * 100u + (budget_cycles / 2u)) / budget_cycles;
        if(cpu_pct > 999u)
            cpu_pct = 999u;
        late_cnt = app.audio_late_count.load(std::memory_order_relaxed);
        loop_mode = app.loop_mode.load(std::memory_order_relaxed);
        clip_cnt = app.clip_count.load(std::memory_order_relaxed);
        vact   = app.voices_active.load(std::memory_order_relaxed);
        const uint32_t vpk1s = app.voices_peak_1s.load(std::memory_order_relaxed);
        vstl   = app.voice_steals.load(std::memory_order_relaxed);
        vpack  = app.last_voice_packed.load(std::memory_order_relaxed);
        kg_idx = app.last_sample_index.load(std::memory_order_relaxed);
        fadeouts = app.fadeouts_started.load(std::memory_order_relaxed);
        vel_layer = app.last_vel_layer.load(std::memory_order_relaxed);
        lfo_val = app.last_lfo.load(std::memory_order_relaxed);
        env_val = app.last_env.load(std::memory_order_relaxed);
        lfo_rate_dbg = app.lfo_rate_dbg.load(std::memory_order_relaxed);
        lfo_depth_dbg = app.lfo_depth_dbg.load(std::memory_order_relaxed);
        stats_loaded = true;

        if((pushed != last_events_pushed_) || (popped != last_events_popped_)
           || (ovf != last_queue_overflows_) || (rx_mod != last_rx_mod_)
           || (lsv != last_stolen_voice_index_)
           || (old_id != last_stolen_start_id_)
           || (new_id != last_new_start_id_)
           || (cpu_pct != last_cpu_pct_)
           || (late_cnt != last_audio_late_)
           || (loop_mode != last_loop_mode_)
           || (clip_cnt != last_clip_count_)
           || (vact != last_voices_active_)
           || (vpk1s != last_voices_peak_1s_)
           || (vstl != last_voice_steals_) || (vpack != last_voice_packed_)
           || (kg_idx != last_sample_index_)
           || (fadeouts != last_fadeouts_started_)
           || (vel_layer != last_vel_layer_)
           || (lfo_val != last_lfo_)
           || (env_val != last_env_)
           || (lfo_rate_dbg != last_lfo_rate_dbg_)
           || (lfo_depth_dbg != last_lfo_depth_dbg_))
            app.ui.ui_dirty = true;

        // Decimate stats-driven dirty marking to 10Hz max.
        last_stats_ms_ = now_ms;

        last_voices_peak_1s_ = vpk1s;
    }

    if(oled_pager_.IsTransferring())
        return;

    if(now_ms < app.render.render_cooldown_until_ms)
    {
        app.render.render_skips++;
        return;
    }

    if(!app.ui.ui_dirty && !app.render.overlay.visible)
        return;

    const uint32_t start_ms = System::GetNow();
    Render(app, params);
    oled_pager_.BeginFrameTransfer();
    const uint32_t end_ms = System::GetNow();
    uint32_t dt32 = end_ms - start_ms;
    if(dt32 > 0xFFFFu)
        dt32 = 0xFFFFu;
    const uint16_t dt = static_cast<uint16_t>(dt32);
    app.render.render_ms = dt;
    if(dt > app.render.render_hi_ms)
        app.render.render_hi_ms = dt;
    app.render.render_frames++;
    if(dt > kRenderBudgetMs)
        app.render.render_cooldown_until_ms = end_ms + kCooldownMs;

    app.ui.ui_dirty = false;

    // Update cached stats after any render (even if render was triggered by controls).
    if(!stats_loaded)
    {
        pushed = app.events_pushed.load(std::memory_order_relaxed);
        popped = app.events_popped.load(std::memory_order_relaxed);
        ovf    = app.queue_overflows.load(std::memory_order_relaxed);
        const uint32_t rx = app.midi_rx_count.load(std::memory_order_relaxed);
        rx_mod = rx % 1000;
        lsv    = app.last_stolen_voice_index.load(std::memory_order_relaxed);
        old_id = app.last_stolen_start_id.load(std::memory_order_relaxed);
        new_id = app.last_new_start_id.load(std::memory_order_relaxed);
        const uint32_t peak_cycles = app.audio_cycles_peak.load(std::memory_order_relaxed);
        const uint32_t budget_cycles = app.audio_budget_cycles.load(std::memory_order_relaxed);
        cpu_pct = 0;
        if(budget_cycles > 0)
            cpu_pct = (peak_cycles * 100u + (budget_cycles / 2u)) / budget_cycles;
        if(cpu_pct > 999u)
            cpu_pct = 999u;
        late_cnt = app.audio_late_count.load(std::memory_order_relaxed);
        loop_mode = app.loop_mode.load(std::memory_order_relaxed);
        clip_cnt = app.clip_count.load(std::memory_order_relaxed);
        vact   = app.voices_active.load(std::memory_order_relaxed);
        last_voices_peak_1s_ = app.voices_peak_1s.load(std::memory_order_relaxed);
        vstl   = app.voice_steals.load(std::memory_order_relaxed);
        vpack  = app.last_voice_packed.load(std::memory_order_relaxed);
        kg_idx = app.last_sample_index.load(std::memory_order_relaxed);
        fadeouts = app.fadeouts_started.load(std::memory_order_relaxed);
        vel_layer = app.last_vel_layer.load(std::memory_order_relaxed);
        lfo_val = app.last_lfo.load(std::memory_order_relaxed);
        env_val = app.last_env.load(std::memory_order_relaxed);
        lfo_rate_dbg = app.lfo_rate_dbg.load(std::memory_order_relaxed);
        lfo_depth_dbg = app.lfo_depth_dbg.load(std::memory_order_relaxed);
    }

    last_events_pushed_   = pushed;
    last_events_popped_   = popped;
    last_queue_overflows_ = ovf;
    last_rx_mod_          = rx_mod;
    last_stolen_voice_index_ = lsv;
    last_stolen_start_id_    = old_id;
    last_new_start_id_       = new_id;
    last_cpu_pct_            = cpu_pct;
    last_audio_late_         = late_cnt;
    last_loop_mode_          = loop_mode;
    last_clip_count_         = clip_cnt;
    last_voices_active_   = vact;
    last_voice_steals_    = vstl;
    last_voice_packed_    = vpack;
    last_sample_index_    = kg_idx;
    last_fadeouts_started_ = fadeouts;
    last_vel_layer_       = vel_layer;
    last_lfo_             = lfo_val;
    last_env_             = env_val;
    last_lfo_rate_dbg_    = lfo_rate_dbg;
    last_lfo_depth_dbg_   = lfo_depth_dbg;
}
