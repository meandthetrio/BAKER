#pragma once

#include "app_state.h"
#include "params.h"
#include "daisy_pod.h"
#include "dev/oled_ssd130x.h"
#include "oled_pager.h"

// UI Render Layer:
// - Draw only.
// - Uses app.ui.ui_dirty and a 60Hz timer.
// - Does NOT change params or read controls.

class UIRender
{
  public:
    using PodDisplay = daisy::OledDisplay<daisy::SSD130xI2c128x64Driver>;

    void Init(PodDisplay* display, daisy::DaisyPod& hw);
    void Tick(AppState& app, const Params& params);
    void TickOledTransfer(uint32_t now_ms, bool midi_busy);
    // Synchronous "render NOW and push a full frame to the OLED." Bypasses
    // the 60 Hz Tick gate and drains all 8 page transfers in one call
    // (~22 ms blocking on I2C). For long-blocking main-loop work (bake
    // worker) that needs to update the UI mid-task — main-loop Tick can't
    // run, so we have to drive the render pipeline ourselves.
    void RenderNowAndFlushFullFrame(AppState& app, const Params& params);
    bool IsBootSplashActive() const { return boot_splash_phase_ != BootSplashPhase::Done; }

  private:
    enum class BootSplashPhase : uint8_t
    {
        Retro = 0,
        InvertedLogo,
        Done
    };

    OledPager   oled_pager_;
    uint32_t    last_ui_ms_ = 0;
    uint32_t    ui_ticks_accum_ = 0;
    uint32_t    ui_window_start_ms_ = 0;
    uint32_t    last_stats_ms_ = 0;
    uint32_t    last_gain_stats_ms_ = 0;
    // Only the diagnostic fields actually consumed by the HUD screen or the
    // diag overlay are cached here. The other 16 `last_*_` members removed
    // in P7-Cleanup were never read back anywhere they could drive a render,
    // so loading and storing them each 100 ms was pure overhead.
    uint32_t    last_events_pushed_   = 0;
    uint32_t    last_events_popped_   = 0;
    uint32_t    last_queue_overflows_ = 0;
    uint32_t    last_cpu_pct_         = 0;
    uint32_t    last_audio_late_      = 0;
    uint32_t    last_clip_count_      = 0;
    uint32_t    last_velocity_monitor_   = 0;
    uint32_t    last_note_monitor_       = 0;
    uint32_t    last_keyzone_flash_phase_ = 0;
    uint32_t    last_playhead_frame_[2]  = {0, 0};
    uint32_t    last_playhead_active_[2] = {0, 0};
    uint32_t    boot_logo_until_ms_ = 0;
    bool        boot_logo_drawn_ = false;
    BootSplashPhase boot_splash_phase_ = BootSplashPhase::Retro;
    bool        blank_screen_frame_pending_ = false;
    bool        blank_screen_display_off_ = false;

    void Render(const AppState& app, const Params& params);
};
