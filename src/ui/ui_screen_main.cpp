#include "ui_screens_internal.h"

#include "app_state_ui.h"
#include "app_state_engine.h"
#include "app_state_recording.h"
#include "app_state_project.h"
#include "app_state_diagnostics.h"
#include "app_state_shared.h"
#include "app_state_worker.h"
#include "oled_pager.h"
#include "ui_input.h"
#include "ui_layout.h"

#include <cstdio>

static constexpr int32_t kMainMenuCount = 3;
static const char* kMenuLabels[kMainMenuCount] = {"PRESETS", "SAMPLES", "PERFORM"};
static constexpr int32_t kSamplesMenuCount = 3;
static const char* kSamplesMenuLabels[kSamplesMenuCount] = {"RECORD", "CRAFT", "SD BROWSER"};
static constexpr int32_t kRecordMenuCount = 3;
static const char* kRecordMenuLabels[kRecordMenuCount] = {"LINE IN", "MICROPHONE", "RENDER"};

static void DrawFillOnlyTinyString(OledPager& d, const char* str, int x, int y)
{
    if(!str)
        return;
    const int w = TinyStringWidth(str);
    d.DrawRect(x - 2, y - 2, x + w + 1, y + Font5x7::H + 1, true, true);
    DrawTinyString(d, str, x, y, false);
}

static int32_t WrapMenuIndex(int32_t current, int32_t delta, int32_t count)
{
    if(count <= 0)
        return 0;
    int32_t next = current + delta;
    while(next < 0)
        next += count;
    while(next >= count)
        next -= count;
    return next;
}

static int32_t NextMenuIndex(int32_t current, int32_t delta)
{
    return WrapMenuIndex(current, delta, kMainMenuCount);
}

constexpr int kIconW = 61;
constexpr int kIconH = 29;
constexpr int kIconStride = 8;

static const uint8_t kIconLoadDisk61x29[kIconH * kIconStride] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf0,
    0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30,
    0x60, 0x00, 0x3f, 0xff, 0xf8, 0x00, 0x00, 0x30,
    0x60, 0x00, 0x44, 0x01, 0x24, 0x00, 0x00, 0x30,
    0x60, 0x00, 0x84, 0x71, 0x24, 0x00, 0x00, 0x30,
    0x60, 0x21, 0x04, 0x89, 0x3c, 0x04, 0x00, 0x30,
    0x60, 0x61, 0x04, 0xa9, 0x04, 0x0c, 0x00, 0x30,
    0x60, 0xe1, 0x04, 0x89, 0x04, 0x1c, 0x00, 0x30,
    0x61, 0xe1, 0x04, 0x71, 0x04, 0x3c, 0x00, 0x30,
    0x63, 0xe1, 0x03, 0xfe, 0x04, 0x7c, 0x00, 0x30,
    0x67, 0xe1, 0x00, 0x00, 0x04, 0xfc, 0x00, 0x30,
    0x6f, 0xff, 0x3f, 0xff, 0xe5, 0xff, 0xff, 0xf0,
    0x7f, 0xff, 0x40, 0x00, 0x17, 0xff, 0xff, 0xf0,
    0x7f, 0xff, 0x4f, 0xff, 0x97, 0xff, 0xff, 0xf0,
    0x7f, 0xff, 0x40, 0x00, 0x17, 0xff, 0xff, 0xf0,
    0x7f, 0xff, 0x43, 0xfe, 0x17, 0xff, 0xff, 0xf0,
    0x6f, 0xff, 0x40, 0x00, 0x15, 0xff, 0xff, 0xf0,
    0x67, 0xe1, 0x40, 0x00, 0x14, 0xfc, 0x00, 0x30,
    0x63, 0xe1, 0x41, 0xfc, 0x14, 0x7c, 0x00, 0x30,
    0x61, 0xe1, 0x40, 0x00, 0x14, 0x3c, 0x00, 0x30,
    0x60, 0xe1, 0x40, 0x70, 0x14, 0x1c, 0x00, 0x30,
    0x60, 0x61, 0x40, 0x00, 0x14, 0x0c, 0x00, 0x30,
    0x60, 0x21, 0x40, 0x00, 0x14, 0x04, 0x00, 0x30,
    0x60, 0x00, 0xff, 0xff, 0xf8, 0x00, 0x00, 0x30,
    0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30,
    0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30,
    0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf0,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t kIconRecordTape61x29[kIconH * kIconStride] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf8,
    0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08,
    0xbf, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xe8,
    0xa0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28,
    0xa8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xa8,
    0xa4, 0x1f, 0xff, 0xff, 0xff, 0xff, 0xc1, 0x28,
    0xa2, 0x38, 0x00, 0x00, 0x00, 0x00, 0xe2, 0x28,
    0xa1, 0x37, 0xff, 0xff, 0xff, 0xff, 0x64, 0x28,
    0xa0, 0x28, 0x00, 0x00, 0x00, 0x00, 0xa0, 0x28,
    0xa0, 0x28, 0x7f, 0xff, 0xff, 0xf0, 0xa0, 0x28,
    0xa0, 0x28, 0xff, 0xff, 0xff, 0xf8, 0xa0, 0x28,
    0xa0, 0x29, 0xf8, 0x80, 0x08, 0xfc, 0xa0, 0x28,
    0xa0, 0x29, 0xf7, 0x87, 0xef, 0x7c, 0xa0, 0x28,
    0xa0, 0x29, 0xed, 0x83, 0xcd, 0xbc, 0xa0, 0x28,
    0xa0, 0x29, 0xe8, 0xb3, 0xa8, 0xbc, 0xa0, 0x28,
    0xa0, 0x29, 0xed, 0xb3, 0xcd, 0xbc, 0xa0, 0x28,
    0xa0, 0x29, 0xf7, 0x87, 0xef, 0x7c, 0xa0, 0x28,
    0xa0, 0x29, 0xf8, 0x80, 0x08, 0xfc, 0xa0, 0x28,
    0xa0, 0x29, 0xff, 0xff, 0xff, 0xfc, 0xa0, 0x28,
    0xa0, 0x28, 0xfc, 0x00, 0x01, 0xf8, 0xa0, 0x28,
    0xa0, 0x2c, 0x33, 0xff, 0xfe, 0x61, 0xa0, 0x28,
    0xa1, 0x37, 0x77, 0xc0, 0x1f, 0x77, 0x64, 0x28,
    0xa2, 0x38, 0x6f, 0xc0, 0x1f, 0xb0, 0xe2, 0x28,
    0xa4, 0x1f, 0xdf, 0xff, 0xff, 0xdf, 0xc1, 0x28,
    0xa8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xa8,
    0xa0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28,
    0xbf, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xe8,
    0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf8,
};

static const uint8_t kIconPerformMpc61x29[kIconH * kIconStride] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf8,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf8,
    0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08,
    0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08,
    0x87, 0xbd, 0xef, 0x7b, 0xcf, 0xff, 0xff, 0xc8,
    0x87, 0xbd, 0xef, 0x7b, 0xce, 0x0f, 0xff, 0xc8,
    0x87, 0xbd, 0xef, 0x7b, 0xce, 0xef, 0xf0, 0xc8,
    0x87, 0xbd, 0xef, 0x7b, 0xce, 0xef, 0xf6, 0xc8,
    0x80, 0x00, 0x00, 0x00, 0x0e, 0xef, 0xf6, 0xc8,
    0x87, 0xbd, 0xef, 0x7b, 0xce, 0xef, 0xf6, 0xc8,
    0x87, 0xbd, 0xef, 0x7b, 0xce, 0xee, 0x36, 0xc8,
    0x87, 0xbd, 0xef, 0x7b, 0xce, 0xee, 0xb6, 0xc8,
    0x87, 0xbd, 0xef, 0x7b, 0xce, 0xee, 0xb6, 0xc8,
    0x80, 0x00, 0x00, 0x00, 0x00, 0xe0, 0x86, 0x08,
    0x80, 0x00, 0x00, 0x00, 0x0f, 0xff, 0xff, 0xc8,
    0x80, 0x00, 0x00, 0x00, 0x0f, 0xff, 0xff, 0xc8,
    0x89, 0x11, 0x11, 0x20, 0x00, 0x00, 0x00, 0x08,
    0x89, 0x11, 0x11, 0x20, 0x00, 0x00, 0x00, 0x08,
    0x89, 0x11, 0x11, 0x20, 0x00, 0x00, 0x00, 0x08,
    0x89, 0x11, 0x11, 0x20, 0xa0, 0x14, 0x02, 0x88,
    0x89, 0x11, 0x11, 0x21, 0xb0, 0x36, 0x06, 0xc8,
    0x8b, 0xbb, 0xbb, 0xa3, 0xb8, 0x77, 0x0e, 0xe8,
    0x8b, 0xbb, 0xbb, 0xa3, 0xb8, 0x77, 0x0e, 0xe8,
    0x8b, 0xbb, 0xbb, 0xa3, 0xf8, 0x7f, 0x0f, 0xe8,
    0x8b, 0xbb, 0xbb, 0xa1, 0xf0, 0x3e, 0x07, 0xc8,
    0x8f, 0xff, 0xff, 0xe0, 0xe0, 0x1c, 0x03, 0x88,
    0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf8,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf8,
};

static void DrawMainMenuFriendStyle(OledPager& d, int selected)
{
    constexpr int kDisplayW = 128;
    constexpr int kDisplayH = 64;
    constexpr int kListLeftX = 2;
    constexpr int kListGapY = 6;

    d.Fill(false);

    const int text_h = Font5x7::H;
    int max_label_w = 0;
    for(int32_t i = 0; i < kMainMenuCount; ++i)
    {
        const int w = TinyStringWidth(kMenuLabels[i]);
        if(w > max_label_w)
            max_label_w = w;
    }
    const int total_h = (kMainMenuCount * text_h) + ((kMainMenuCount - 1) * kListGapY);
    const int start_y = (kDisplayH - total_h) / 2;
    const int list_w = max_label_w;
    const int icon_area_x = list_w + 4;
    const int icon_area_w = kDisplayW - icon_area_x;

    for(int32_t i = 0; i < kMainMenuCount; ++i)
    {
        const bool is_selected = (i == selected);
        const char* label = kMenuLabels[i];
        const int text_x = kListLeftX;
        const int text_y = start_y + i * (text_h + kListGapY);
        if(is_selected)
        {
            DrawFillOnlyTinyString(d, label, text_x, text_y);
        }
        else
        {
            DrawTinyString(d, label, text_x, text_y, true);
        }

        if(is_selected)
        {
            const uint8_t* icon = nullptr;
            int icon_w = 0;
            int icon_h = 0;
            int icon_stride = 0;
            if(i == 0)
            {
                icon = kIconLoadDisk61x29;
                icon_w = kIconW;
                icon_h = kIconH;
                icon_stride = kIconStride;
            }
            else if(i == 1)
            {
                icon = kIconRecordTape61x29;
                icon_w = kIconW;
                icon_h = kIconH;
                icon_stride = kIconStride;
            }
            else if(i == 2)
            {
                icon = kIconPerformMpc61x29;
                icon_w = kIconW;
                icon_h = kIconH;
                icon_stride = kIconStride;
            }
            if(icon != nullptr && icon_area_w > icon_w)
            {
                const int icon_x = icon_area_x + (icon_area_w - icon_w) / 2;
                const int icon_y = (kDisplayH - icon_h) / 2;
                DrawBitmap1bpp(d, icon_x, icon_y, icon_w, icon_h, icon_stride, icon, true);
            }
        }
    }
}

static void DrawTopRightMicroLabel(OledPager& d, const char* label)
{
    if(!label)
        return;
    const int box_h = kMicroH + 4;
    const int w = MicroStringWidth(label);
    const int box_w = w + 4;
    const int x = 128 - box_w;
    d.DrawRect(x, 0, x + box_w - 1, box_h - 1, true, true);
    DrawMicroString(d, label, x + 2, 2, false);
}

static void DrawMenuListStyle(OledPager& d,
                              const char* const* labels,
                              int count,
                              int selected,
                              const char* top_right_label)
{
    constexpr int kDisplayH = 64;
    constexpr int kListLeftX = 2;
    constexpr int kListGapY = 6;

    d.Fill(false);
    DrawTopRightMicroLabel(d, top_right_label);

    const int text_h = Font5x7::H;
    int max_label_w = 0;
    for(int i = 0; i < count; ++i)
    {
        const int w = TinyStringWidth(labels[i]);
        if(w > max_label_w)
            max_label_w = w;
    }

    const int total_h = (count * text_h) + ((count - 1) * kListGapY);
    const int start_y = (kDisplayH - total_h) / 2;
    for(int i = 0; i < count; ++i)
    {
        const int text_x = kListLeftX;
        const int text_y = start_y + i * (text_h + kListGapY);
        if(i == selected)
            DrawFillOnlyTinyString(d, labels[i], text_x, text_y);
        else
            DrawTinyString(d, labels[i], text_x, text_y, true);
    }
}

static void DrawBlankPlaceholder(OledPager& d, const char* top_right_label)
{
    d.Fill(false);
    DrawTopRightMicroLabel(d, top_right_label);
}

static int ClampRecordRenderInt(int v, int lo, int hi)
{
    if(v < lo)
        return lo;
    if(v > hi)
        return hi;
    return v;
}

static void FormatRecordRenderNoteValue(int offset, char* out, size_t out_n)
{
    if(!out || out_n == 0)
        return;
    if(offset > 0)
    {
        std::snprintf(out, out_n, "+%d", offset);
        return;
    }
    std::snprintf(out, out_n, "%d", offset);
}

static void FormatRecordRenderHoldValue(int hold_ms, char* out, size_t out_n)
{
    if(!out || out_n == 0)
        return;
    std::snprintf(out, out_n, "%dms", hold_ms);
}

static void DrawRecordRenderValueRow(OledPager& d,
                                     const char* label,
                                     const char* value,
                                     int y,
                                     bool value_focused)
{
    DrawTinyString(d, label, 2, y, true);

    const int value_w = TinyStringWidth(value);
    const int box_w = value_w + 6;
    const int box_h = Font5x7::H + 4;
    const int box_x = 128 - box_w - 2;
    const int box_y = y - 2;
    d.DrawRect(box_x, box_y, box_x + box_w - 1, box_y + box_h - 1, true, value_focused);
    DrawTinyString(d, value, box_x + 3, y, value_focused ? false : true);
}

static void DrawRecordRenderMenuStyle(OledPager& d, const AppUiState& ui)
{
    constexpr int kDisplayH = 64;
    constexpr int kRowCount = 3;
    constexpr int kListGapY = 6;
    const int text_h = Font5x7::H;
    const int total_h = (kRowCount * text_h) + ((kRowCount - 1) * kListGapY);
    const int start_y = (kDisplayH - total_h) / 2;

    char note_value[16];
    char hold_value[16];
    FormatRecordRenderNoteValue(static_cast<int>(ui.record_render_note_offset), note_value, sizeof(note_value));
    FormatRecordRenderHoldValue(static_cast<int>(ui.record_render_hold_ms), hold_value, sizeof(hold_value));

    d.Fill(false);
    DrawTopRightMicroLabel(d, "render");

    DrawRecordRenderValueRow(d,
                             "note",
                             note_value,
                             start_y,
                             (ui.record_render_focus % kRowCount) == 0u);
    DrawRecordRenderValueRow(d,
                             "hold",
                             hold_value,
                             start_y + text_h + kListGapY,
                             (ui.record_render_focus % kRowCount) == 1u);

    const int execute_y = start_y + ((text_h + kListGapY) * 2);
    if((ui.record_render_focus % kRowCount) == 2u)
        DrawFillOnlyTinyString(d, "execute", 2, execute_y);
    else
        DrawTinyString(d, "execute", 2, execute_y, true);
}

bool MainMenu_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.ui)
        return false;
    if(ctx.shift)
        return false;

    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod && e.value != 0)
    {
        const int32_t next = NextMenuIndex(static_cast<int32_t>(ctx.ui->main_menu_index), e.value);
        ctx.ui->main_menu_index = static_cast<uint8_t>(next);
        ctx.ui->ui_dirty = true;
        return true;
    }

    return false;
}

bool MainMenu_OnEnter(UiScreenCtx& ctx)
{
    if(!ctx.ui)
        return false;

    const uint8_t selected = static_cast<uint8_t>(ctx.ui->main_menu_index % kMainMenuCount);
    switch(selected)
    {
        case 0:
            return UiNav_Push(ctx.ui->ui_nav, UiScreenId::Presets);
        case 1:
            return UiNav_Push(ctx.ui->ui_nav, UiScreenId::SamplesMenu);
        case 2:
        default:
            return UiNav_Push(ctx.ui->ui_nav, UiScreenId::PerformMenu);
    }
}

void MainMenu_Render(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.display)
        return;

    const int selected = static_cast<int>(ctx.ui->main_menu_index % kMainMenuCount);
    DrawMainMenuFriendStyle(*ctx.display, selected);
}

bool SamplesMenu_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.ui)
        return false;
    if(ctx.shift)
        return false;

    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod && e.value != 0)
    {
        ctx.ui->samples_menu_index = static_cast<uint8_t>(
            WrapMenuIndex(static_cast<int32_t>(ctx.ui->samples_menu_index), e.value, kSamplesMenuCount));
        ctx.ui->ui_dirty = true;
        return true;
    }

    return false;
}

bool SamplesMenu_OnEnter(UiScreenCtx& ctx)
{
    if(!ctx.ui)
        return false;

    switch(ctx.ui->samples_menu_index % kSamplesMenuCount)
    {
        case 0:
            return UiNav_Push(ctx.ui->ui_nav, UiScreenId::RecordMenu);
        case 1:
            return UiNav_Push(ctx.ui->ui_nav, UiScreenId::CraftMenu);
        case 2:
        default:
            return UiNav_Push(ctx.ui->ui_nav, UiScreenId::SdBrowse);
    }
}

void SamplesMenu_Render(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.display)
        return;

    DrawMenuListStyle(*ctx.display,
                      kSamplesMenuLabels,
                      kSamplesMenuCount,
                      static_cast<int>(ctx.ui->samples_menu_index % kSamplesMenuCount),
                      "samples");
}

bool RecordMenu_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.ui)
        return false;
    if(ctx.shift)
        return false;

    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod && e.value != 0)
    {
        ctx.ui->record_menu_index = static_cast<uint8_t>(
            WrapMenuIndex(static_cast<int32_t>(ctx.ui->record_menu_index), e.value, kRecordMenuCount));
        ctx.ui->ui_dirty = true;
        return true;
    }

    return false;
}

bool RecordMenu_OnEnter(UiScreenCtx& ctx)
{
    if(!ctx.ui)
        return false;

    switch(ctx.ui->record_menu_index % kRecordMenuCount)
    {
        case 0:
            ctx.ui->record_menu_source_override_active = true;
            ctx.ui->record_menu_source_override = static_cast<uint8_t>(RecordInputSource::LineIn);
            return UiNav_Push(ctx.ui->ui_nav, UiScreenId::Record);
        case 1:
            ctx.ui->record_menu_source_override_active = true;
            ctx.ui->record_menu_source_override = static_cast<uint8_t>(RecordInputSource::Mic);
            return UiNav_Push(ctx.ui->ui_nav, UiScreenId::Record);
        case 2:
        default:
            return UiNav_Push(ctx.ui->ui_nav, UiScreenId::RecordRenderMenu);
    }
}

void RecordMenu_Render(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.display)
        return;

    DrawMenuListStyle(*ctx.display,
                      kRecordMenuLabels,
                      kRecordMenuCount,
                      static_cast<int>(ctx.ui->record_menu_index % kRecordMenuCount),
                      "record");
}

bool CraftMenu_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    (void)ctx;
    (void)e;
    return false;
}

void CraftMenu_Render(UiScreenCtx& ctx)
{
    if(!ctx.display)
        return;
    DrawBlankPlaceholder(*ctx.display, "craft");
}

bool RecordRenderMenu_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.ui)
        return false;
    if(ctx.shift)
        return false;

    AppUiState& ui = *ctx.ui;

    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod && e.value != 0)
    {
        ui.record_render_focus = static_cast<uint8_t>(
            WrapMenuIndex(static_cast<int32_t>(ui.record_render_focus), e.value, 3));
        ui.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt && e.value != 0)
    {
        switch(ui.record_render_focus % 3u)
        {
            case 0:
            {
                const int next = ClampRecordRenderInt(static_cast<int>(ui.record_render_note_offset) + e.value,
                                                      -36,
                                                      36);
                if(next != static_cast<int>(ui.record_render_note_offset))
                {
                    ui.record_render_note_offset = static_cast<int8_t>(next);
                    ui.ui_dirty = true;
                }
                return true;
            }
            case 1:
            {
                const int next = ClampRecordRenderInt(static_cast<int>(ui.record_render_hold_ms) + (e.value * 10),
                                                      100,
                                                      500);
                if(next != static_cast<int>(ui.record_render_hold_ms))
                {
                    ui.record_render_hold_ms = static_cast<uint16_t>(next);
                    ui.ui_dirty = true;
                }
                return true;
            }
            default:
                return false;
        }
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPod2)
    {
        ui.record_render_preview_trigger_pending = true;
        ui.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc
       && (ui.record_render_focus % 3u) == 2u)
    {
        return UiNav_Push(ui.ui_nav, UiScreenId::RecordRenderExecute);
    }

    return false;
}

void RecordRenderMenu_Render(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.display)
        return;

    DrawRecordRenderMenuStyle(*ctx.display, *ctx.ui);
}

bool RecordRenderExecute_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    (void)ctx;
    (void)e;
    return false;
}

void RecordRenderExecute_Render(UiScreenCtx& ctx)
{
    if(!ctx.display)
        return;
    DrawBlankPlaceholder(*ctx.display, "execute");
}
