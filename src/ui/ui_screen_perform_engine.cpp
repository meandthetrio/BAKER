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

static int LoadWordmarkWidth()
{
    // 5 + 1 + 3 + 1 + 3 + 1 + 4
    return 18;
}

static void DrawLoadWordmark(OledPager& d, int x, int y, bool on)
{
    constexpr int l_x = 0;   // width 5 (0..4)
    constexpr int o_x = 6;   // width 3 (6..8), 1px gap after L
    constexpr int a_x = 10;  // width 3 (10..12), 1px gap after o
    constexpr int d_x = 14;  // width 4 (14..17), 1px gap after a

    auto draw_char_3x5 = [&](char ch, int cx, int cy)
    {
        uint8_t rows[5] = {};
        switch(ch)
        {
            case 'o':
                rows[0] = 0b010;
                rows[1] = 0b101;
                rows[2] = 0b101;
                rows[3] = 0b101;
                rows[4] = 0b010;
                break;
            case 'a':
                rows[0] = 0b010;
                rows[1] = 0b001;
                rows[2] = 0b011;
                rows[3] = 0b101;
                rows[4] = 0b111;
                break;
            default:
                return;
        }

        for(int yy = 0; yy < 5; ++yy)
        {
            const uint8_t row = rows[yy];
            for(int xx = 0; xx < 3; ++xx)
            {
                if((row >> (2 - xx)) & 1)
                {
                    const int px = cx + xx;
                    const int py = cy + yy;
                    if(px >= 0 && px < 128 && py >= 0 && py < 64)
                        d.DrawPixel(px, py, on);
                }
            }
        }
    };

    auto draw_l_top_extended = [&](int cx, int cy)
    {
        // Extend only the top of the vertical stroke by one pixel.
        for(int yy = -1; yy <= 6; ++yy)
        {
            const int py = cy + yy;
            if(cx >= 0 && cx < 128 && py >= 0 && py < 64)
                d.DrawPixel(cx, py, on);
        }
        const int foot_y = cy + 6;
        if(foot_y >= 0 && foot_y < 64)
        {
            for(int xx = 0; xx < 5; ++xx)
            {
                const int px = cx + xx;
                if(px >= 0 && px < 128)
                    d.DrawPixel(px, foot_y, on);
            }
        }
    };

    auto draw_D_4x7 = [&](int cx, int cy)
    {
        const uint8_t rows[7] = {
            0b1110,
            0b1001,
            0b1001,
            0b1001,
            0b1001,
            0b1001,
            0b1110,
        };
        for(int yy = 0; yy < 7; ++yy)
        {
            for(int xx = 0; xx < 4; ++xx)
            {
                if((rows[yy] >> (3 - xx)) & 1)
                {
                    const int px = cx + xx;
                    const int py = cy + yy;
                    if(px >= 0 && px < 128 && py >= 0 && py < 64)
                        d.DrawPixel(px, py, on);
                }
            }
        }
    };

    draw_l_top_extended(x + l_x, y);
    draw_char_3x5('o', x + o_x, y);
    draw_char_3x5('a', x + a_x, y);
    draw_D_4x7(x + d_x, y);

    // Extended baseline from L to one pixel before D.
    const int baseline_y = y + Font5x7::H - 1;
    const int baseline_end_x = x + d_x - 2; // leaves one blank pixel before D
    d.DrawLine(x, baseline_y, baseline_end_x, baseline_y, on);
}

static int TuneWordmarkWidth()
{
    // 5 + 1 + 3 + 1 + 3 + 1 + 3
    return 17;
}

static void DrawTuneWordmark(OledPager& d, int x, int y, bool on)
{
    constexpr int t_x = 0;   // width 5 (0..4)
    constexpr int u_x = 6;   // width 3 (6..8), 1px gap after T
    constexpr int n_x = 10;  // width 3 (10..12), 1px gap after u
    constexpr int e_x = 14;  // width 3 (14..16), 1px gap after n

    auto draw_char_3x5 = [&](char ch, int cx, int cy)
    {
        uint8_t rows[5] = {};
        switch(ch)
        {
            case 'u':
                rows[0] = 0b101;
                rows[1] = 0b101;
                rows[2] = 0b101;
                rows[3] = 0b101;
                rows[4] = 0b011;
                break;
            case 'n':
                rows[0] = 0b110;
                rows[1] = 0b101;
                rows[2] = 0b101;
                rows[3] = 0b101;
                rows[4] = 0b101;
                break;
            case 'e':
                rows[0] = 0b111;
                rows[1] = 0b100;
                rows[2] = 0b110;
                rows[3] = 0b100;
                rows[4] = 0b111;
                break;
            default:
                return;
        }

        for(int yy = 0; yy < 5; ++yy)
        {
            const uint8_t row = rows[yy];
            for(int xx = 0; xx < 3; ++xx)
            {
                if((row >> (2 - xx)) & 1)
                {
                    const int px = cx + xx;
                    const int py = cy + yy;
                    if(px >= 0 && px < 128 && py >= 0 && py < 64)
                        d.DrawPixel(px, py, on);
                }
            }
        }
    };

    auto draw_t = [&](int cx, int cy)
    {
        // T top bar.
        for(int xx = 0; xx < 5; ++xx)
        {
            const int px = cx + xx;
            if(px >= 0 && px < 128 && cy >= 0 && cy < 64)
                d.DrawPixel(px, cy, on);
        }
        // T vertical stem.
        for(int yy = 1; yy < 7; ++yy)
        {
            const int py = cy + yy;
            const int px = cx + 2;
            if(px >= 0 && px < 128 && py >= 0 && py < 64)
                d.DrawPixel(px, py, on);
        }
    };

    draw_t(x + t_x, y);
    draw_char_3x5('u', x + u_x, y + 2);
    draw_char_3x5('n', x + n_x, y + 2);
    draw_char_3x5('e', x + e_x, y + 2);

    // Extended top line from T across u/n/e.
    d.DrawLine(x, y, x + TuneWordmarkWidth() - 1, y, on);
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

static constexpr int32_t kEngineRowCount = 3;
static constexpr int32_t kEngineRowWave = 0;
static constexpr int32_t kEngineRowLoad = 1;
static constexpr int32_t kEngineRowTune = 2;

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

    const uint8_t row = ctx.engine->perform_nav.perform_engine_row % static_cast<uint8_t>(kEngineRowCount);
    if(row == kEngineRowWave)
        return UiNav_Push(ctx.ui->ui_nav, UiScreenId::PerformWaveEdit);
    if(row != kEngineRowLoad)
        return false;

    ctx.engine->layer.engine_load_target_layer = ctx.engine->perform_nav.perform_layer & 1u;
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

    if((e.type == UiInputType::BtnDown || e.type == UiInputType::BtnUp) && e.id == kUiBtnRShift)
    {
        const uint8_t row = engine.perform_nav.perform_engine_row % static_cast<uint8_t>(kEngineRowCount);
        if(row == kEngineRowTune)
        {
            ui.ui_dirty = true;
            return true;
        }
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
                int v = static_cast<int>(engine.layer.engine_tune_cents[layer]) + e.value;
                v = ClampInt(v, -99, 99);
                const int8_t vv = static_cast<int8_t>(v);
                if(vv != engine.layer.engine_tune_cents[layer])
                {
                    engine.layer.engine_tune_cents[layer] = vv;
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
    const int tune_w = TuneWordmarkWidth();
    constexpr int kScreenW = 128;
    constexpr int kHalfW = kScreenW / 2;
    const int load_anchor_x = kHalfW / 2;
    const int tune_anchor_x = kHalfW + (kHalfW / 2);
    int load_x = load_anchor_x - (load_w / 2);
    int tune_x = tune_anchor_x - (tune_w / 2);
    const int load_min_x = 1;
    const int load_max_x = (kHalfW - 1) - load_w;
    const int tune_min_x = kHalfW + 1;
    const int tune_max_x = (kScreenW - 1) - tune_w;
    if(load_x < load_min_x)
        load_x = load_min_x;
    if(load_x > load_max_x)
        load_x = load_max_x;
    if(tune_x < tune_min_x)
        tune_x = tune_min_x;
    if(tune_x > tune_max_x)
        tune_x = tune_max_x;

    if(row == kEngineRowLoad)
    {
        d.DrawRect(load_x - 1, kFooterY - 1, load_x + load_w, kFooterY + Font5x7::H, true, true);
        DrawLoadWordmark(d, load_x, kFooterY, false);
    }
    else
    {
        DrawLoadWordmark(d, load_x, kFooterY, true);
    }

    if(row == kEngineRowTune)
    {
        if(ctx.rshift)
            d.DrawRect(tune_x - 2, kFooterY - 2, tune_x + tune_w + 1, kFooterY + Font5x7::H + 1, true, false);
        else
            DrawDottedRect(d, tune_x - 2, kFooterY - 2, tune_x + tune_w + 1, kFooterY + Font5x7::H + 1, true);
        const int tune_value = ctx.rshift ? static_cast<int>(engine.layer.engine_tune_cents[layer])
                                          : static_cast<int>(engine.layer.engine_tune_semitones[layer]);
        const int val_w = SignedSemitoneTextWidth(tune_value);
        int val_x = tune_x + (tune_w - val_w) / 2;
        if(val_x < tune_x)
            val_x = tune_x;
        DrawSignedSemitoneText(d, tune_value, val_x, kFooterY, true);
    }
    else
    {
        DrawTuneWordmark(d, tune_x, kFooterY, true);
    }
}
