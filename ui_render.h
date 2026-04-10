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

  private:
    OledPager   oled_pager_;
    uint32_t    last_ui_ms_ = 0;
    uint32_t    ui_ticks_accum_ = 0;
    uint32_t    ui_window_start_ms_ = 0;
    uint32_t    last_stats_ms_ = 0;
    uint32_t    last_events_pushed_   = 0;
    uint32_t    last_events_popped_   = 0;
    uint32_t    last_queue_overflows_ = 0;
    uint32_t    last_rx_mod_          = 0;
    uint32_t    last_stolen_voice_index_ = 0;
    uint32_t    last_stolen_start_id_    = 0;
    uint32_t    last_new_start_id_       = 0;
    uint32_t    last_cpu_pct_            = 0;
    uint32_t    last_audio_late_         = 0;
    uint32_t    last_loop_mode_          = 0;
    uint32_t    last_clip_count_         = 0;
    uint32_t    last_voices_active_   = 0;
    uint32_t    last_voices_peak_1s_  = 0;
    uint32_t    last_voice_steals_    = 0;
    uint32_t    last_voice_packed_    = 0;
    uint32_t    last_sample_index_   = 0;
    uint32_t    last_fadeouts_started_ = 0;
    uint32_t    last_vel_layer_      = 0;
    int32_t     last_lfo_            = 0;
    int32_t     last_env_            = 0;
    uint32_t    last_lfo_rate_dbg_   = 0;
    uint32_t    last_lfo_depth_dbg_  = 0;
    uint32_t    last_playhead_frame_[2] = {0, 0};
    uint32_t    last_playhead_active_[2] = {0, 0};

    void Render(const AppState& app, const Params& params);
};
