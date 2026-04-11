#include "ui_screens_internal.h"
#include "ui_screen_record_internal.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "app_state_ui.h"
#include "app_state_engine.h"
#include "app_state_recording.h"
#include "app_state_project.h"
#include "app_state_diagnostics.h"
#include "app_state_shared.h"
#include "app_state_worker.h"
#include "oled_pager.h"
#include "params.h"
#include "sample_edit.h"
#include "sd_browser_state.h"
#include "sd_sample_pool.h"
#include "ui_input.h"
#include "ui_layout.h"
#include "ui_requests.h"

static constexpr uint32_t kRecordCountdownMs = 4000;

static float Clamp01(float x)
{
    if(x < 0.0f) return 0.0f;
    if(x > 1.0f) return 1.0f;
    return x;
}

static void Record_ResetLiveWave(AppSharedState& shared)
{
    for(int i = 0; i < 128; ++i)
    {
        shared.recording.rec_live_min[i] = 0;
        shared.recording.rec_live_max[i] = 0;
    }
    shared.recording.rec_live_last_col = -1;
}

void Record_StopPreview(AppRecordingState& recording)
{
    recording.record_preview_hold = false;
    recording.record_preview_gate = false;
}

static void Record_PrepareRecordingUiState(AppEngineState& engine,
                                           AppRecordingState& recording,
                                           AppSharedState& shared)
{
    Record_StopPreview(recording);
    shared.recording.rec_monitor_enable.store(0, std::memory_order_release);
    shared.recording.rec_start_req.store(0, std::memory_order_release);
    shared.recording.rec_stop_req.store(0, std::memory_order_release);
    shared.recording.rec_active.store(0, std::memory_order_release);
    shared.recording.rec_pos.store(0, std::memory_order_release);
    shared.recording.rec_length.store(0, std::memory_order_release);
    Record_ResetLiveWave(shared);

    const uint8_t slot = engine.perform_nav.perform_layer & 1u;
    recording.record_slot = slot;
    shared.recording.rec_slot_pending.store(slot, std::memory_order_release);

    // Reset target slot metadata so review/save reflects the fresh unsaved take.
    Sample& s = shared.sample.publish.sd_slots[slot];
    s.pcm = nullptr;
    s.length = 0;
    s.sample_rate = 48000;
    s.root_key = 60;
    s.loop_start = 0;
    s.loop_end = 0;
    s.loop_enabled = false;

    SampleEdit edit = SampleEdit_Default(0);
    shared.sample.edit.sd_edit_slots[slot] = edit;
    shared.sample.edit.sd_edit_pending = edit;
    shared.sample.edit.sd_edit_slot.store(slot, std::memory_order_release);
    shared.sample.edit.sd_edit_gen.fetch_add(1, std::memory_order_acq_rel);
    shared.sample.edit.sd_edit_ready.store(1, std::memory_order_release);
}

static void Record_StartRecording(UiScreenCtx& ctx)
{
    if(!ctx.ui)
        return;

    AppUiState& ui = *ctx.ui;
    AppEngineState& engine = *ctx.engine;
    AppRecordingState& recording = *ctx.recording;
    AppProjectState& project = *ctx.project;
    AppDiagnosticsState& diag = *ctx.diag;
    AppSharedState& shared = *ctx.shared;
    AppWorkerState& worker = *ctx.worker;
    const uint8_t src = (recording.record_source_index == 1)
                            ? static_cast<uint8_t>(RecordInputSource::Mic)
                            : static_cast<uint8_t>(RecordInputSource::LineIn);
    shared.recording.rec_source_sel.store(src, std::memory_order_release);
    Record_PrepareRecordingUiState(engine, recording, shared);
    // Keep input monitor live while recording.
    shared.recording.rec_monitor_enable.store(1, std::memory_order_release);
    shared.recording.rec_start_req.store(1, std::memory_order_release);
    recording.record_state = RecordUiState::Recording;
    ui.ui_dirty = true;
}

static void Record_RenderReadyCuzStyle(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.display)
        return;
    AppUiState& ui = *ctx.ui;
    AppEngineState& engine = *ctx.engine;
    AppRecordingState& recording = *ctx.recording;
    AppProjectState& project = *ctx.project;
    AppDiagnosticsState& diag = *ctx.diag;
    AppSharedState& shared = *ctx.shared;
    AppWorkerState& worker = *ctx.worker;
    OledPager& d = *ctx.display;

    static uint8_t text_mask[64][128];
    static uint8_t text_fb[64][128];
    static uint8_t bold_fb[64][128];
    static uint8_t fb_buf[64][128];
    static uint8_t cached_source = 0xFFu;
    static bool cache_valid = false;

    if(recording.record_anim_start_ms < 0.0)
        recording.record_anim_start_ms = static_cast<double>(ctx.now_ms);

    const double elapsed_s = (static_cast<double>(ctx.now_ms) - recording.record_anim_start_ms) / 1000.0;
    const int cx = 64;
    const int cy = 32;

    const char* line1 = (recording.record_source_index == 1) ? "RECORD MICROPHONE" : "RECORD LINE IN";
    const char* line2 = "READY";
    int scale = 2;
    int char_spacing = scale;
    int line_gap = scale * 2;
    int char_h = Font5x7::H * scale;
    const int lines = 2;
    int text_h = lines * char_h + (lines - 1) * line_gap;
    int y0 = (64 / 2) - (text_h / 2);

    if(!cache_valid || cached_source != recording.record_source_index)
    {
        std::memset(text_mask, 0, sizeof(text_mask));
        std::memset(text_fb, 0, sizeof(text_fb));
        std::memset(bold_fb, 0, sizeof(bold_fb));

        auto mark_char = [&](int x, int y, char c)
        {
            uint8_t rows[Font5x7::H] = {};
            Font5x7::GetGlyphRows(c, rows);
            for(int yy = 0; yy < Font5x7::H; ++yy)
            {
                uint8_t row = rows[yy];
                for(int xx = 0; xx < Font5x7::W; ++xx)
                {
                    if(((row >> (Font5x7::W - 1 - xx)) & 1u) == 0u)
                        continue;
                    for(int sy = 0; sy < scale; ++sy)
                    {
                        for(int sx = 0; sx < scale; ++sx)
                        {
                            const int px = x + xx * scale + sx;
                            const int py = y + yy * scale + sy;
                            if(px >= 0 && px < 128 && py >= 0 && py < 64)
                            {
                                text_mask[py][px] = 1;
                                text_fb[py][px] = 1;
                            }
                        }
                    }
                }
            }
        };

        auto mark_line = [&](int x, int y, const char* text)
        {
            const int char_w = Font5x7::W * scale;
            int cx0 = x;
            for(const char* p = text; *p; ++p)
            {
                mark_char(cx0, y, *p);
                cx0 += char_w + char_spacing;
            }
        };

        auto width = [&](const char* t)
        {
            const int len = static_cast<int>(std::strlen(t));
            if(len <= 0)
                return 0;
            const int char_w = Font5x7::W * scale;
            return len * char_w + (len - 1) * char_spacing;
        };

        int max_w = width(line1);
        const int line2_w = width(line2);
        if(line2_w > max_w)
            max_w = line2_w;
        if(max_w > 128)
        {
            scale = 1;
            char_spacing = scale;
            line_gap = scale * 2;
            char_h = Font5x7::H * scale;
        }
        text_h = lines * char_h + (lines - 1) * line_gap;
        y0 = (64 / 2) - (text_h / 2);

        auto mark_centered = [&](const char* t1, const char* t2)
        {
            auto width2 = [&](const char* t)
            {
                const int len = static_cast<int>(std::strlen(t));
                if(len <= 0)
                    return 0;
                const int char_w = Font5x7::W * scale;
                return len * char_w + (len - 1) * char_spacing;
            };
            const int w1 = width2(t1);
            const int w2 = width2(t2);
            const int x1 = (128 / 2) - (w1 / 2);
            const int x2 = (128 / 2) - (w2 / 2);
            mark_line(x1, y0, t1);
            mark_line(x2, y0 + char_h + line_gap, t2);
        };

        mark_centered(line1, line2);

        // Precompute bold version of the text-only buffer.
        for(int y = 0; y < 64; ++y)
        {
            for(int x = 0; x < 128; ++x)
            {
                if(!text_mask[y][x])
                    continue;
                for(int dy = 0; dy <= 1; ++dy)
                {
                    for(int dx = 0; dx <= 1; ++dx)
                    {
                        const int px = x + dx;
                        const int py = y + dy;
                        if(px >= 0 && px < 128 && py >= 0 && py < 64)
                            bold_fb[py][px] = 1;
                    }
                }
            }
        }
        cache_valid = true;
        cached_source = recording.record_source_index;
    }

    const double max_visible_r = std::sqrt(std::pow(128 / 2.0, 2) + std::pow(64 / 2.0, 2));
    const double start_r = max_visible_r + 10.0;
    const double duration_s = 1.0;
    const double offset1_s = 0.2;
    const double offset2_s = offset1_s + 0.3;
    const double grow_duration_s = 0.5;
    const double grow_start_s = offset2_s + duration_s / 2.0;
    const double gap_s = 0.1;
    const double cycle_s = grow_start_s + grow_duration_s + gap_s;
    const double anim_t = std::fmod(elapsed_s, cycle_s);
    const double flicker_on_s = 0.1;
    const double flicker_off_s = 0.1;
    const double flicker_period = flicker_on_s + flicker_off_s;
    const double flicker_phase = std::fmod(anim_t, flicker_period);
    const bool flicker_on = (flicker_phase < flicker_on_s);
    std::memcpy(fb_buf, flicker_on ? bold_fb : text_fb, sizeof(fb_buf));

    auto for_circle_perimeter = [&](int r, const auto& fn)
    {
        if(r <= 0)
            return;
        auto plot_if_in = [&](int px, int py)
        {
            if(px >= 0 && px < 128 && py >= 0 && py < 64)
                fn(px, py);
        };
        int x = r;
        int y = 0;
        int err = 1 - x;
        while(x >= y)
        {
            plot_if_in(cx + x, cy + y);
            plot_if_in(cx + y, cy + x);
            plot_if_in(cx - y, cy + x);
            plot_if_in(cx - x, cy + y);
            plot_if_in(cx - x, cy - y);
            plot_if_in(cx - y, cy - x);
            plot_if_in(cx + y, cy - x);
            plot_if_in(cx + x, cy - y);
            ++y;
            if(err < 0)
                err += 2 * y + 1;
            else
            {
                --x;
                err += 2 * (y - x) + 1;
            }
        }
    };

    auto animate_circle = [&](double t_offset,
                              int thickness_px,
                              bool invert_text,
                              double speedup_after_abs = -1.0,
                              double speedup_factor = 1.0)
    {
        const double local_t = anim_t - t_offset;
        if(local_t < 0.0 || local_t > duration_s)
            return;
        double adj_local_t = local_t;
        if(speedup_after_abs >= 0.0 && speedup_factor != 1.0)
        {
            const double threshold_local = speedup_after_abs - t_offset;
            if(local_t > threshold_local)
            {
                const double extra = local_t - threshold_local;
                adj_local_t = threshold_local + extra * speedup_factor;
                if(adj_local_t > duration_s)
                    adj_local_t = duration_s;
            }
        }

        const double f = 1.0 - (adj_local_t / duration_s);
        const double r = start_r * f;
        if(r > max_visible_r)
            return;
        const int ri = static_cast<int>(std::round(r));
        for(int t = 0; t < thickness_px; ++t)
        {
            const int rr = ri - t;
            if(rr <= 0)
                continue;
            for_circle_perimeter(rr, [&](int px, int py)
            {
                if(text_mask[py][px] && invert_text)
                    fb_buf[py][px] = !fb_buf[py][px];
                else if(!text_mask[py][px])
                    fb_buf[py][px] = 1;
            });
        }
    };

    animate_circle(0.0, 2, false);
    animate_circle(offset1_s, 4, true);
    animate_circle(offset2_s, 2, false, offset1_s + duration_s, 2.0);

    auto animate_grow_circle = [&](double t_offset, int thickness_px)
    {
        const double local_t = anim_t - t_offset;
        if(local_t < 0.0 || local_t > grow_duration_s)
            return;
        const double f = local_t / grow_duration_s;
        const double base_r = thickness_px - 1;
        const double target_r = max_visible_r + thickness_px - 1;
        const double r = base_r + f * (target_r - base_r);
        const int ri = static_cast<int>(std::round(r));
        for(int t = 0; t < thickness_px; ++t)
        {
            const int rr = ri - t;
            if(rr <= 0)
                continue;
            for_circle_perimeter(rr, [&](int px, int py)
            {
                if(text_mask[py][px])
                    fb_buf[py][px] = !fb_buf[py][px];
                else
                    fb_buf[py][px] = !fb_buf[py][px];
            });
        }
    };
    animate_grow_circle(grow_start_s, 16);

    for(int y = 0; y < 64; ++y)
    {
        for(int x = 0; x < 128; ++x)
            d.DrawPixel(x, y, fb_buf[y][x] != 0);
    }
}

void Record_Render(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.display)
        return;

    AppUiState& ui = *ctx.ui;
    AppEngineState& engine = *ctx.engine;
    AppRecordingState& recording = *ctx.recording;
    AppProjectState& project = *ctx.project;
    AppDiagnosticsState& diag = *ctx.diag;
    AppSharedState& shared = *ctx.shared;
    AppWorkerState& worker = *ctx.worker;
    OledPager& d = *ctx.display;
    d.Fill(false);

    const UiLayout layout = UiLayout_Default();
    char status[16];
    BuildStatus(shared, status, sizeof(status));

    const uint32_t rec_len = shared.recording.rec_length.load(std::memory_order_acquire);
    const uint32_t rec_pos = shared.recording.rec_pos.load(std::memory_order_acquire);

    if(recording.record_state == RecordUiState::Countdown)
    {
        const uint32_t elapsed = ctx.now_ms - recording.record_countdown_start_ms;
        if(elapsed >= kRecordCountdownMs)
        {
            Record_StartRecording(ctx);
        }
    }

    if(recording.record_state == RecordUiState::SaveWait)
    {
        if(!ui.sd.save_in_progress && !worker.ui_req_busy)
        {
            const bool save_ok = (std::strncmp(ui.sd.save_status, "SAVED", 5) == 0);
            if(save_ok)
            {
                // Successful save: go back to source select (recording is no longer at risk).
                Record_StopPreview(recording);
                shared.recording.rec_monitor_enable.store(0, std::memory_order_release);
                recording.record_state = RecordUiState::SourceSelect;
            }
            else
            {
                // On failure, return to review so user can retry/save again.
                recording.record_state = RecordUiState::Review;
            }
            ui.ui_dirty = true;
        }
    }

    switch(recording.record_state)
    {
        case RecordUiState::SourceSelect:
        {
            UiDraw_Header(d, layout, "SOURCE", status);
            const int line_h = layout.line_h + 2;
            const int y0 = layout.y_body + 2;
            const char* names[2] = {"LINE IN", "MICROPHONE"};
            for(int i = 0; i < 2; ++i)
            {
                const int y = y0 + i * line_h;
                const bool sel = (static_cast<uint8_t>(i) == recording.record_source_index);
                if(sel)
                    d.DrawRect(0, y, 127, y + line_h - 1, true, true);
                d.SetCursor(2, y + 1);
                d.WriteString(names[i], Font_6x8, !sel);
            }
        }
        break;

        case RecordUiState::Armed:
        {
            Record_RenderReadyCuzStyle(ctx);
        }
        break;

        case RecordUiState::Countdown:
        {
            const uint32_t elapsed = ctx.now_ms - recording.record_countdown_start_ms;
            const uint32_t remain = (elapsed < kRecordCountdownMs) ? (kRecordCountdownMs - elapsed) : 0u;
            const uint32_t sec = (remain + 999u) / 1000u;
            const int cx = 64;
            const int cy = 32;
            const int outer_r = 30;
            const int inner_r = 22;

            DrawCirclePixels(d, cx, cy, outer_r, true);
            DrawCirclePixels(d, cx, cy, inner_r, true);
            d.DrawLine(cx, 0, cx, 63, true);
            d.DrawLine(0, cy, 127, cy, true);

            const float phase = static_cast<float>(elapsed % 1000u) / 1000.0f;
            const float angle = phase * 2.0f * 3.14159265f;
            const int hand_r = outer_r - 2;
            const int hx = cx + static_cast<int>(std::cos(angle) * hand_r);
            const int hy = cy + static_cast<int>(std::sin(angle) * hand_r);
            d.DrawLine(cx, cy, hx, hy, true);

            char big[8];
            std::snprintf(big, sizeof(big), "%lu", (unsigned long)sec);
            const int scale = 4;
            const int text_w = static_cast<int>(std::strlen(big)) * Font_6x8.FontWidth * scale;
            const int text_h = Font_6x8.FontHeight * scale;
            const int text_x = (128 - text_w) / 2;
            const int text_y = (64 - text_h) / 2;
            DrawScaledText6x8(d, big, text_x, text_y, scale);
        }
        break;

        case RecordUiState::Recording:
        {
            d.SetCursor(0, 0);
            d.WriteString("RECORDING - 5 SEC MAX", Font_6x8, true);

            const int wave_y0 = Font_6x8.FontHeight + 2;
            const int wave_y1 = 62;
            const int wave_h = wave_y1 - wave_y0 + 1;
            static float mist_level[128] = {};
            static uint32_t mist_seed = 0xA5B35791u;
            static uint32_t snap_gen = 0;
            static int16_t snap_min[128] = {};
            static int16_t snap_max[128] = {};
            const uint32_t live_gen = shared.recording.rec_live_gen.load(std::memory_order_acquire);
            if(live_gen != snap_gen)
            {
                uint32_t g0 = 0, g1 = 0;
                int retry = 0;
                do
                {
                    g0 = shared.recording.rec_live_gen.load(std::memory_order_acquire);
                    std::memcpy(snap_min, shared.recording.rec_live_min, sizeof(snap_min));
                    std::memcpy(snap_max, shared.recording.rec_live_max, sizeof(snap_max));
                    g1 = shared.recording.rec_live_gen.load(std::memory_order_acquire);
                } while(g0 != g1 && ++retry < 2);
                snap_gen = g1;
            }

            if(rec_pos == 0)
            {
                for(int x = 0; x < 128; ++x)
                    mist_level[x] = 0.0f;
            }

            const int px = static_cast<int>((static_cast<uint64_t>(rec_pos) * 127u) / kSdSampleMaxFrames);
            const int kTrail = 24; // slightly looser follow
            const float mic_boost = (recording.record_source_index == 1) ? 1.5f : 1.0f;
            for(int x = 0; x < 128; ++x)
            {
                int16_t minv = snap_min[x];
                int16_t maxv = snap_max[x];
                int16_t a0 = (minv < 0) ? static_cast<int16_t>(-minv) : minv;
                int16_t a1 = (maxv < 0) ? static_cast<int16_t>(-maxv) : maxv;
                int16_t absmax = (a1 > a0) ? a1 : a0;
                float near = 0.0f;
                const int dist = (x > px) ? (x - px) : (px - x);
                if(dist < kTrail)
                {
                    near = 1.0f - (static_cast<float>(dist) / static_cast<float>(kTrail));
                }
                const float amp = Clamp01((static_cast<float>(absmax) / 32767.0f) * mic_boost);
                const float envelope = 0.26f + (0.74f * near); // loose mist everywhere, strongest near playhead
                const float target = Clamp01(amp * envelope);
                float v = mist_level[x];
                const float rise = 0.97f;
                const float fall = 0.95f;
                v += (target > v) ? ((target - v) * rise) : ((target - v) * fall);
                mist_level[x] = v;
                const float spike_boost = 1.0f + (near * 0.35f); // largest peaks live around playhead on both sides
                int h = static_cast<int>(v * static_cast<float>(wave_h) * 1.35f * spike_boost + 0.5f);
                if(h < 0)
                    h = 0;
                if(h > wave_h)
                    h = wave_h;
                const int start = wave_y1 - h;
                for(int y = start; y <= wave_y1; ++y)
                {
                    mist_seed = mist_seed * 1664525u + 1013904223u;
                    const int frac = wave_y1 - y;
                    const int dens = 1 + (frac / 4);
                    if((mist_seed % static_cast<uint32_t>(dens)) == 0u)
                        d.DrawPixel(x, y, true);
                }
            }

            d.DrawLine(px, wave_y0, px, wave_y1, true);
        }
        break;

        case RecordUiState::Review:
        {
            UiDraw_Header(d, layout, "RECORDED PLAYBACK", status);
            const uint8_t slot = recording.record_slot & 1u;
            const Sample& s = shared.sample.publish.sd_slots[slot];
            const SampleEdit* e = (s.length > 0) ? &shared.sample.edit.sd_edit_slots[slot] : nullptr;
            DrawWaveformPreview(d, s, e, 0, layout.y_body, 128, 50);
            if(s.length == 0)
            {
                d.SetCursor(42, 34);
                d.WriteString("NO AUDIO", Font_6x8, true);
            }
        }
        break;

        case RecordUiState::TargetSelect:
        {
            UiDraw_Header(d, layout, "SAVE SAMPLE?", status);
            const char* a = "SAVE";
            const char* b = "RECORD AGAIN";
            const bool sa = (recording.record_target_index == 0);
            const bool sb = !sa;
            d.DrawRect(8, 20, 119, 31, true, sa);
            d.SetCursor(44, 22);
            d.WriteString(a, Font_6x8, !sa);
            d.DrawRect(8, 36, 119, 47, true, sb);
            d.SetCursor(24, 38);
            d.WriteString(b, Font_6x8, !sb);
        }
        break;

        case RecordUiState::BackConfirm:
        {
            UiDraw_Header(d, layout, "ARE YOU SURE?", status);
            d.SetCursor(16, 24);
            d.WriteString("REC WILL BE LOST", Font_6x8, true);
            UiDraw_Footer(d, layout, "R:YES  L:NO");
        }
        break;

        case RecordUiState::SaveWait:
        {
            d.Fill(false);
            if(ui.sd.save_in_progress)
            {
                d.SetCursor(0, 0);
                d.WriteString("SAVING", Font_6x8, true);
                const int bar_w = 96;
                const int bar_h = 6;
                const int bar_x = (128 - bar_w) / 2;
                const int bar_y = Font_6x8.FontHeight + 16;
                const int pct = static_cast<int>(ui.sd.save_progress);
                const int fill_w = (pct <= 0) ? 0 : ((pct >= 100) ? bar_w : ((bar_w * pct) / 100));
                d.DrawRect(bar_x, bar_y, bar_x + bar_w, bar_y + bar_h, true, false);
                if(fill_w > 0)
                    d.DrawRect(bar_x + 1, bar_y + 1, bar_x + fill_w - 1, bar_y + bar_h - 1, true, true);
            }
            else
            {
                const bool ok = (std::strncmp(ui.sd.save_status, "SAVED", 5) == 0);
                d.SetCursor(0, 0);
                d.WriteString(ok ? "SAVE OK" : "SAVE FAILED", Font_6x8, true);
                if(ok && ui.sd.save_name[0] != '\0')
                {
                    d.SetCursor(0, Font_6x8.FontHeight + 2);
                    d.WriteString(ui.sd.save_name, Font_6x8, true);
                }
            }
        }
        break;
    }

    (void)rec_len;
}
