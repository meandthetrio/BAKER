#include "ui_screens_internal.h"

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
#include "sample_edit.h"
#include "ui_input.h"
#include "ui_layout.h"

static void DrawPlus5x7(OledPager& d, int x, int y, bool on)
{
    // 5x7 plus centered in the same cell width used by DrawTinyString.
    for(int yy = 1; yy <= 5; ++yy)
        d.DrawPixel(x + 2, y + yy, on);
    for(int xx = 0; xx < 5; ++xx)
        d.DrawPixel(x + xx, y + 3, on);
}

static int SignedSemitoneTextWidth(int v)
{
    if(v > 0)
    {
        char digits[8];
        std::snprintf(digits, sizeof(digits), "%d", v);
        return 6 + TinyStringWidth(digits); // '+' cell (6px advance) + digits
    }

    char buf[8];
    std::snprintf(buf, sizeof(buf), "%d", v);
    return TinyStringWidth(buf);
}

static void DrawSignedSemitoneText(OledPager& d, int v, int x, int y, bool on)
{
    if(v > 0)
    {
        char digits[8];
        std::snprintf(digits, sizeof(digits), "%d", v);
        DrawPlus5x7(d, x, y, on);
        DrawTinyString(d, digits, x + 6, y, on);
        return;
    }

    char buf[8];
    std::snprintf(buf, sizeof(buf), "%d", v);
    DrawTinyString(d, buf, x, y, on);
}

static int DivFloor(int n, int d)
{
    int q = n / d;
    int r = n % d;
    if(r != 0 && ((r > 0) != (d > 0)))
        --q;
    return q;
}

static int ModFloor(int n, int d)
{
    int r = n % d;
    if(r < 0)
        r += (d < 0) ? -d : d;
    return r;
}

static int SignedTuneWithCentsTextWidth(int total_cents)
{
    if(total_cents == 0)
        return TinyStringWidth("0");

    const int abs_cents = (total_cents < 0) ? -total_cents : total_cents;
    const int semis = abs_cents / 100;
    const int cents = abs_cents % 100;
    char buf[12];
    std::snprintf(buf, sizeof(buf), "+%d.%02d", semis, cents);
    return TinyStringWidth(buf);
}

static void DrawSignedTuneWithCentsText(OledPager& d, int total_cents, int x, int y, bool on)
{
    if(total_cents == 0)
    {
        DrawTinyString(d, "0", x, y, on);
        return;
    }

    const int abs_cents = (total_cents < 0) ? -total_cents : total_cents;
    const int semis = abs_cents / 100;
    const int cents = abs_cents % 100;
    char buf[12];
    std::snprintf(buf, sizeof(buf), "%c%d.%02d", (total_cents < 0) ? '-' : '+', semis, cents);
    DrawTinyString(d, buf, x, y, on);
}

static void ClampAndSplitTune(int total_cents, int8_t& semitones, int8_t& cents)
{
    int clamped_total = total_cents;
    if(clamped_total < -2400)
        clamped_total = -2400;
    if(clamped_total > 2400)
        clamped_total = 2400;
    const int semis = DivFloor(clamped_total, 100);
    const int rem = ModFloor(clamped_total, 100);
    if(clamped_total < 0 && rem != 0)
    {
        semitones = static_cast<int8_t>(semis + 1);
        cents = static_cast<int8_t>(rem - 100);
        return;
    }
    semitones = static_cast<int8_t>(semis);
    cents = static_cast<int8_t>(rem);
}

static int LoadWordmarkWidth()
{
    return MicroStringWidth("load");
}

static void DrawLoadWordmark(OledPager& d, int x, int y, bool on)
{
    DrawMicroString(d, "load", x, y, on);
}

static int TuneWordmarkWidth()
{
    return MicroStringWidth("tune");
}

static int ReverseWordmarkWidth()
{
    return MicroStringWidth("rev");
}

static void DrawReverseWordmark(OledPager& d, int x, int y, bool on)
{
    DrawMicroString(d, "rev", x, y, on);
}

static void DrawTuneWordmark(OledPager& d, int x, int y, bool on)
{
    DrawMicroString(d, "tune", x, y, on);
}

static void ToLowerCase(const char* in, char* out, size_t out_sz)
{
    if(out_sz == 0)
        return;
    out[0] = '\0';
    if(!in)
        return;

    size_t j = 0;
    for(size_t i = 0; in[i] != '\0' && j + 1 < out_sz; ++i)
    {
        char c = in[i];
        if(c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
        out[j++] = c;
    }
    out[j] = '\0';
}

static constexpr uint8_t kPerformLayerCount = 2;

static int ClampInt(int v, int lo, int hi)
{
    if(v < lo)
        return lo;
    if(v > hi)
        return hi;
    return v;
}

static constexpr int32_t kEngineRowCount = 4;
static constexpr int32_t kEngineRowWave = 0;
static constexpr int32_t kEngineRowLoad = 1;
static constexpr int32_t kEngineRowReverse = 2;
static constexpr int32_t kEngineRowTune = 3;

static bool ReverseLoadedSampleForLayer(AppSharedState& shared, uint8_t layer)
{
    layer &= 1u;
    Sample& sample = shared.sample.publish.sd_slots[layer];
    if(sample.pcm == nullptr || sample.length < 2)
        return false;

    int16_t* pcm = const_cast<int16_t*>(sample.pcm);
    uint32_t i = 0;
    uint32_t j = sample.length - 1u;
    while(i < j)
    {
        const int16_t t = pcm[i];
        pcm[i] = pcm[j];
        pcm[j] = t;
        ++i;
        --j;
    }

    auto map_index = [&](uint32_t idx) -> uint32_t
    {
        if(idx > sample.length)
            idx = sample.length;
        return sample.length - idx;
    };

    uint32_t new_loop_start = map_index(sample.loop_end);
    uint32_t new_loop_end = map_index(sample.loop_start);
    if(new_loop_start > new_loop_end)
    {
        const uint32_t t = new_loop_start;
        new_loop_start = new_loop_end;
        new_loop_end = t;
    }
    sample.loop_start = new_loop_start;
    sample.loop_end = new_loop_end;

    SampleEdit& edit = shared.sample.edit.sd_edit_slots[layer];
    uint32_t new_edit_start = map_index(edit.start_frame);
    uint32_t new_edit_end = map_index(edit.end_frame);
    if(new_edit_start > new_edit_end)
    {
        const uint32_t t = new_edit_start;
        new_edit_start = new_edit_end;
        new_edit_end = t;
    }
    edit.start_frame = new_edit_start;
    edit.end_frame = new_edit_end;

    uint32_t new_edit_loop_start = map_index(edit.loop_end);
    uint32_t new_edit_loop_end = map_index(edit.loop_start);
    if(new_edit_loop_start > new_edit_loop_end)
    {
        const uint32_t t = new_edit_loop_start;
        new_edit_loop_start = new_edit_loop_end;
        new_edit_loop_end = t;
    }
    edit.loop_start = new_edit_loop_start;
    edit.loop_end = new_edit_loop_end;
    if(edit.loop_start > edit.end_frame)
        edit.loop_start = edit.end_frame;
    if(edit.loop_end > edit.end_frame)
        edit.loop_end = edit.end_frame;
    if(edit.loop_start < edit.start_frame)
        edit.loop_start = edit.start_frame;
    if(edit.loop_end < edit.start_frame)
        edit.loop_end = edit.start_frame;

    return true;
}

void PerformEngine_OnScreenEnter(UiScreenCtx& ctx)
{
    if(!ctx.ui)
        return;

    EngineRefreshLoadedMetadata(*ctx.ui, *ctx.engine, *ctx.shared);
    const bool pending_engine_load = ctx.engine->layer.engine_load_from_perform
                                     && (ctx.engine->layer.engine_load_target_layer < kPerformLayerCount);
    const uint8_t layer = ctx.engine->perform_nav.perform_layer & 1u;
    if(!pending_engine_load)
        ctx.shared->sample.publish.sd_current_slot.store(layer, std::memory_order_release);

    ctx.engine->layer.engine_load_from_perform = false;
    ctx.engine->layer.engine_load_target_layer = 0xFFu;
    ctx.engine->perform_nav.perform_engine_row = static_cast<uint8_t>(kEngineRowLoad);
    PublishEngineLayerParams(ctx);
    ctx.ui->ui_dirty = true;
}

bool PerformEngine_OnEnter(UiScreenCtx& ctx)
{
    if(!ctx.ui)
        return false;

    AppEngineState& engine = *ctx.engine;
    AppSharedState& shared = *ctx.shared;
    const uint8_t row = ctx.engine->perform_nav.perform_engine_row % static_cast<uint8_t>(kEngineRowCount);
    if(row == kEngineRowWave)
        return UiNav_Push(ctx.ui->ui_nav, UiScreenId::PerformWaveEdit);
    if(row == kEngineRowReverse)
    {
        const uint8_t layer = engine.perform_nav.perform_layer & 1u;
        if(ReverseLoadedSampleForLayer(shared, layer))
        {
            PublishEngineLayerParams(ctx);
            ctx.ui->ui_dirty = true;
        }
        return true;
    }
    if(row != kEngineRowLoad)
        return false;

    ctx.engine->layer.engine_load_target_layer = engine.perform_nav.perform_layer & 1u;
    ctx.engine->layer.engine_load_from_perform = true;
    return UiNav_Push(ctx.ui->ui_nav, UiScreenId::SdBrowse);
}

bool PerformEngine_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.ui)
        return false;
    if(ctx.lshift)
        return false;

    AppUiState& ui = *ctx.ui;
    AppEngineState& engine = *ctx.engine;
    AppSharedState& shared = *ctx.shared;
    EngineRefreshLoadedMetadata(ui, engine, shared);

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPod2)
    {
        engine.perform_nav.perform_layer ^= 1u;
        const uint8_t layer = engine.perform_nav.perform_layer & 1u;
        shared.sample.publish.sd_current_slot.store(layer, std::memory_order_release);
        engine.layer.engine_header_invert_until_ms = e.t_ms + 250u;
        PublishEngineLayerParams(ctx);
        ui.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod && e.value != 0)
    {
        int row = static_cast<int>(engine.perform_nav.perform_engine_row);
        row += e.value;
        while(row < 0)
            row += kEngineRowCount;
        while(row >= kEngineRowCount)
            row -= kEngineRowCount;
        engine.perform_nav.perform_engine_row = static_cast<uint8_t>(row);
        ui.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt && e.value != 0)
    {
        const uint8_t layer = engine.perform_nav.perform_layer & 1u;
        const uint8_t row = engine.perform_nav.perform_engine_row % static_cast<uint8_t>(kEngineRowCount);
        bool changed = false;
        if(row == kEngineRowTune)
        {
            if(ctx.rshift)
            {
                const int total_cents = static_cast<int>(engine.layer.engine_tune_semitones[layer]) * 100
                                        + static_cast<int>(engine.layer.engine_tune_cents[layer])
                                        + e.value;
                int8_t new_semitones = engine.layer.engine_tune_semitones[layer];
                int8_t new_cents = engine.layer.engine_tune_cents[layer];
                ClampAndSplitTune(total_cents, new_semitones, new_cents);
                if(new_semitones != engine.layer.engine_tune_semitones[layer]
                   || new_cents != engine.layer.engine_tune_cents[layer])
                {
                    engine.layer.engine_tune_semitones[layer] = new_semitones;
                    engine.layer.engine_tune_cents[layer] = new_cents;
                    changed = true;
                }
            }
            else
            {
                int v = static_cast<int>(engine.layer.engine_tune_semitones[layer]) + e.value;
                v = ClampInt(v, -24, 24);
                const int8_t vv = static_cast<int8_t>(v);
                if(vv != engine.layer.engine_tune_semitones[layer])
                {
                    engine.layer.engine_tune_semitones[layer] = vv;
                    changed = true;
                }
            }
        }

        if(changed)
        {
            PublishEngineLayerParams(ctx);
            ui.ui_dirty = true;
            return true;
        }
    }

    return false;
}

void PerformEngine_Render(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.display)
        return;

    AppUiState& ui = *ctx.ui;
    AppEngineState& engine = *ctx.engine;
    AppSharedState& shared = *ctx.shared;
    EngineRefreshLoadedMetadata(ui, engine, shared);

    OledPager& d = *ctx.display;
    d.Fill(false);

    const uint8_t layer = engine.perform_nav.perform_layer & 1u;
    const Sample& sample = shared.sample.publish.sd_slots[layer];
    const bool sample_loaded = (sample.pcm != nullptr && sample.length > 0);
    const SampleEdit* edit = sample_loaded ? &shared.sample.edit.sd_edit_slots[layer] : nullptr;

    const UiLayout layout = UiLayout_Default();
    char header_label[16] = {};
    std::snprintf(header_label, sizeof(header_label), "engine %c", layer == 0 ? 'a' : 'b');
    const int header_w = MicroStringWidth(header_label);
    const int box_w = header_w + 4;
    const int box_h = kMicroH + 4;
    int box_x = 128 - box_w;
    if(box_x < 0)
        box_x = 0;
    const bool header_invert_flash
        = static_cast<int32_t>(engine.layer.engine_header_invert_until_ms - ctx.now_ms) > 0;
    if(header_invert_flash)
    {
        // Inverted phase: dark fill with white text/border.
        d.DrawRect(box_x, 0, box_x + box_w - 1, box_h - 1, false, true);
        d.DrawRect(box_x, 0, box_x + box_w - 1, box_h - 1, true, false);
        DrawMicroString(d, header_label, box_x + 2, 2, true);
    }
    else
    {
        // Default phase: filled white box with dark text.
        d.DrawRect(box_x, 0, box_x + box_w - 1, box_h - 1, true, true);
        DrawMicroString(d, header_label, box_x + 2, 2, false);
    }

    constexpr int kTopTextX = 2;
    constexpr int kTopTextY = 2;
    const int top_text_bottom_y = kTopTextY + Font5x7::H - 1;
    if(!sample_loaded)
    {
        DrawTinyString(d, "no sample", kTopTextX, kTopTextY, true);
    }
    else
    {
        const char* name = engine.layer.engine_sample_name[layer];
        if(name == nullptr)
            name = "";
        char name_buf[40];
        ToLowerCase(name, name_buf, sizeof(name_buf));
        char clipped[40];
        clipped[0] = '\0';
        const int max_name_w = box_x - kTopTextX - 1; // keep clear of the header box.
        if(max_name_w > 0)
        {
            const int char_advance = Font5x7::W + 1;
            const int max_chars = (max_name_w + 1) / char_advance;
            int i = 0;
            for(; name_buf[i] != '\0' && i < max_chars && i + 1 < static_cast<int>(sizeof(clipped)); ++i)
                clipped[i] = name_buf[i];
            clipped[i] = '\0';
        }
        DrawTinyString(d, clipped, kTopTextX, kTopTextY, true);
    }

    constexpr int kWaveX = 0;
    const int kWaveY = top_text_bottom_y + 2;
    constexpr int kWaveW = 128;
    constexpr int kScreenH = 64;
    int kWaveBottomY = layout.y_footer - 9;
    if(kWaveBottomY < kWaveY)
        kWaveBottomY = kWaveY;
    const int kWaveH = kWaveBottomY - kWaveY + 1;

    const int footer_region_top = kWaveBottomY + 1;
    const int footer_region_bottom = kScreenH - 1;
    const int footer_region_h = footer_region_bottom - footer_region_top + 1;
    int kFooterY = footer_region_top;
    if(footer_region_h > Font5x7::H)
        kFooterY += (footer_region_h - Font5x7::H) / 2;

    const uint8_t row = engine.perform_nav.perform_engine_row % static_cast<uint8_t>(kEngineRowCount);
    if(ui.sd.sd_wav_load_busy)
    {
        d.DrawRect(kWaveX, kWaveY, kWaveX + kWaveW - 1, kWaveY + kWaveH - 1, true, false);
        if(row == kEngineRowWave)
            d.DrawRect(kWaveX, kWaveY, kWaveX + kWaveW - 1, kWaveY + kWaveH - 1, true, true);
    }
    else
    {
        DrawWaveformPreview(d, sample, edit, kWaveX, kWaveY, kWaveW, kWaveH, true);
        if(row == kEngineRowWave)
        {
            // Invert full waveform preview region to signal enterable deep menu.
            d.DrawRect(kWaveX, kWaveY, kWaveX + kWaveW - 1, kWaveY + kWaveH - 1, true, true);
            DrawWaveformPreview(d, sample, edit, kWaveX, kWaveY, kWaveW, kWaveH, false);
        }
    }

    const int load_w = LoadWordmarkWidth();
    const int reverse_w = ReverseWordmarkWidth();
    const int tune_w = TuneWordmarkWidth();
    constexpr int kScreenW = 128;
    constexpr int kThirdW = kScreenW / 3;
    const int load_anchor_x = kThirdW / 2;
    const int reverse_anchor_x = kThirdW + (kThirdW / 2);
    const int tune_anchor_x = (2 * kThirdW) + (kThirdW / 2);
    int load_x = load_anchor_x - (load_w / 2);
    int reverse_x = reverse_anchor_x - (reverse_w / 2);
    int tune_x = tune_anchor_x - (tune_w / 2);
    const int load_min_x = 1;
    const int load_max_x = (kThirdW - 1) - load_w;
    const int reverse_min_x = kThirdW + 1;
    const int reverse_max_x = (2 * kThirdW - 1) - reverse_w;
    const int tune_min_x = (2 * kThirdW) + 1;
    const int tune_max_x = (kScreenW - 1) - tune_w;
    if(load_x < load_min_x)
        load_x = load_min_x;
    if(load_x > load_max_x)
        load_x = load_max_x;
    if(reverse_x < reverse_min_x)
        reverse_x = reverse_min_x;
    if(reverse_x > reverse_max_x)
        reverse_x = reverse_max_x;
    if(tune_x < tune_min_x)
        tune_x = tune_min_x;
    if(tune_x > tune_max_x)
        tune_x = tune_max_x;

    if(row == kEngineRowLoad)
    {
        d.DrawRect(load_x - 1, kFooterY - 1, load_x + load_w, kFooterY + kMicroH, true, true);
        DrawLoadWordmark(d, load_x, kFooterY, false);
    }
    else
    {
        DrawLoadWordmark(d, load_x, kFooterY, true);
    }

    if(row == kEngineRowReverse)
    {
        d.DrawRect(reverse_x - 1, kFooterY - 1, reverse_x + reverse_w, kFooterY + kMicroH, true, true);
        DrawReverseWordmark(d, reverse_x, kFooterY, false);
    }
    else
    {
        DrawReverseWordmark(d, reverse_x, kFooterY, true);
    }

    if(row == kEngineRowTune)
    {
        const int semitone_value = static_cast<int>(engine.layer.engine_tune_semitones[layer]);
        const int cents_value = static_cast<int>(engine.layer.engine_tune_cents[layer]);
        const int total_cents = semitone_value * 100 + cents_value;
        const int val_w = ctx.rshift ? SignedTuneWithCentsTextWidth(total_cents)
                                     : SignedSemitoneTextWidth(semitone_value);
        int val_x = tune_x + (tune_w - val_w) / 2;
        if(val_x < tune_x)
            val_x = tune_x;

        const int by0 = kFooterY - 2;
        const int by1 = kFooterY + Font5x7::H + 1;
        if(ctx.rshift)
        {
            const int bx0 = val_x - 2;
            const int bx1 = val_x + val_w + 1;
            d.DrawRect(bx0, by0, bx1, by1, true, false);
        }
        else
        {
            const int bx0 = tune_x - 2;
            const int bx1 = tune_x + tune_w + 1;
            DrawDottedRect(d, bx0, by0, bx1, by1, true);
        }

        if(ctx.rshift)
            DrawSignedTuneWithCentsText(d, total_cents, val_x, kFooterY, true);
        else
            DrawSignedSemitoneText(d, semitone_value, val_x, kFooterY, true);
    }
    else
    {
        DrawTuneWordmark(d, tune_x, kFooterY, true);
    }
}
