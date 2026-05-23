#include "ui_screens_internal.h"

#include "app_state_ui.h"
#include "app_state_engine.h"
#include "app_state_recording.h"
#include "app_state_project.h"
#include "app_state_diagnostics.h"
#include "app_state_shared.h"
#include "app_state_worker.h"
#include "oled_pager.h"
#include "sample_edit.h"
#include "ui_input.h"

namespace
{
bool WaveEditIsRenderReview(const AppUiState& ui)
{
    return ui.wave_edit_source == WaveEditSource::RenderReview;
}

void StartTrimPreview(AppUiState& ui)
{
    ui.ui_trim_preview_hold = true;
}

void StopTrimPreview(AppUiState& ui)
{
    ui.ui_trim_preview_hold = false;
}

const Sample& WaveEditActiveSample(AppUiState& ui, AppEngineState& engine, AppSharedState& shared)
{
    if(WaveEditIsRenderReview(ui))
        return shared.recording.rec_sample;

    const uint8_t layer = engine.perform_nav.perform_layer & 1u;
    shared.sample.publish.sd_current_slot.store(layer, std::memory_order_release);
    return shared.sample.publish.sd_slots[layer];
}

SampleEdit WaveEditActiveEdit(const AppUiState& ui,
                              const AppEngineState& engine,
                              const AppSharedState& shared)
{
    if(WaveEditIsRenderReview(ui))
        return shared.recording.rec_edit;

    const uint8_t layer = engine.perform_nav.perform_layer & 1u;
    return shared.sample.edit.sd_edit_slots[layer];
}

void WaveEditWriteEdit(const AppUiState& ui,
                       const AppEngineState& engine,
                       AppSharedState& shared,
                       const SampleEdit& edit)
{
    if(WaveEditIsRenderReview(ui))
    {
        shared.recording.rec_edit = edit;
        return;
    }

    const uint8_t layer = engine.perform_nav.perform_layer & 1u;
    shared.sample.edit.sd_edit_slots[layer] = edit;
}
} // namespace

void PerformWaveEdit_Render(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.display)
        return;

    OledPager& d = *ctx.display;
    d.Fill(false);

    AppUiState& ui = *ctx.ui;
    AppEngineState& engine = *ctx.engine;
    AppDiagnosticsState& diag = *ctx.diag;
    AppSharedState& shared = *ctx.shared;
    if(!WaveEditIsRenderReview(ui))
        EngineRefreshLoadedMetadata(ui, engine, shared);
    const uint8_t layer = engine.perform_nav.perform_layer & 1u;
    const Sample& sample = WaveEditActiveSample(ui, engine, shared);
    const bool sample_loaded = (sample.pcm != nullptr && sample.length > 0);
    SampleEdit edit = WaveEditActiveEdit(ui, engine, shared);
    SampleEdit_Clamp(edit, sample.length);

    const int wave_x = 0;
    const int wave_y = 0;
    const int wave_w = 128;
    const int wave_h = 64;
    const int x0 = wave_x;
    const int y0 = wave_y;
    const int x1 = wave_x + wave_w - 1;
    const int y1 = wave_y + wave_h - 1;

    if(!sample_loaded)
    {
        DrawTinyString(d, "no sample", 2, 2, true);
        return;
    }

    if(wave_w >= 3 && wave_h >= 3)
    {
        const int preview_bottom_y = y1 - (kMini3x5H + 3);
        d.DrawRect(x0, y0, x1, preview_bottom_y - 1, true, false);
        const int waveform_y0 = y0 + 1;
        const int waveform_y1 = preview_bottom_y - 1;
        if(waveform_y1 <= waveform_y0)
            return;

        const uint32_t frames = sample.length;
        const uint32_t denom = (frames > 1) ? (frames - 1) : 1;
        int start_x = x0 + static_cast<int>((static_cast<uint64_t>(edit.start_frame) * (wave_w - 1)) / denom);
        int end_x = x0 + static_cast<int>((static_cast<uint64_t>(edit.end_frame) * (wave_w - 1)) / denom);
        if(end_x < start_x)
        {
            const int t = start_x;
            start_x = end_x;
            end_x = t;
        }
        if(start_x < x0) start_x = x0;
        if(end_x > x1) end_x = x1;

        const int waveform_h = waveform_y1 - waveform_y0 + 1;
        const int mid = waveform_y0 + waveform_h / 2;
        const int amp_h = (waveform_h - 1) / 2;

        // Invert selected trim window.
        d.DrawRect(start_x, waveform_y0, end_x, waveform_y1, true, true);

        const int draw_w = wave_w - 2;
        const uint32_t total = sample.length;
        for(int px = 0; px < draw_w; ++px)
        {
            const uint32_t seg0 = (static_cast<uint64_t>(total) * static_cast<uint32_t>(px)) / draw_w;
            uint32_t seg1 = (static_cast<uint64_t>(total) * static_cast<uint32_t>(px + 1)) / draw_w;
            if(seg1 <= seg0)
                seg1 = seg0 + 1;
            if(seg1 > total)
                seg1 = total;

            int16_t mn = 32767;
            int16_t mx = -32768;
            for(uint32_t i = seg0; i < seg1; ++i)
            {
                const int16_t v = sample.pcm[i];
                if(v < mn)
                    mn = v;
                if(v > mx)
                    mx = v;
            }

            int top = mid - (static_cast<int>(mx) * amp_h) / 32768;
            int bot = mid - (static_cast<int>(mn) * amp_h) / 32768;
            if(top < waveform_y0) top = waveform_y0;
            if(bot > waveform_y1) bot = waveform_y1;
            if(bot < top) bot = top;

            const int xx = x0 + 1 + px;
            const bool inside = (xx >= start_x && xx <= end_x);
            if(inside)
            {
                d.DrawLine(xx, top, xx, bot, false);
            }
            else
            {
                for(int yy = top; yy <= bot; ++yy)
                {
                    if((yy & 1) == 0)
                        d.DrawPixel(xx, yy, true);
                }
            }
        }

        d.DrawLine(start_x, waveform_y0, start_x, waveform_y1, true);
        d.DrawLine(end_x, waveform_y0, end_x, waveform_y1, true);

        // Dotted box for START/END region (R_SHIFT hint), while outer waveform frame stays solid.
        DrawDottedRect(d, x0 + 1, preview_bottom_y, x1 - 1, y1 - 1, true);

        const int start_w = MiniString3x5Width("start");
        const int end_w = MiniString3x5Width("end");
        const int label_y = preview_bottom_y + 2;
        const int min_gap = 1;
        const int start_min_x = x0 + 1;
        const int end_min_x = x0 + 1;
        const int start_max_x = x1 - start_w;
        const int end_max_x = x1 - end_w;

        int start_label_x = start_x - (start_w / 2);
        int end_label_x = end_x - (end_w / 2);

        if(start_label_x < start_min_x) start_label_x = start_min_x;
        if(start_label_x > start_max_x) start_label_x = start_max_x;
        if(end_label_x < end_min_x) end_label_x = end_min_x;
        if(end_label_x > end_max_x) end_label_x = end_max_x;

        // Keep labels disjoint even when trim lines get very close.
        if(start_label_x + start_w + min_gap > end_label_x)
        {
            const int overlap = (start_label_x + start_w + min_gap) - end_label_x;
            start_label_x -= (overlap + 1) / 2;
            end_label_x += overlap / 2;

            if(start_label_x < start_min_x) start_label_x = start_min_x;
            if(end_label_x > end_max_x) end_label_x = end_max_x;

            if(start_label_x + start_w + min_gap > end_label_x)
            {
                end_label_x = start_label_x + start_w + min_gap;
                if(end_label_x > end_max_x)
                {
                    end_label_x = end_max_x;
                    start_label_x = end_label_x - start_w - min_gap;
                    if(start_label_x < start_min_x)
                        start_label_x = start_min_x;
                }
            }
        }

        DrawMiniString3x5(d, "start", start_label_x, label_y, true);
        DrawMiniString3x5(d, "end", end_label_x, label_y, true);

        const uint32_t ph_active = WaveEditIsRenderReview(ui)
                                       ? shared.recording.preview_active.load(std::memory_order_relaxed)
                                       : diag.playhead_active[layer].load(std::memory_order_relaxed);
        if(ph_active != 0u)
        {
            const uint32_t ph_frame = WaveEditIsRenderReview(ui)
                                          ? shared.recording.preview_pos.load(std::memory_order_relaxed)
                                          : diag.playhead_frame[layer].load(std::memory_order_relaxed);
            const uint32_t ph = (ph_frame >= frames) ? (frames - 1) : ph_frame;
            const int play_x = x0 + static_cast<int>((static_cast<uint64_t>(ph) * (wave_w - 1)) / denom);
            d.DrawLine(play_x, waveform_y0, play_x, waveform_y1, true);
        }
    }

}

void PerformWaveEdit_OnScreenEnter(UiScreenCtx& ctx)
{
    if(!ctx.ui)
        return;

    AppUiState& ui = *ctx.ui;
    AppEngineState& engine = *ctx.engine;
    AppSharedState& shared = *ctx.shared;
    StopTrimPreview(ui);
    ui.ui_trim_preview_gate = false;
    if(WaveEditIsRenderReview(ui))
    {
        SampleEdit edit = shared.recording.rec_edit;
        SampleEdit_Clamp(edit, shared.recording.rec_sample.length);
        shared.recording.rec_edit = edit;
        ui.render_review_trim_entry = edit;
        ui.render_review_trim_has_entry = true;
    }
    else
    {
        for(uint8_t slot = 0; slot < kSdSampleSlots; ++slot)
            engine.wave_edit.perform_wave_edit_entry[slot] = shared.sample.edit.sd_edit_slots[slot];
        engine.wave_edit.perform_wave_edit_has_entry = true;
    }
    ui.ui_dirty = true;
}

bool PerformWaveEdit_OnEnter(UiScreenCtx& ctx)
{
    if(!ctx.ui)
        return false;

    AppUiState& ui = *ctx.ui;
    AppEngineState& engine = *ctx.engine;
    AppSharedState& shared = *ctx.shared;
    StopTrimPreview(ui);
    ui.ui_trim_preview_gate = false;
    if(WaveEditIsRenderReview(ui))
    {
        SampleEdit edit = shared.recording.rec_edit;
        const Sample& sample = shared.recording.rec_sample;
        SampleEdit_Clamp(edit, sample.length);
        shared.recording.rec_edit = edit;
        ui.render_review_trim_has_entry = false;
    }
    else
    {
        const uint8_t layer = engine.perform_nav.perform_layer & 1u;
        SampleEdit edit = shared.sample.edit.sd_edit_slots[layer];
        const Sample& sample = shared.sample.publish.sd_slots[layer];
        SampleEdit_Clamp(edit, sample.length);
        shared.sample.edit.sd_edit_slots[layer] = edit;
        shared.sample.edit.sd_edit_pending = edit;
        shared.sample.edit.sd_edit_slot.store(layer, std::memory_order_release);
        shared.sample.edit.sd_edit_gen.fetch_add(1, std::memory_order_acq_rel);
        shared.sample.edit.sd_edit_ready.store(1, std::memory_order_release);
        engine.wave_edit.perform_wave_edit_has_entry = false;
    }
    UiNav_Pop(ui.ui_nav);
    ui.ui_dirty = true;
    return true;
}

bool PerformWaveEdit_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.ui)
        return false;

    AppUiState& ui = *ctx.ui;
    AppEngineState& engine = *ctx.engine;
    AppSharedState& shared = *ctx.shared;
    const bool render_review = WaveEditIsRenderReview(ui);
    const uint8_t layer = engine.perform_nav.perform_layer & 1u;
    Sample& sample = render_review ? shared.recording.rec_sample : shared.sample.publish.sd_slots[layer];
    if(!render_review)
        shared.sample.publish.sd_current_slot.store(layer, std::memory_order_release);
    if(sample.pcm == nullptr || sample.length == 0)
        return false;

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPod2)
    {
        if(ctx.rshift && !render_review)
        {
            StopTrimPreview(ui);
            engine.perform_nav.perform_layer ^= 1u;
            const uint8_t next = engine.perform_nav.perform_layer & 1u;
            shared.sample.publish.sd_current_slot.store(next, std::memory_order_release);
        }
        else
        {
            StartTrimPreview(ui);
        }
        ui.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::BtnUp && e.id == kUiBtnPod2)
    {
        StopTrimPreview(ui);
        ui.ui_dirty = true;
        return true;
    }

    // Cancel trim edit session: restore entry snapshot and return.
    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPodEnc)
    {
        StopTrimPreview(ui);
        ui.ui_trim_preview_gate = false;
        if(render_review)
        {
            if(ui.render_review_trim_has_entry)
            {
                shared.recording.rec_edit = ui.render_review_trim_entry;
                SampleEdit_Clamp(shared.recording.rec_edit, shared.recording.rec_sample.length);
                ui.render_review_trim_has_entry = false;
            }
        }
        else if(engine.wave_edit.perform_wave_edit_has_entry)
        {
            for(uint8_t slot = 0; slot < kSdSampleSlots; ++slot)
                shared.sample.edit.sd_edit_slots[slot] = engine.wave_edit.perform_wave_edit_entry[slot];
            engine.wave_edit.perform_wave_edit_has_entry = false;
        }
        UiNav_Pop(ui.ui_nav);
        ui.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::EncDelta
       && (e.id == kUiEncPod || e.id == kUiEncExt)
       && e.value != 0)
    {
        SampleEdit edit = WaveEditActiveEdit(ui, engine, shared);
        SampleEdit_Clamp(edit, sample.length);

        const uint32_t frames = sample.length;
        if(frames < 2u)
            return false;
        const float denom = static_cast<float>(frames);
        float trim_start = static_cast<float>(edit.start_frame) / denom;
        float trim_end = static_cast<float>(edit.end_frame) / denom;

        const int32_t start_delta = (e.id == kUiEncPod) ? e.value : 0;
        const int32_t end_delta = (e.id == kUiEncExt) ? e.value : 0;
        const float base_step = ctx.rshift ? (1.0f / 64.0f) : (1.0f / 32.0f);
        auto step = [&](int d)
        {
            int mag = (d < 0) ? -d : d;
            if(mag < 1)
                mag = 1;
            int log2 = 0;
            while(mag > 1)
            {
                mag >>= 1;
                ++log2;
            }
            return base_step * static_cast<float>(1 << log2);
        };

        if(start_delta != 0)
            trim_start += static_cast<float>(start_delta) * step(start_delta);
        if(end_delta != 0)
            trim_end += static_cast<float>(end_delta) * step(end_delta);

        if(trim_start < 0.0f)
            trim_start = 0.0f;
        if(trim_end > 1.0f)
            trim_end = 1.0f;
        const float min_norm = 2.0f / static_cast<float>(frames);
        if((trim_end - trim_start) < min_norm)
        {
            trim_end = trim_start + min_norm;
            if(trim_end > 1.0f)
            {
                trim_end = 1.0f;
                trim_start = trim_end - min_norm;
            }
        }

        uint32_t start_frame = static_cast<uint32_t>(trim_start * static_cast<float>(frames));
        uint32_t end_frame = static_cast<uint32_t>(trim_end * static_cast<float>(frames));
        if(end_frame <= start_frame)
            end_frame = start_frame + 2u;
        if(end_frame > frames)
            end_frame = frames;
        if(start_frame >= end_frame)
            start_frame = (end_frame > 0u) ? (end_frame - 1u) : 0u;
        edit.start_frame = start_frame;
        edit.end_frame = end_frame;

        SampleEdit_Clamp(edit, frames);
        WaveEditWriteEdit(ui, engine, shared, edit);
        ui.ui_dirty = true;
        return true;
    }

    return false;
}
