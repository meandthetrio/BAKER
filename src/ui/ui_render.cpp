#include "ui_render.h"
#include "ui_screens.h"
#include "ui_layout.h"
#include "ui_overlay.h"
#include "ui_boot_logo.h"
#include "ui_draw_text.h"

using namespace daisy;

// Transient centered "saved" confirmation banner, drawn on top of the active
// screen for ~1 s after a successful save (preset or bake).
static void DrawSavedBanner(OledPager& d, const char* msg)
{
    if(!msg || msg[0] == '\0')
        msg = "SAVED";
    const int tw = TinyStringWidth(msg);
    const int box_w = tw + 10;
    const int box_h = Font5x7::H + 10;
    const int x0 = (128 - box_w) / 2;
    const int y0 = (64 - box_h) / 2;
    d.DrawRect(x0, y0, x0 + box_w - 1, y0 + box_h - 1, false, true);  // clear box
    d.DrawRect(x0, y0, x0 + box_w - 1, y0 + box_h - 1, true, false);  // border
    DrawTinyString(d, msg, x0 + 5, y0 + 5, true);
}

static constexpr uint16_t kRenderBudgetMs = 4;
static constexpr uint16_t kCooldownMs = 50;
static constexpr uint32_t kBootRetroDurationMs = 3500;
// Dithered fade-in completes within the first slice of the total splash
// duration; the logo then holds fully revealed for the remainder.
static constexpr uint32_t kBootRetroFadeInMs = 1800;

void UIRender::Init(PodDisplay* display, DaisyPod& hw)
{
    (void)display;
    oled_pager_.Init(hw);
    last_ui_ms_  = 0;
    ui_ticks_accum_ = 0;
    ui_window_start_ms_ = 0;
    last_stats_ms_ = 0;
    last_gain_stats_ms_ = 0;
    last_events_pushed_   = 0;
    last_events_popped_   = 0;
    last_queue_overflows_ = 0;
    last_cpu_pct_          = 0;
    last_audio_late_       = 0;
    last_clip_count_       = 0;
    last_velocity_monitor_ = 0;
    last_note_monitor_     = 0;
    last_playhead_frame_[0]  = 0;
    last_playhead_frame_[1]  = 0;
    last_playhead_active_[0] = 0;
    last_playhead_active_[1] = 0;
    boot_splash_phase_ = BootSplashPhase::Retro;
    boot_logo_until_ms_ = System::GetNow() + kBootRetroDurationMs;
    boot_logo_drawn_ = false;
    blank_screen_frame_pending_ = false;
    blank_screen_display_off_ = false;
}

void UIRender::Render(const AppState& app, const Params& params)
{
    const AppUiState& ui = app.ui;
    const AppDiagnosticsState& diag = app.diag;
    UiScreenCtx ctx{};
    AppState& mutable_app = const_cast<AppState&>(app);
    Params& mutable_params = const_cast<Params&>(params);
    UiSessionState ctx_session{&mutable_app.ui,
                               &mutable_app.engine,
                               &mutable_app.recording,
                               &mutable_app.project};
    UiScreenCtx_BindSession(ctx, ctx_session);
    ctx.diag = &mutable_app.diag;
    ctx.shared = &mutable_app.shared;
    ctx.worker = &mutable_app.worker;
    ctx.params = &mutable_params;
    ctx.display = &oled_pager_;
    ctx.now_ms = System::GetNow();
    const bool shift_held = ui.ui_lshift_held || ui.ui_rshift_held;
    ctx.shift = shift_held;
    ctx.lshift = ui.ui_lshift_held;
    ctx.rshift = ui.ui_rshift_held;
    UiRouter_Render(ctx);

    if(diag.overlay.visible)
    {
        const UiLayout layout = UiLayout_Default();
        UiOverlay_Render(app.ui, app.diag, app.worker, params, layout, oled_pager_);
    }

    // "saved" confirmation banner (auto-expires; no nav state needed).
    if(static_cast<int32_t>(ui.preset_saved_overlay_until_ms - ctx.now_ms) > 0)
        DrawSavedBanner(oled_pager_, ui.saved_overlay_text);
}

void UIRender::RenderNowAndFlushFullFrame(AppState& app, const Params& params)
{
    if(!oled_pager_.IsReady())
        return;
    // Render the active screen into the back buffer, arm a transfer, then drain
    // all 8 pages synchronously. The normal 60 Hz path is async (DMA); this path
    // is only used inside already-blocking long ops (e.g. the SD save/load
    // splash), so a blocking drain is fine and is what the caller expects.
    oled_pager_.Fill(false);
    Render(app, params);
    oled_pager_.BeginFrameTransfer();
    oled_pager_.FlushFrameBlocking();
}

void UIRender::TickOledTransfer(uint32_t now_ms, bool midi_busy)
{
    oled_pager_.TickTransferOnePage(now_ms, midi_busy);
}

void UIRender::Tick(AppState& app, const Params& params)
{
    AppUiState& ui = app.ui;
    AppDiagnosticsState& diag = app.diag;
    if(!oled_pager_.IsReady())
        return;

    const uint32_t now_ms = System::GetNow();

    if(boot_splash_phase_ != BootSplashPhase::Done)
    {
        if(now_ms >= boot_logo_until_ms_)
        {
            // Single-phase splash: just the Retro logo, then straight to Done.
            boot_splash_phase_ = BootSplashPhase::Done;
            boot_logo_drawn_ = false;
            ui.ui_dirty = true;
        }

        if(boot_splash_phase_ != BootSplashPhase::Done)
        {
            // boot_logo_drawn_ here means "the fully-revealed frame has been
            // drawn" — once true, the dither animation is done and we hold
            // the static logo for the rest of the splash without redrawing.
            if(boot_logo_drawn_ || oled_pager_.IsTransferring())
                return;

            const uint32_t phase_start_ms = boot_logo_until_ms_ - kBootRetroDurationMs;
            const uint32_t elapsed_ms = now_ms - phase_start_ms;
            const float progress = static_cast<float>(elapsed_ms)
                                    / static_cast<float>(kBootRetroFadeInMs);

            oled_pager_.Fill(false);
            UiBootLogo::DrawRetro(oled_pager_, progress);
            oled_pager_.BeginFrameTransfer();
            boot_logo_drawn_ = (progress >= 1.0f);
            return;
        }
    }

    if(blank_screen_display_off_ && !ui.ui_blank_screen_active)
    {
        oled_pager_.SetTransferSuppressed(false);
        oled_pager_.SetDisplayOn(true);
        blank_screen_display_off_ = false;
        blank_screen_frame_pending_ = false;
        last_ui_ms_ = 0;
        diag.render_cooldown_until_ms = 0;
        ui.ui_dirty = true;
    }
    else if(!ui.ui_blank_screen_active)
    {
        blank_screen_frame_pending_ = false;
    }

    if(ui.ui_blank_screen_active)
    {
        if(blank_screen_display_off_)
            return;

        if(oled_pager_.IsTransferring())
            return;

        if(!blank_screen_frame_pending_)
        {
            oled_pager_.Fill(false);
            oled_pager_.BeginFrameTransfer();
            blank_screen_frame_pending_ = true;
            return;
        }

        oled_pager_.SetDisplayOn(false);
        oled_pager_.SetTransferSuppressed(true);
        blank_screen_display_off_ = true;
        blank_screen_frame_pending_ = false;
        return;
    }

    // 60Hz timer gate
    if((now_ms - last_ui_ms_) < 16)
        return;
    last_ui_ms_ = now_ms;

    if(ui_window_start_ms_ == 0)
        ui_window_start_ms_ = now_ms;
    ui_ticks_accum_++;
    if((now_ms - ui_window_start_ms_) >= 1000)
    {
        ui.ui_hz = ui_ticks_accum_;
        ui_ticks_accum_ = 0;
        ui_window_start_ms_ = now_ms;
        ui.ui_dirty = true;
    }

    if(last_gain_stats_ms_ == 0)
        last_gain_stats_ms_ = now_ms;
    if((now_ms - last_gain_stats_ms_) >= 1000)
    {
        for(uint8_t i = 0; i < kDiagGainProbeCount; ++i)
            diag.gain_probe_display_db[i]
                = DiagnosticsReadAndResetPeakDbfs(diag.gain_probe_peak_bits[i]);
        last_gain_stats_ms_ = now_ms;
        if(diag.overlay.visible && diag.overlay.page != kDiagOverlayPageSys)
            ui.ui_dirty = true;
    }

    if(UiNav_Active(ui.ui_nav) == UiScreenId::PerformWaveEdit)
    {
        for(uint8_t layer = 0; layer < 2; ++layer)
        {
            const uint32_t frame = diag.playhead_frame[layer].load(std::memory_order_relaxed);
            const uint32_t active = diag.playhead_active[layer].load(std::memory_order_relaxed);
            if(frame != last_playhead_frame_[layer] || active != last_playhead_active_[layer])
            {
                ui.ui_dirty = true;
                last_playhead_frame_[layer] = frame;
                last_playhead_active_[layer] = active;
            }
        }
    }

    {
        const UiScreenId active = UiNav_Active(ui.ui_nav);
        if(active == UiScreenId::VelocityMod || active == UiScreenId::VelocityMod2)
        {
            const uint32_t v = diag.last_velocity.load(std::memory_order_relaxed);
            if(v != last_velocity_monitor_)
            {
                ui.ui_dirty = true;
                last_velocity_monitor_ = v;
            }
            const uint32_t n = diag.last_note.load(std::memory_order_relaxed);
            if(n != last_note_monitor_)
            {
                ui.ui_dirty = true;
                last_note_monitor_ = n;
            }
        }
        else if(active == UiScreenId::PerformKeyzone)
        {
            // Trigger a keybed play-flash on each note-on and keep redrawing at
            // ~3 Hz while the flash window is open so the blink animates. Only a
            // phase flip (or a new note) marks the screen dirty, so the window is
            // a handful of redraws, not a continuous refresh.
            const uint32_t n = diag.last_note.load(std::memory_order_relaxed);
            const uint32_t v = diag.last_velocity.load(std::memory_order_relaxed);
            if(n != last_note_monitor_ || v != last_velocity_monitor_)
            {
                last_note_monitor_     = n;
                last_velocity_monitor_ = v;
                ui.keyzone_flash_note     = static_cast<uint8_t>(n > 127u ? 127u : n);
                ui.keyzone_flash_until_ms = now_ms + 900u; // ~3 flashes at 3 Hz
                ui.ui_dirty = true;
            }
            if(static_cast<int32_t>(ui.keyzone_flash_until_ms - now_ms) > 0)
            {
                const uint32_t phase = now_ms / 167u;
                if(phase != last_keyzone_flash_phase_)
                {
                    last_keyzone_flash_phase_ = phase;
                    ui.ui_dirty = true;
                }
            }
        }
    }

    // Saved-banner pending flag set by callers without a timestamp (bake save):
    // open the 1 s banner window using now_ms.
    if(ui.saved_overlay_pending)
    {
        ui.saved_overlay_pending = false;
        ui.preset_saved_overlay_until_ms = now_ms + 1000u;
        ui.ui_dirty = true;
    }

    // Keep redrawing through the saved-banner window, plus a short tail (~50 ms)
    // past expiry so one frame renders without the banner to clear it (rendering
    // is otherwise dirty-gated).
    if(ui.preset_saved_overlay_until_ms != 0
       && static_cast<int32_t>((ui.preset_saved_overlay_until_ms + 50u) - now_ms) > 0)
        ui.ui_dirty = true;

    // Only the diag fields that are actually rendered (HUD screen + diag
    // overlay) participate in dirty tracking here. Other diag fields are
    // left alone — the UI never reads them back, so we don't pay the 100ms
    // atomic load tax either.
    uint32_t pushed   = 0;
    uint32_t popped   = 0;
    uint32_t ovf      = 0;
    uint32_t cpu_pct  = 0;
    uint32_t late_cnt = 0;
    uint32_t clip_cnt = 0;
    bool     stats_loaded = false;

    const bool stats_due = (now_ms - last_stats_ms_) >= 100;
    if(stats_due)
    {
        pushed = diag.events_pushed.load(std::memory_order_relaxed);
        popped = diag.events_popped.load(std::memory_order_relaxed);
        ovf    = diag.queue_overflows.load(std::memory_order_relaxed);
        const uint32_t peak_cycles   = diag.audio_cycles_peak.load(std::memory_order_relaxed);
        const uint32_t budget_cycles = diag.audio_budget_cycles.load(std::memory_order_relaxed);
        if(budget_cycles > 0)
            cpu_pct = (peak_cycles * 100u + (budget_cycles / 2u)) / budget_cycles;
        if(cpu_pct > 999u)
            cpu_pct = 999u;
        late_cnt = diag.audio_late_count.load(std::memory_order_relaxed);
        clip_cnt = diag.clip_count.load(std::memory_order_relaxed);
        stats_loaded = true;

        // Only mark ui_dirty for diag fields actually rendered on the
        // currently visible surface.
        //   HUD screen            -> cpu_pct, late_cnt
        //   Diag overlay (opt-in) -> cpu_pct, late_cnt, clip_cnt, ovf,
        //                            pushed, popped
        const UiScreenId active_screen   = UiNav_Active(ui.ui_nav);
        const bool       is_hud_screen   = (active_screen == UiScreenId::Hud);
        const bool       is_overlay_vis  = diag.overlay.visible;
        const bool       hud_group_watch = is_hud_screen || is_overlay_vis;

        if(hud_group_watch)
        {
            if((cpu_pct != last_cpu_pct_)
               || (late_cnt != last_audio_late_))
                ui.ui_dirty = true;
        }
        if(is_overlay_vis)
        {
            if((clip_cnt != last_clip_count_)
               || (ovf != last_queue_overflows_)
               || (pushed != last_events_pushed_)
               || (popped != last_events_popped_))
                ui.ui_dirty = true;
            if(diag.overlay.page != kDiagOverlayPageSys)
                ui.ui_dirty = true;
        }

        // Decimate stats-driven dirty marking to 10 Hz max.
        last_stats_ms_ = now_ms;
    }

    if(oled_pager_.IsTransferring())
        return;

    if(now_ms < diag.render_cooldown_until_ms)
    {
        diag.render_skips++;
        return;
    }

    if(!ui.ui_dirty && !diag.overlay.visible)
        return;

    const uint32_t start_ms = System::GetNow();
    Render(app, params);
    oled_pager_.BeginFrameTransfer();
    const uint32_t end_ms = System::GetNow();
    uint32_t dt32 = end_ms - start_ms;
    if(dt32 > 0xFFFFu)
        dt32 = 0xFFFFu;
    const uint16_t dt = static_cast<uint16_t>(dt32);
    diag.render_ms = dt;
    if(dt > diag.render_hi_ms)
        diag.render_hi_ms = dt;
    diag.render_frames++;
    if(dt > kRenderBudgetMs)
        diag.render_cooldown_until_ms = end_ms + kCooldownMs;

    ui.ui_dirty = false;

    // On control-triggered renders we may not have loaded fresh stats yet;
    // do it here so the cached last_* values stay current.
    if(!stats_loaded)
    {
        pushed = diag.events_pushed.load(std::memory_order_relaxed);
        popped = diag.events_popped.load(std::memory_order_relaxed);
        ovf    = diag.queue_overflows.load(std::memory_order_relaxed);
        const uint32_t peak_cycles   = diag.audio_cycles_peak.load(std::memory_order_relaxed);
        const uint32_t budget_cycles = diag.audio_budget_cycles.load(std::memory_order_relaxed);
        cpu_pct = 0;
        if(budget_cycles > 0)
            cpu_pct = (peak_cycles * 100u + (budget_cycles / 2u)) / budget_cycles;
        if(cpu_pct > 999u)
            cpu_pct = 999u;
        late_cnt = diag.audio_late_count.load(std::memory_order_relaxed);
        clip_cnt = diag.clip_count.load(std::memory_order_relaxed);
    }

    last_events_pushed_   = pushed;
    last_events_popped_   = popped;
    last_queue_overflows_ = ovf;
    last_cpu_pct_         = cpu_pct;
    last_audio_late_      = late_cnt;
    last_clip_count_      = clip_cnt;
}
