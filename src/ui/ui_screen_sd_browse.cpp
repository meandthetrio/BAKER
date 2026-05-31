#include "ui_screens_internal.h"

#include "app_state_ui.h"
#include "app_state_engine.h"
#include "app_state_recording.h"
#include "app_state_project.h"
#include "app_state_diagnostics.h"
#include "app_state_shared.h"
#include "app_state_worker.h"
#include "ui_input.h"
#include "ui_list_menu.h"
#include "ui_value_edit.h"
#include "ui_layout.h"
#include "oled_pager.h"
#include "ui_requests.h"
#include "sd_browser_state.h"
#include "sample_edit.h"

#include <cctype>
#include <cstdio>
#include <cstring>

using namespace daisy;

namespace
{
static constexpr uint8_t kSdManageVisibleRows = 6;
static constexpr uint8_t kSdManageStyleFilterVisibleRows = 5;
static constexpr uint8_t kSdManageActionCount = 3;
static constexpr int kSdManageRowPitch = Font5x7::H + 2;
static constexpr int kSdManageNumberX = 2;
static constexpr int kSdManageRowBoxX = 1;
static constexpr int kSdManageNameX = 25;
static constexpr int kSdManageStyleX = 95;
static constexpr int kSdManageFocusedRowTopExtra = 1;
static constexpr int kSdManageRowsStartY = 12;
static constexpr int kSdManageHeaderButtonY = 0;
static constexpr int kSdManageHeaderButtonH = kMicroH + 3;
static constexpr int kSdManageHeaderTextY = 2;
static constexpr int kSdManageHeaderNumberX0 = 0;
static constexpr int kSdManageHeaderNumberX1 = 18;
static constexpr int kSdManageHeaderNameX0 = 22;
static constexpr int kSdManageHeaderNameX1 = 76;
static constexpr int kSdManageHeaderStyleX0 = 80;
static constexpr int kSdManageHeaderStyleX1 = 127;
static constexpr int kSdManageOverlayX0 = 16;
static constexpr int kSdManageOverlayY0 = 10;
static constexpr int kSdManageOverlayX1 = 115;
static constexpr int kSdManageOverlayY1 = 54;
static constexpr int kSdManageOverlayTopActionY = kSdManageOverlayY0 + 18;
static constexpr int kSdManageOverlayBottomActionY = kSdManageOverlayY0 + 32;
static constexpr int kSdManageOverlayLeftActionX = kSdManageOverlayX0 + 8;
static constexpr int kSdManageOverlayStyleX = kSdManageOverlayX0 + 55;
static const char kSdManageMaxStyleLabel[] = "Cold";
static const char* kSdManageStyleFilterLabels[kSampleStyleOptionCount] = {
    "all",
    "Hot",
    "Dry",
    "Wet",
    "Cold",
};

int CenterTinyLabelXInBand(int x0, int x1, const char* label)
{
    const int w = TinyStringWidth(label);
    return x0 + ((x1 - x0 + 1) - w) / 2;
}

int CenterTinyLabelYInBand(int y0, int y1)
{
    return y0 + ((y1 - y0 + 1) - Font5x7::H) / 2;
}

void ComputeStyleFilterOptionPosition(int overlay_x0,
                                      int overlay_y0,
                                      int overlay_x1,
                                      int overlay_y1,
                                      const char* label,
                                      uint8_t option,
                                      int& out_x,
                                      int& out_y)
{
    const int content_x0 = overlay_x0 + 4;
    const int content_x1 = overlay_x1 - 4;
    const int content_y0 = overlay_y0 + 4;
    const int content_y1 = overlay_y1 - 4;
    const int content_mid_x = content_x0 + ((content_x1 - content_x0 + 1) / 2);
    const int col_x0[2] = {content_x0, content_mid_x};
    const int col_x1[2] = {content_mid_x - 1, content_x1};
    const int content_h = content_y1 - content_y0 + 1;
    const int row_y0[3] = {
        content_y0,
        content_y0 + content_h / 3,
        content_y0 + (2 * content_h) / 3,
    };
    const int row_y1[3] = {
        row_y0[1] - 1,
        row_y0[2] - 1,
        content_y1,
    };

    if(option == 0u)
    {
        out_x = CenterTinyLabelXInBand(content_x0, content_x1, label);
        out_y = CenterTinyLabelYInBand(row_y0[0], row_y1[0]);
        return;
    }

    const uint8_t grid_index = static_cast<uint8_t>(option - 1u);
    const uint8_t row = static_cast<uint8_t>(1u + (grid_index / 2u));
    const uint8_t col = static_cast<uint8_t>(grid_index % 2u);

    out_x = CenterTinyLabelXInBand(col_x0[col], col_x1[col], label);
    out_y = CenterTinyLabelYInBand(row_y0[row], row_y1[row]);
}

void DrawFillOnlyString6x8(OledPager& d, const char* str, int x, int y)
{
    if(!str)
        return;
    const int w = static_cast<int>(std::strlen(str)) * 6;
    int x0 = x - 2;
    int y0 = y - 2;
    int x1 = x + w + 1;
    int y1 = y + 8 + 1;
    if(x0 < 0) x0 = 0;
    if(y0 < 0) y0 = 0;
    if(x1 > 127) x1 = 127;
    if(y1 > 63) y1 = 63;
    d.DrawRect(x0, y0, x1, y1, true, true);
    d.SetCursor(x, y);
    d.WriteString(str, Font_6x8, false);
}

void DrawFillOnlyTinyString(OledPager& d, const char* str, int x, int y)
{
    if(!str)
        return;
    const int w = TinyStringWidth(str);
    int x0 = x - 2;
    int y0 = y - 2;
    int x1 = x + w + 1;
    int y1 = y + Font5x7::H + 1;
    if(x0 < 0) x0 = 0;
    if(y0 < 0) y0 = 0;
    if(x1 > 127) x1 = 127;
    if(y1 > 63) y1 = 63;
    d.DrawRect(x0, y0, x1, y1, true, true);
    DrawTinyString(d, str, x, y, false);
}

void DrawFillOnlyTinyStringCaseSensitive(OledPager& d, const char* str, int x, int y)
{
    if(!str)
        return;
    const int w = TinyStringWidth(str);
    int x0 = x - 2;
    int y0 = y - 2;
    int x1 = x + w + 1;
    int y1 = y + Font5x7::H + 1;
    if(x0 < 0) x0 = 0;
    if(y0 < 0) y0 = 0;
    if(x1 > 127) x1 = 127;
    if(y1 > 63) y1 = 63;
    d.DrawRect(x0, y0, x1, y1, true, true);
    DrawTinyStringCaseSensitive(d, str, x, y, false);
}

uint8_t WrapCursor(uint8_t value, int delta, uint8_t count)
{
    int next = static_cast<int>(value) + delta;
    while(next < 0)
        next += count;
    while(next >= static_cast<int>(count))
        next -= count;
    return static_cast<uint8_t>(next);
}

void BuildRenameDraftFromName(const char* name, char* out, size_t out_n)
{
    if(!out || out_n == 0u)
        return;
    out[0] = '\0';
    if(!name || name[0] == '\0')
        return;
    BuildSampleDisplayName(name, out, out_n);
}

void BuildSdManageSlotNumber(uint8_t sample_index, char* out, size_t out_n)
{
    if(!out || out_n == 0)
        return;
    std::snprintf(out, out_n, "%u.", static_cast<unsigned>(sample_index + 1u));
}

void BuildSdManageDisplayName(const SdBrowserState& sd, uint8_t sample_index, char* out, size_t out_n)
{
    if(!out || out_n == 0)
        return;
    out[0] = '\0';
    if(sample_index >= sd.wav_count)
        return;
    std::snprintf(out, out_n, "%s", sd.display_names[sample_index]);
}

const char* SdManageStyleDisplayName(const SdBrowserState& sd, uint8_t sample_index)
{
    if(sample_index >= sd.wav_count)
        return SampleStyleLabel(SampleStyle::None);
    return SampleStyleLabel(sd.styles[sample_index]);
}

SampleStyle SampleStyleFromCursor(uint8_t cursor)
{
    switch(cursor)
    {
        case 1u: return SampleStyle::Hot;
        case 2u: return SampleStyle::Dry;
        case 3u: return SampleStyle::Wet;
        case 4u: return SampleStyle::Cold;
        case 0u:
        default: return SampleStyle::None;
    }
}

uint8_t SampleStyleCursor(SampleStyle style)
{
    switch(style)
    {
        case SampleStyle::Hot: return 1u;
        case SampleStyle::Dry: return 2u;
        case SampleStyle::Wet: return 3u;
        case SampleStyle::Cold: return 4u;
        case SampleStyle::None:
        default: return 0u;
    }
}

uint8_t SdManageStyleFilterCursor(const AppUiState& ui)
{
    if(ui.sd_manage_style_filter == kSampleStyleFilterAll)
        return 0u;
    const uint8_t stored = ui.sd_manage_style_filter;
    return (stored < kSampleStyleOptionCount) ? stored : 0u;
}

char ToAsciiLower(char c)
{
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

int CompareSdManageNames(const SdBrowserState& sd, uint8_t lhs_index, uint8_t rhs_index)
{
    char lhs[kSdNameMax];
    char rhs[kSdNameMax];
    BuildSdManageDisplayName(sd, lhs_index, lhs, sizeof(lhs));
    BuildSdManageDisplayName(sd, rhs_index, rhs, sizeof(rhs));

    for(size_t i = 0;; ++i)
    {
        const char lc = ToAsciiLower(lhs[i]);
        const char rc = ToAsciiLower(rhs[i]);
        if(lc < rc)
            return -1;
        if(lc > rc)
            return 1;
        if(lc == '\0')
            break;
    }

    if(lhs_index < rhs_index)
        return -1;
    if(lhs_index > rhs_index)
        return 1;
    return 0;
}

int FindVisibleSdManageIndex(const AppUiState& ui, uint8_t sample_index)
{
    for(uint8_t i = 0; i < ui.sd_manage_visible_count; ++i)
    {
        if(ui.sd_manage_visible_order[i] == sample_index)
            return static_cast<int>(i);
    }
    return -1;
}

uint8_t SdManageFocusCount(const AppUiState& ui)
{
    return static_cast<uint8_t>(kProjectPresetsHeaderCount + ui.sd_manage_visible_count);
}

uint8_t SdManageMaxTopRow(const AppUiState& ui)
{
    return (ui.sd_manage_visible_count > kSdManageVisibleRows)
               ? static_cast<uint8_t>(ui.sd_manage_visible_count - kSdManageVisibleRows)
               : 0u;
}

void ClampSdManageTopRow(AppUiState& ui)
{
    const uint8_t max_top = SdManageMaxTopRow(ui);
    if(ui.sd_manage_top_row > max_top)
        ui.sd_manage_top_row = max_top;
}

void EnsureSdManageRowVisible(AppUiState& ui, uint8_t row_index)
{
    ClampSdManageTopRow(ui);
    if(row_index < ui.sd_manage_top_row)
        ui.sd_manage_top_row = row_index;
    else if(row_index >= static_cast<uint8_t>(ui.sd_manage_top_row + kSdManageVisibleRows))
        ui.sd_manage_top_row = static_cast<uint8_t>(row_index - (kSdManageVisibleRows - 1u));
    ClampSdManageTopRow(ui);
}

void SyncCurrentSdManageIndexToFocusedRow(AppUiState& ui)
{
    if(ui.sd_manage_focus_index < kProjectPresetsHeaderCount || ui.sd_manage_visible_count == 0u)
        return;

    uint8_t row_index = static_cast<uint8_t>(ui.sd_manage_focus_index - kProjectPresetsHeaderCount);
    if(row_index >= ui.sd_manage_visible_count)
        row_index = static_cast<uint8_t>(ui.sd_manage_visible_count - 1u);
    ui.sd_manage_focus_index = static_cast<uint8_t>(kProjectPresetsHeaderCount + row_index);
    ui.sd_manage_current_index = ui.sd_manage_visible_order[row_index];
}

void RebuildVisibleSdManageOrder(AppUiState& ui, const SdBrowserState& sd)
{
    const bool row_focus = ui.sd_manage_focus_index >= kProjectPresetsHeaderCount;
    const uint8_t selected_index = ui.sd_manage_current_index;

    uint8_t count = 0u;
    for(uint8_t i = 0; i < sd.wav_count; ++i)
    {
        if(ui.sd_manage_style_filter != kSampleStyleFilterAll
           && SampleStyleCursor(sd.styles[i]) != ui.sd_manage_style_filter)
            continue;
        ui.sd_manage_visible_order[count++] = i;
    }

    for(uint8_t i = 0; i < count; ++i)
    {
        for(uint8_t j = static_cast<uint8_t>(i + 1u); j < count; ++j)
        {
            bool swap = false;
            if(ui.sd_manage_sort_mode == ProjectPresetsSortMode::Number)
            {
                swap = ui.sd_manage_sort_descending
                           ? (ui.sd_manage_visible_order[i] < ui.sd_manage_visible_order[j])
                           : (ui.sd_manage_visible_order[i] > ui.sd_manage_visible_order[j]);
            }
            else
            {
                const int cmp = CompareSdManageNames(sd,
                                                     ui.sd_manage_visible_order[i],
                                                     ui.sd_manage_visible_order[j]);
                swap = ui.sd_manage_sort_descending ? (cmp < 0) : (cmp > 0);
            }

            if(swap)
            {
                const uint8_t tmp = ui.sd_manage_visible_order[i];
                ui.sd_manage_visible_order[i] = ui.sd_manage_visible_order[j];
                ui.sd_manage_visible_order[j] = tmp;
            }
        }
    }

    ui.sd_manage_visible_count = count;

    if(ui.sd_manage_visible_count == 0u)
    {
        ui.sd_manage_top_row = 0u;
        ui.sd_manage_current_index = 0u;
        if(row_focus)
            ui.sd_manage_focus_index = static_cast<uint8_t>(kProjectPresetsHeaderCount - 1u);
        return;
    }

    int selected_visible_index = FindVisibleSdManageIndex(ui, selected_index);
    if(selected_visible_index < 0)
    {
        selected_visible_index = 0;
        ui.sd_manage_current_index = ui.sd_manage_visible_order[0];
    }

    if(row_focus)
    {
        ui.sd_manage_focus_index = static_cast<uint8_t>(kProjectPresetsHeaderCount + selected_visible_index);
        EnsureSdManageRowVisible(ui, static_cast<uint8_t>(selected_visible_index));
    }
    else
    {
        ClampSdManageTopRow(ui);
    }
}

void MoveSdManageFocus(AppUiState& ui, int delta)
{
    if(delta == 0)
        return;

    const uint8_t focus_count = SdManageFocusCount(ui);
    if(focus_count == 0u)
        return;

    const int step = (delta > 0) ? 1 : -1;
    int remaining = (delta > 0) ? delta : -delta;
    while(remaining-- > 0)
    {
        if(ui.sd_manage_focus_index < kProjectPresetsHeaderCount)
        {
            ui.sd_manage_focus_index = WrapCursor(ui.sd_manage_focus_index, step, focus_count);
            if(ui.sd_manage_focus_index >= kProjectPresetsHeaderCount)
            {
                const uint8_t row_index = static_cast<uint8_t>(
                    ui.sd_manage_focus_index - kProjectPresetsHeaderCount);
                EnsureSdManageRowVisible(ui, row_index);
            }
            continue;
        }

        uint8_t row_index = static_cast<uint8_t>(ui.sd_manage_focus_index - kProjectPresetsHeaderCount);
        if(step > 0)
        {
            if(row_index + 1u >= ui.sd_manage_visible_count)
            {
                ui.sd_manage_focus_index = WrapCursor(ui.sd_manage_focus_index, step, focus_count);
                continue;
            }

            ++row_index;
            if(ui.sd_manage_visible_count > kSdManageVisibleRows
               && row_index >= static_cast<uint8_t>(ui.sd_manage_top_row + kSdManageVisibleRows))
                ++ui.sd_manage_top_row;
            ui.sd_manage_focus_index = static_cast<uint8_t>(kProjectPresetsHeaderCount + row_index);
        }
        else
        {
            if(row_index == 0u)
            {
                ui.sd_manage_focus_index = WrapCursor(ui.sd_manage_focus_index, step, focus_count);
                continue;
            }

            --row_index;
            if(row_index < ui.sd_manage_top_row && ui.sd_manage_top_row > 0u)
                --ui.sd_manage_top_row;
            ui.sd_manage_focus_index = static_cast<uint8_t>(kProjectPresetsHeaderCount + row_index);
        }
    }

    ClampSdManageTopRow(ui);
}

int ProjectHeaderGlyphWidth(const char* glyph)
{
    if(!glyph)
        return 0;
    if(std::strcmp(glyph, "#") == 0)
        return 5;
    if(std::strcmp(glyph, "v") == 0 || std::strcmp(glyph, "^") == 0)
        return 5;
    return MicroStringWidth(glyph);
}

void DrawProjectHeaderGlyph(OledPager& d, const char* glyph, int x, int y, bool on)
{
    if(!glyph || glyph[0] == '\0')
        return;

    if(std::strcmp(glyph, "#") == 0)
    {
        static constexpr uint8_t kHashRows[kMicroH] = {
            0b01010,
            0b11111,
            0b01010,
            0b11111,
            0b01010,
            0b0000,
        };
        for(int yy = 0; yy < kMicroH; ++yy)
        {
            const uint8_t row = kHashRows[yy];
            for(int xx = 0; xx < 5; ++xx)
            {
                if((row >> (4 - xx)) & 1u)
                    d.DrawPixel(x + xx, y + yy, on);
            }
        }
        return;
    }

    if(std::strcmp(glyph, "v") == 0)
    {
        static constexpr uint8_t kDownRows[kMicroH] = {
            0b00000,
            0b10001,
            0b01010,
            0b00100,
            0b00000,
            0b00000,
        };
        for(int yy = 0; yy < kMicroH; ++yy)
        {
            const uint8_t row = kDownRows[yy];
            for(int xx = 0; xx < 5; ++xx)
            {
                if((row >> (4 - xx)) & 1u)
                    d.DrawPixel(x + xx, y + yy, on);
            }
        }
        return;
    }

    if(std::strcmp(glyph, "^") == 0)
    {
        static constexpr uint8_t kUpRows[kMicroH] = {
            0b00100,
            0b01010,
            0b10001,
            0b00000,
            0b00000,
            0b00000,
        };
        for(int yy = 0; yy < kMicroH; ++yy)
        {
            const uint8_t row = kUpRows[yy];
            for(int xx = 0; xx < 5; ++xx)
            {
                if((row >> (4 - xx)) & 1u)
                    d.DrawPixel(x + xx, y + yy, on);
            }
        }
        return;
    }

    DrawMicroString(d, glyph, x, y, on);
}

void DrawProjectHeaderButton(OledPager& d,
                             int x0,
                             int x1,
                             const char* label,
                             bool focused,
                             bool draw_arrow,
                             bool arrow_descending,
                             bool filter_active)
{
    const bool active = focused || filter_active;
    const int arrow_w = draw_arrow ? ProjectHeaderGlyphWidth("v") + 1 : 0;
    const int label_w = ProjectHeaderGlyphWidth(label);
    const int content_w = arrow_w + label_w;
    const int box_w = content_w + 6;
    int box_x0 = x0 + ((x1 - x0 + 1) - box_w) / 2;
    int box_x1 = box_x0 + box_w;
    if(box_x0 < x0)
    {
        box_x0 = x0;
        box_x1 = x0 + box_w;
    }
    if(box_x1 > x1)
    {
        box_x1 = x1;
        box_x0 = x1 - box_w;
    }

    d.DrawRect(box_x0,
               kSdManageHeaderButtonY,
               box_x1,
               kSdManageHeaderButtonY + kSdManageHeaderButtonH,
               active,
               true);
    d.DrawRect(box_x0,
               kSdManageHeaderButtonY,
               box_x1,
               kSdManageHeaderButtonY + kSdManageHeaderButtonH,
               true,
               false);

    const bool text_on = !active;
    int text_x = box_x0 + 3;
    if(draw_arrow)
    {
        DrawProjectHeaderGlyph(d,
                               arrow_descending ? "^" : "v",
                               text_x,
                               kSdManageHeaderTextY,
                               text_on);
        text_x += arrow_w;
    }
    DrawProjectHeaderGlyph(d, label, text_x, kSdManageHeaderTextY, text_on);
}

void DrawSdManageHeaderRow(OledPager& d, const AppUiState& ui)
{
    DrawProjectHeaderButton(d,
                            kSdManageHeaderNumberX0,
                            kSdManageHeaderNumberX1,
                            "#",
                            ui.sd_manage_focus_index == 0u,
                            ui.sd_manage_sort_mode == ProjectPresetsSortMode::Number,
                            ui.sd_manage_sort_descending,
                            false);
    DrawProjectHeaderButton(d,
                            kSdManageHeaderNameX0,
                            kSdManageHeaderNameX1,
                            "name",
                            ui.sd_manage_focus_index == 1u,
                            ui.sd_manage_sort_mode == ProjectPresetsSortMode::Name,
                            ui.sd_manage_sort_descending,
                            false);
    DrawProjectHeaderButton(d,
                            kSdManageHeaderStyleX0,
                            kSdManageHeaderStyleX1,
                            "style",
                            ui.sd_manage_focus_index == 2u,
                            false,
                            false,
                            ui.sd_manage_style_filter != kSampleStyleFilterAll);
}

void DrawSdManageRow(OledPager& d,
                     const SdBrowserState& sd,
                     uint8_t sample_index,
                     int row_y,
                     bool focused)
{
    char number[5];
    char name[kSdNameMax];
    BuildSdManageSlotNumber(sample_index, number, sizeof(number));
    BuildSdManageDisplayName(sd, sample_index, name, sizeof(name));

    if(focused)
    {
        const int row_w = (kSdManageStyleX + TinyStringWidth(kSdManageMaxStyleLabel))
                          - kSdManageRowBoxX;
        int x0 = kSdManageRowBoxX;
        int y0 = row_y - kSdManageFocusedRowTopExtra;
        int x1 = kSdManageRowBoxX + row_w;
        int y1 = row_y + Font5x7::H;
        if(x0 < 0) x0 = 0;
        if(y0 < 0) y0 = 0;
        if(x1 > 127) x1 = 127;
        if(y1 > 63) y1 = 63;
        d.DrawRect(x0, y0, x1, y1, true, true);
        DrawTinyString(d, number, kSdManageNumberX, row_y, false);
        DrawTinyStringCaseSensitive(d, name, kSdManageNameX, row_y, false);
        DrawTinyStringCaseSensitive(d,
                                    SdManageStyleDisplayName(sd, sample_index),
                                    kSdManageStyleX,
                                    row_y,
                                    false);
        return;
    }

    DrawTinyString(d, number, kSdManageNumberX, row_y, true);
    DrawTinyStringCaseSensitive(d, name, kSdManageNameX, row_y, true);
    DrawTinyStringCaseSensitive(d,
                                SdManageStyleDisplayName(sd, sample_index),
                                kSdManageStyleX,
                                row_y,
                                true);
}

void RenderSdManageList(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.display)
        return;

    AppUiState& ui = *ctx.ui;
    SdBrowserState& sd = ui.sd;
    OledPager& d = *ctx.display;
    RebuildVisibleSdManageOrder(ui, sd);
    d.Fill(false);

    DrawSdManageHeaderRow(d, ui);

    if(!sd.sd_ok)
    {
        DrawTinyString(d, "sd err", 46, 28, true);
        return;
    }
    if(sd.scan_in_progress && sd.wav_count == 0u)
    {
        DrawTinyString(d, "scanning", 38, 28, true);
        return;
    }
    if(ui.sd_manage_visible_count == 0u)
    {
        DrawTinyString(d, "none", 52, 28, true);
        return;
    }

    ClampSdManageTopRow(ui);
    const uint8_t top_row = ui.sd_manage_top_row;
    for(uint8_t row = 0; row < kSdManageVisibleRows; ++row)
    {
        const uint8_t visible_index = static_cast<uint8_t>(top_row + row);
        if(visible_index >= ui.sd_manage_visible_count)
            break;

        const uint8_t sample_index = ui.sd_manage_visible_order[visible_index];
        const int row_y = kSdManageRowsStartY + static_cast<int>(row) * kSdManageRowPitch;
        const bool focused = (ui.sd_manage_focus_index
                              == static_cast<uint8_t>(kProjectPresetsHeaderCount + visible_index));
        DrawSdManageRow(d, sd, sample_index, row_y, focused);
    }
}

void ActivateSdManageHeader(AppUiState& ui)
{
    if(ui.sd_manage_focus_index == 0u)
    {
        if(ui.sd_manage_sort_mode == ProjectPresetsSortMode::Number)
            ui.sd_manage_sort_descending = !ui.sd_manage_sort_descending;
        else
        {
            ui.sd_manage_sort_mode = ProjectPresetsSortMode::Number;
            ui.sd_manage_sort_descending = false;
        }
        ui.ui_dirty = true;
    }
    else if(ui.sd_manage_focus_index == 1u)
    {
        if(ui.sd_manage_sort_mode == ProjectPresetsSortMode::Name)
            ui.sd_manage_sort_descending = !ui.sd_manage_sort_descending;
        else
        {
            ui.sd_manage_sort_mode = ProjectPresetsSortMode::Name;
            ui.sd_manage_sort_descending = false;
        }
        ui.ui_dirty = true;
    }
}

void EnsureScanRequested(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.worker)
        return;

    SdBrowserState& sd = ctx.ui->sd;
    if(sd.scan_in_progress || sd.scan_done)
        return;

    UiReq req{UiReqType::ScanSdWavs, 0, 0};
    if(UiReq_Push(*ctx.ui, *ctx.worker, req))
    {
        sd.scan_in_progress = true;
        SdBrowser_SetStatus(sd, "SCANNING");
        ctx.ui->ui_dirty = true;
    }
}

bool BeginSampleRenameFromSdManage(UiScreenCtx& ctx, uint8_t sample_index)
{
    if(!ctx.ui)
        return false;

    AppUiState& ui = *ctx.ui;
    if(sample_index >= ui.sd.wav_count)
        return true;

    ui.sample_rename_active = true;
    ui.sample_rename_index = sample_index;
    BuildRenameDraftFromName(ui.sd.display_names[sample_index],
                             ui.project_rename_draft,
                             sizeof(ui.project_rename_draft));
    ui.project_rename_length = static_cast<uint8_t>(std::strlen(ui.project_rename_draft));
    if(UiNav_Push(ui.ui_nav, UiScreenId::RenameProject))
        ui.ui_dirty = true;
    return true;
}
} // namespace

void SdManageMenu_OnEnter(UiScreenCtx& ctx)
{
    if(!ctx.ui)
        return;

    EnsureScanRequested(ctx);
    RebuildVisibleSdManageOrder(*ctx.ui, ctx.ui->sd);
    if(ctx.ui->sd_manage_focus_index >= SdManageFocusCount(*ctx.ui))
        ctx.ui->sd_manage_focus_index = kProjectPresetsHeaderCount;
    SyncCurrentSdManageIndexToFocusedRow(*ctx.ui);
}

bool SdManageMenu_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.ui)
        return false;
    if(ctx.shift)
        return false;

    AppUiState& ui = *ctx.ui;
    RebuildVisibleSdManageOrder(ui, ui.sd);

    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod && e.value != 0)
    {
        MoveSdManageFocus(ui, e.value);
        SyncCurrentSdManageIndexToFocusedRow(ui);
        ui.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        if(ui.sd_manage_focus_index < kProjectPresetsHeaderCount)
        {
            if(ui.sd_manage_focus_index == 2u)
            {
                ui.sd_manage_style_picker_cursor = SdManageStyleFilterCursor(ui);
                if(UiNav_Push(ui.ui_nav, UiScreenId::SdManageStyleFilter))
                    ui.ui_dirty = true;
                return true;
            }

            ActivateSdManageHeader(ui);
            return true;
        }

        if(ui.sd_manage_visible_count == 0u)
            return true;

        ui.sd_manage_action_cursor = 0u;
        ui.sd_manage_style_cursor = SampleStyleCursor(ui.sd.styles[ui.sd_manage_current_index]);
        if(UiNav_Push(ui.ui_nav, UiScreenId::SdManageActionMenu))
            ui.ui_dirty = true;
        return true;
    }

    return false;
}

void SdManageMenu_Render(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.display)
        return;

    RenderSdManageList(ctx);
}

bool SdManageStyleFilter_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.ui)
        return false;

    AppUiState& ui = *ctx.ui;

    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod && e.value != 0)
    {
        ui.sd_manage_style_picker_cursor = WrapCursor(ui.sd_manage_style_picker_cursor,
                                                      e.value,
                                                      kSampleStyleOptionCount);
        ui.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        ui.sd_manage_style_filter = (ui.sd_manage_style_picker_cursor == 0u)
                                        ? kSampleStyleFilterAll
                                        : SampleStyleCursor(SampleStyleFromCursor(
                                              ui.sd_manage_style_picker_cursor));
        RebuildVisibleSdManageOrder(ui, ui.sd);
        UiNav_Pop(ui.ui_nav);
        ui.ui_dirty = true;
        return true;
    }

    return false;
}

void SdManageStyleFilter_Render(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.display)
        return;

    AppUiState& ui = *ctx.ui;
    OledPager& d = *ctx.display;
    RenderSdManageList(ctx);

    const int overlay_x0 = 24;
    const int overlay_y0 = 8;
    const int overlay_x1 = 103;
    const int overlay_y1 = 63;
    d.DrawRect(overlay_x0, overlay_y0, overlay_x1, overlay_y1, false, true);
    d.DrawRect(overlay_x0, overlay_y0, overlay_x1, overlay_y1, true, false);

    const uint8_t option_count = kSampleStyleOptionCount;
    uint8_t focused_option = 0u;
    int focused_x = 0;
    int focused_row_y = 0;
    bool has_focused_option = false;

    for(uint8_t option = 0; option < option_count; ++option)
    {
        int option_x = 0;
        int row_y = 0;
        ComputeStyleFilterOptionPosition(overlay_x0,
                                         overlay_y0,
                                         overlay_x1,
                                         overlay_y1,
                                         kSdManageStyleFilterLabels[option],
                                         option,
                                         option_x,
                                         row_y);
        if(option == ui.sd_manage_style_picker_cursor)
        {
            focused_option = option;
            focused_x = option_x;
            focused_row_y = row_y;
            has_focused_option = true;
            continue;
        }

        DrawTinyStringCaseSensitive(d,
                                    kSdManageStyleFilterLabels[option],
                                    option_x,
                                    row_y,
                                    true);
    }

    if(has_focused_option)
    {
        DrawFillOnlyTinyStringCaseSensitive(d,
                                            kSdManageStyleFilterLabels[focused_option],
                                            focused_x,
                                            focused_row_y);
    }
}

bool SdManageActionMenu_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.ui || !ctx.worker)
        return false;

    AppUiState& ui = *ctx.ui;
    RebuildVisibleSdManageOrder(ui, ui.sd);

    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod && e.value != 0)
    {
        ui.sd_manage_action_cursor = WrapCursor(ui.sd_manage_action_cursor,
                                                e.value,
                                                kSdManageActionCount);
        ui.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt && e.value != 0)
    {
        const uint8_t sample_index = ui.sd_manage_current_index;
        if(ui.sd_manage_action_cursor == 1u && sample_index < ui.sd.wav_count)
        {
            ui.sd_manage_style_cursor = WrapCursor(ui.sd_manage_style_cursor,
                                                   e.value,
                                                   kSampleStyleOptionCount);
            ui.ui_dirty = true;
            return true;
        }
        return true;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        const uint8_t sample_index = ui.sd_manage_current_index;
        if(sample_index >= ui.sd.wav_count)
            return true;

        if(ui.sd_manage_action_cursor == 0u)
            return BeginSampleRenameFromSdManage(ctx, sample_index);

        if(ui.sd_manage_action_cursor == 1u)
        {
            const UiReq req{UiReqType::UpdateWavStyleIndex,
                            ui.sd_manage_current_index,
                            SampleStyleCursor(SampleStyleFromCursor(ui.sd_manage_style_cursor))};
            if(!UiReq_Push(ui, *ctx.worker, req))
                return true;

            UiNav_Pop(ui.ui_nav);
            ui.ui_dirty = true;
            return true;
        }

        if(ui.sd_manage_action_cursor == 2u)
        {
            ui.sd_delete_mode = true;
            ui.sd_delete_index = sample_index;
            ExtractBaseName(ui.sd.paths[sample_index],
                            ui.sd_delete_name,
                            sizeof(ui.sd_delete_name));
            if(UiNav_Push(ui.ui_nav, UiScreenId::SdDeleteConfirm))
                ui.ui_dirty = true;
            return true;
        }
    }

    return false;
}

void SdManageActionMenu_Render(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.display)
        return;

    AppUiState& ui = *ctx.ui;
    OledPager& d = *ctx.display;
    RenderSdManageList(ctx);

    char name[kSdNameMax];
    BuildSdManageDisplayName(ui.sd, ui.sd_manage_current_index, name, sizeof(name));
    d.DrawRect(kSdManageOverlayX0, kSdManageOverlayY0, kSdManageOverlayX1, kSdManageOverlayY1, false, true);
    d.DrawRect(kSdManageOverlayX0, kSdManageOverlayY0, kSdManageOverlayX1, kSdManageOverlayY1, true, false);

    DrawTinyStringCaseSensitive(d, name, kSdManageOverlayX0 + 4, kSdManageOverlayY0 + 4, true);
    if(ui.sd_manage_action_cursor == 0u)
        DrawFillOnlyTinyString(d, "rename", kSdManageOverlayLeftActionX, kSdManageOverlayTopActionY);
    else
        DrawTinyString(d, "rename", kSdManageOverlayLeftActionX, kSdManageOverlayTopActionY, true);

    const char* style = (ui.sd_manage_current_index < ui.sd.wav_count)
                            ? SampleStyleLabel(SampleStyleFromCursor(ui.sd_manage_style_cursor))
                            : SampleStyleLabel(SampleStyle::None);
    const int style_label_y = kSdManageOverlayTopActionY;
    if(ui.sd_manage_action_cursor == 1u)
    {
        const int style_label_w = TinyStringWidth(kSdManageMaxStyleLabel);
        DrawRencFocusFrame(d,
                           kSdManageOverlayStyleX,
                           style_label_y,
                           style_label_w,
                           Font5x7::H);
        DrawTinyStringCaseSensitive(d, style, kSdManageOverlayStyleX, style_label_y, false);
    }
    else
    {
        DrawTinyString(d, "style", kSdManageOverlayStyleX, style_label_y, true);
    }

    if(ui.sd_manage_action_cursor == 2u)
        DrawFillOnlyTinyString(d, "delete", kSdManageOverlayLeftActionX, kSdManageOverlayBottomActionY);
    else
        DrawTinyString(d, "delete", kSdManageOverlayLeftActionX, kSdManageOverlayBottomActionY, true);
}

void SdBrowse_OnEnter(UiScreenCtx& ctx)
{
    if(!ctx.ui)
        return;

    SdBrowserState& sd = ctx.ui->sd;
    const UiLayout layout = UiLayout_Default();
    const uint8_t rows = (layout.rows_body > 1) ? static_cast<uint8_t>(layout.rows_body - 1) : 1;
    if(!sd.menu_inited || sd.menu_rows != rows)
    {
        sd.menu_rows = rows;
        SdBrowser_RebuildMenu(sd);
    }

    EnsureScanRequested(ctx);
}

bool SdBrowse_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.ui)
        return false;

    SdBrowserState& sd = ctx.ui->sd;
    if(ctx.shift)
        return false;

    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod)
    {
        if(UiListMenu_OnEnc(sd.menu, e.value))
        {
            ctx.ui->ui_dirty = true;
            return true;
        }
        return false;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        EnsureScanRequested(ctx);
        if(!sd.scan_done)
            return true;

        if(sd.wav_count > 0 && !sd.scan_in_progress)
        {
            const uint16_t idx = sd.menu.cursor;
            const uint8_t layer_count = static_cast<uint8_t>(
                sizeof(ctx.engine->layer.engine_sample_path) / sizeof(ctx.engine->layer.engine_sample_path[0]));
            if(ctx.engine->layer.engine_load_target_layer < layer_count)
            {
                const uint8_t target = ctx.engine->layer.engine_load_target_layer & 1u;
                ctx.shared->sample.publish.sd_current_slot.store(target ^ 1u, std::memory_order_release);
                std::snprintf(ctx.engine->layer.engine_sample_path[target],
                              sizeof(ctx.engine->layer.engine_sample_path[target]),
                              "%s",
                              sd.paths[idx]);
                ExtractBaseName(sd.paths[idx],
                                ctx.engine->layer.engine_sample_name[target],
                                sizeof(ctx.engine->layer.engine_sample_name[target]));
            }
            UiReq req{UiReqType::LoadWavIndex, idx, 0};
            UiReq_Push(*ctx.ui, *ctx.worker, req);
            sd.load_in_progress = true;
            sd.load_progress = 0;
            SdBrowser_SetStatus(sd, "LOADING");
            if(ctx.ui->craft_browser_open)
                ctx.ui->craft_browser_wait_for_load = true;
            if(ctx.engine->layer.engine_load_from_perform)
                UiNav_Pop(ctx.ui->ui_nav);
            ctx.ui->ui_dirty = true;
        }
        return true;
    }

    return false;
}

void SdBrowse_Render(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.display)
        return;

    EnsureScanRequested(ctx);

    SdBrowserState& sd = ctx.ui->sd;
    const UiLayout layout = UiLayout_Default();
    bool show_issue = false;
    char issue_buf[24];
    issue_buf[0] = '\0';
    if(!sd.sd_ok)
    {
        show_issue = true;
        std::snprintf(issue_buf, sizeof(issue_buf), "SD ERR");
    }
    else if(sd.scan_done && !sd.scan_in_progress && sd.wav_count == 0)
    {
        show_issue = true;
        std::snprintf(issue_buf, sizeof(issue_buf), "NO WAV");
    }
    else if(sd.status[0] != '\0')
    {
        const bool noisy_ok = (std::strncmp(sd.status, "LOADED", 6) == 0)
                           || (std::strncmp(sd.status, "LOADING", 7) == 0)
                           || (std::strncmp(sd.status, "SCANNING", 8) == 0)
                           || (std::strncmp(sd.status, "DELETED", 7) == 0)
                           || (std::strncmp(sd.status, "READY", 5) == 0);
        if(!noisy_ok)
        {
            show_issue = true;
            std::snprintf(issue_buf, sizeof(issue_buf), "%s", sd.status);
        }
    }

    uint8_t lines_used = static_cast<uint8_t>(1 + (show_issue ? 1 : 0));
    if(lines_used >= layout.rows_body)
        lines_used = layout.rows_body;

    uint8_t menu_rows = (layout.rows_body > lines_used)
                        ? static_cast<uint8_t>(layout.rows_body - lines_used)
                        : 1;
    if(!sd.menu_inited || sd.menu_rows != menu_rows)
    {
        sd.menu_rows = menu_rows;
        SdBrowser_RebuildMenu(sd);
    }

    OledPager& d = *ctx.display;
    d.Fill(false);

    const char* header_label = "sd browse";
    const int header_w = TinyStringWidth(header_label);
    const int box_w = header_w + 2;
    const int box_h = 9;
    int box_x = 128 - box_w;
    if(box_x < 0)
        box_x = 0;
    d.DrawRect(box_x, 0, box_x + box_w - 1, box_h - 1, true, true);
    DrawTinyString(d, header_label, box_x + 1, 1, false);

    if(show_issue && lines_used > 1)
    {
        d.SetCursor(layout.x, layout.y_body);
        d.WriteString(issue_buf, Font_6x8, true);
    }

    const int menu_x = layout.x;
    const int menu_y = layout.y_body + layout.line_h * lines_used;
    const int label_x = menu_x + 6;

    for(uint8_t row = 0; row < sd.menu.rows; ++row)
    {
        const uint8_t idx = static_cast<uint8_t>(sd.menu.scroll + row);
        if(idx >= sd.menu.count || !sd.menu.items)
            break;

        const int row_y = menu_y + static_cast<int>(row) * layout.line_h;
        if(idx == sd.menu.cursor)
        {
            DrawFillOnlyString6x8(d, sd.menu.items[idx].label, label_x, row_y);
            continue;
        }

        d.SetCursor(label_x, row_y);
        d.WriteString(sd.menu.items[idx].label, Font_6x8, true);
    }
}
