#include "ui_screens_internal.h"
#include "ui_screen_record_internal.h"

#include "app_state_ui.h"
#include "app_state_engine.h"
#include "app_state_recording.h"
#include "app_state_project.h"
#include "app_state_diagnostics.h"
#include "app_state_shared.h"
#include "app_state_worker.h"
#include "oled_pager.h"
#include "sd_sample_pool.h"
#include "ui_input.h"
#include "ui_layout.h"
#include "ui_requests.h"

#include <cstdio>
#include <cstring>

static constexpr int32_t kMainMenuCount = 3;
static const char* kMenuLabels[kMainMenuCount] = {"PRESETS", "SAMPLES", "PERFORM"};
static constexpr int32_t kSamplesMenuCount = 4;
static const char* kSamplesMenuLabels[kSamplesMenuCount] = {"RECORD", "CRAFT", "BAKE", "SD MANAGER"};
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

static void DrawFillOnlyMicroString(OledPager& d, const char* str, int x, int y)
{
    if(!str)
        return;
    const int w = MicroStringWidth(str);
    d.DrawRect(x - 1, y - 1, x + w, y + kMicroH, true, true);
    DrawMicroString(d, str, x, y, false);
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

    if(ui.record_render_status[0] != '\0')
    {
        d.DrawLine(0, 54, 127, 54, true);
        DrawTinyString(d, ui.record_render_status, 2, 57, true);
    }
}

static void SetRecordRenderStatus(AppUiState& ui, const char* msg)
{
    if(!msg)
    {
        ui.record_render_status[0] = '\0';
        return;
    }
    std::snprintf(ui.record_render_status, sizeof(ui.record_render_status), "%s", msg);
}

static void ClearRecordRenderStatus(AppUiState& ui)
{
    ui.record_render_status[0] = '\0';
}

static uint8_t RecordRenderMidiNoteValue(const AppUiState& ui)
{
    const int note = 60 + static_cast<int>(ui.record_render_note_offset);
    if(note < 0)
        return 0u;
    if(note > 127)
        return 127u;
    return static_cast<uint8_t>(note);
}

static bool EqualsIgnoreCase(const char* a, const char* b)
{
    if(!a || !b)
        return false;
    while(*a != '\0' && *b != '\0')
    {
        char ca = *a;
        char cb = *b;
        if(ca >= 'a' && ca <= 'z')
            ca = static_cast<char>(ca - ('a' - 'A'));
        if(cb >= 'a' && cb <= 'z')
            cb = static_cast<char>(cb - ('a' - 'A'));
        if(ca != cb)
            return false;
        ++a;
        ++b;
    }
    return (*a == '\0') && (*b == '\0');
}

static bool RecordRenderNameExistsInBrowser(const SdBrowserState& sd, const char* stem)
{
    if(!stem || stem[0] == '\0')
        return false;

    char full_name[kSdNameMax];
    std::snprintf(full_name, sizeof(full_name), "%s.WAV", stem);
    for(uint8_t i = 0; i < sd.wav_count; ++i)
    {
        if(EqualsIgnoreCase(sd.names[i], full_name))
            return true;
    }
    return false;
}

static void BuildDefaultRenderSaveStem(const AppUiState& ui, char* out, size_t out_n)
{
    if(!out || out_n == 0u)
        return;
    out[0] = '\0';

    if(ui.sd.scan_done)
    {
        for(uint16_t i = 1; i <= 9999u; ++i)
        {
            char stem[kSdRenameStemMax + 1u];
            std::snprintf(stem, sizeof(stem), "REND%04u", static_cast<unsigned>(i));
            if(!RecordRenderNameExistsInBrowser(ui.sd, stem))
            {
                std::snprintf(out, out_n, "%s", stem);
                return;
            }
        }
    }

    std::snprintf(out, out_n, "%s", "RENDER");
}

static void DrawRecordRenderReviewActions(OledPager& d, uint8_t focus)
{
    static const char kSaveLabel[] = "save";
    static const char kRerecordLabel[] = "rerecord";
    const int save_w = MicroStringWidth(kSaveLabel);
    const int rerecord_w = MicroStringWidth(kRerecordLabel);
    const int save_x = 32 - (save_w / 2);
    const int rerecord_x = 96 - (rerecord_w / 2);
    const int y = 56;

    if(focus == 1u)
        DrawFillOnlyMicroString(d, kSaveLabel, save_x, y);
    else
        DrawMicroString(d, kSaveLabel, save_x, y, true);

    if(focus == 2u)
        DrawFillOnlyMicroString(d, kRerecordLabel, rerecord_x, y);
    else
        DrawMicroString(d, kRerecordLabel, rerecord_x, y, true);
}

static bool RecordRenderReviewReturnsToPhysicalRecord(const AppUiState& ui)
{
    return (ui.ui_nav.top > 0u) && (ui.ui_nav.stack[ui.ui_nav.top - 1u] == UiScreenId::Record);
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
            return UiNav_Push(ctx.ui->ui_nav, UiScreenId::BakeMenu);
        case 3:
        default:
            ctx.ui->sd_manage_context_active = true;
            ctx.ui->sd_manage_menu_cursor = 0u;
            ctx.ui->sd_delete_mode = false;
            ctx.ui->sd_rename_mode = false;
            ctx.ui->sample_rename_active = false;
            return UiNav_Push(ctx.ui->ui_nav, UiScreenId::SdManageMenu);
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

static constexpr uint8_t kCraftSlotCount = 3u;
static constexpr uint8_t kCraftPluginCapture = 1u;
static constexpr uint8_t kCraftPluginCount = 7u;
static constexpr uint8_t kCraftCaptureParamCount = 6u;
static constexpr uint8_t kCraftFocusLoad = 0u;
static constexpr uint8_t kCraftFocusSlot = 1u;
static constexpr uint8_t kCraftFocusPlugin = 2u;
static constexpr uint8_t kCraftFocusParamStart = 3u;
static constexpr int32_t kCraftCaptureRateCount = 6;
static constexpr int32_t kCraftCaptureBitsCount = 5;
static constexpr int32_t kCraftCaptureFilterCount = 5;
static constexpr int32_t kCraftCaptureCurveCount = 4;
static constexpr int32_t kCraftSlotNameCount = 3;

static const char* kCraftPluginLabels[kCraftPluginCount]
    = {"----", "capture", "alias", "trans", "body", "fdbk", "multi"};
static const char* kCraftSlotNames[kCraftSlotNameCount] = {"one", "two", "three"};
static const char* kCraftCaptureParamLabels[kCraftCaptureParamCount]
    = {"rate", "bits", "input", "filter", "curve", "age"};
static const char* kCraftCaptureRateLabels[6] = {"48k", "32k", "27k", "22k", "16k", "12k"};
static const char* kCraftCaptureBitsLabels[5] = {"16", "12", "10", "8", "6"};
static const char* kCraftCaptureFilterLabels[5] = {"clean", "soft", "ring", "leak", "bad"};
static const char* kCraftCaptureCurveLabels[4] = {"lin", "comp", "warp", "noisy"};

static int ClampCraftInt(int v, int lo, int hi)
{
    if(v < lo)
        return lo;
    if(v > hi)
        return hi;
    return v;
}

static uint8_t CraftClampSlotIndex(uint8_t slot)
{
    return static_cast<uint8_t>(slot % kCraftSlotCount);
}

static bool CraftActiveSlotHasCapture(const AppUiState& ui)
{
    const uint8_t slot = CraftClampSlotIndex(ui.craft_active_slot);
    return ui.craft_slot_plugin[slot] == kCraftPluginCapture;
}

static uint8_t CraftFocusCount(const AppUiState& ui)
{
    return static_cast<uint8_t>(kCraftFocusParamStart + (CraftActiveSlotHasCapture(ui) ? kCraftCaptureParamCount : 0u));
}

static uint8_t CraftNormalizedFocusIndex(const AppUiState& ui)
{
    return static_cast<uint8_t>(
        WrapMenuIndex(static_cast<int32_t>(ui.craft_focus), 0, static_cast<int32_t>(CraftFocusCount(ui))));
}

static void CraftClampFocusForSelectedSlot(AppUiState& ui)
{
    const uint8_t focus_count = CraftFocusCount(ui);
    if(ui.craft_focus >= focus_count)
        ui.craft_focus = static_cast<uint8_t>(focus_count - 1u);
}

static const char* CraftCaptureParamLabel(uint8_t param)
{
    if(param >= kCraftCaptureParamCount)
        return "";
    return kCraftCaptureParamLabels[param];
}

static int CraftMicroTextWidth(const char* text)
{
    return MicroStringWidth(text);
}

static int CraftTinyTextWidth(const char* text)
{
    return TinyStringWidth(text);
}

static void LowercaseAsciiInPlace(char* text)
{
    if(!text)
        return;
    for(size_t i = 0; text[i] != '\0'; ++i)
    {
        if(text[i] >= 'A' && text[i] <= 'Z')
            text[i] = static_cast<char>(text[i] - 'A' + 'a');
    }
}

static void DrawCraftMicroValue(OledPager& d, const char* value, int x, int y)
{
    if(!value)
        return;
    DrawMicroString(d, value, x, y, true);
}

static void DrawCraftMicroFocusedValueBox(OledPager& d, const char* value, int x, int y)
{
    if(!value)
        return;
    const int value_w = CraftMicroTextWidth(value);
    const int box_w = value_w + 6;
    const int box_h = kMicroH + 4;
    const int box_x = x;
    const int box_y = y - 2;
    d.DrawRect(box_x, box_y, box_x + box_w - 1, box_y + box_h - 1, true, false);
    DrawMicroString(d, value, box_x + 3, y, true);
}

static void DrawCraftMicroInvertedValueBox(OledPager& d, const char* value, int x, int y)
{
    if(!value)
        return;
    const int value_w = CraftMicroTextWidth(value);
    const int box_w = value_w + 6;
    const int box_h = kMicroH + 4;
    const int box_x = x;
    const int box_y = y - 2;
    d.DrawRect(box_x, box_y, box_x + box_w - 1, box_y + box_h - 1, true, true);
    DrawMicroString(d, value, box_x + 3, y, false);
}

static void DrawCraftMicroLabel(OledPager& d, const char* label, int x, int y)
{
    if(!label)
        return;
    DrawMicroString(d, label, x, y, true);
}

static void DrawCraftTinyCaseSensitiveLabel(OledPager& d, const char* label, int x, int y)
{
    if(!label)
        return;
    DrawTinyStringCaseSensitive(d, label, x, y, true);
}

static void DrawCraftTinyLabel(OledPager& d, const char* label, int x, int y)
{
    if(!label)
        return;
    DrawTinyString(d, label, x, y, true);
}

static void DrawCraftTinyFocusedValueBox(OledPager& d, const char* value, int x, int y)
{
    if(!value)
        return;
    const int value_w = CraftTinyTextWidth(value);
    const int box_w = value_w + 6;
    const int box_h = Font5x7::H + 4;
    const int box_x = x;
    const int box_y = y - 2;
    d.DrawRect(box_x, box_y, box_x + box_w - 1, box_y + box_h - 1, true, false);
    DrawTinyString(d, value, box_x + 3, y, true);
}

static void FormatCraftCaptureValue(const AppUiState& ui, uint8_t slot, uint8_t param, char* out, size_t out_n)
{
    if(!out || out_n == 0u)
        return;

    const uint8_t slot_ix = CraftClampSlotIndex(slot);
    switch(param)
    {
        case 0:
            std::snprintf(out,
                          out_n,
                          "%s",
                          kCraftCaptureRateLabels[ui.craft_capture_rate[slot_ix]
                                                  % static_cast<uint8_t>(kCraftCaptureRateCount)]);
            break;
        case 1:
            std::snprintf(out,
                          out_n,
                          "%s",
                          kCraftCaptureBitsLabels[ui.craft_capture_bits[slot_ix]
                                                  % static_cast<uint8_t>(kCraftCaptureBitsCount)]);
            break;
        case 2:
            std::snprintf(out, out_n, "%u", static_cast<unsigned>(ui.craft_capture_input[slot_ix]));
            break;
        case 3:
            std::snprintf(out,
                          out_n,
                          "%s",
                          kCraftCaptureFilterLabels[ui.craft_capture_filter[slot_ix]
                                                    % static_cast<uint8_t>(kCraftCaptureFilterCount)]);
            break;
        case 4:
            std::snprintf(out,
                          out_n,
                          "%s",
                          kCraftCaptureCurveLabels[ui.craft_capture_curve[slot_ix]
                                                   % static_cast<uint8_t>(kCraftCaptureCurveCount)]);
            break;
        case 5:
            std::snprintf(out, out_n, "%u", static_cast<unsigned>(ui.craft_capture_age[slot_ix]));
            break;
        default:
            out[0] = '\0';
            break;
    }
    LowercaseAsciiInPlace(out);
}

static void FormatCraftLoadText(const AppUiState& ui, char* out, size_t out_n)
{
    if(!out || out_n == 0u)
        return;

    std::snprintf(out,
                  out_n,
                  "%s",
                  (ui.craft_loaded_name[0] != '\0') ? ui.craft_loaded_name : "load");
    LowercaseAsciiInPlace(out);

    constexpr int kMaxWidth = 92;
    size_t len = std::strlen(out);
    while(len > 0u && CraftMicroTextWidth(out) > kMaxWidth)
    {
        out[len - 1u] = '\0';
        --len;
    }
}

bool CraftMenu_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.ui)
        return false;
    if(ctx.shift)
        return false;

    AppUiState& ui = *ctx.ui;

    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod && e.value != 0)
    {
        const uint8_t next_focus = static_cast<uint8_t>(WrapMenuIndex(static_cast<int32_t>(ui.craft_focus),
                                                                       e.value,
                                                                       static_cast<int32_t>(CraftFocusCount(ui))));
        if(next_focus != ui.craft_focus)
        {
            ui.craft_focus = next_focus;
            ui.ui_dirty = true;
        }
        return true;
    }

    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt && e.value != 0)
    {
        const uint8_t focus = CraftNormalizedFocusIndex(ui);
        const uint8_t slot = CraftClampSlotIndex(ui.craft_active_slot);
        bool changed = false;
        switch(focus)
        {
            case kCraftFocusSlot:
            {
                const uint8_t next = static_cast<uint8_t>(
                    WrapMenuIndex(static_cast<int32_t>(ui.craft_active_slot), e.value, kCraftSlotNameCount));
                if(next != ui.craft_active_slot)
                {
                    ui.craft_active_slot = next;
                    CraftClampFocusForSelectedSlot(ui);
                    changed = true;
                }
                break;
            }
            case kCraftFocusPlugin:
            {
                const uint8_t next = static_cast<uint8_t>(
                    WrapMenuIndex(static_cast<int32_t>(ui.craft_slot_plugin[slot]), e.value, kCraftPluginCount));
                if(next != ui.craft_slot_plugin[slot])
                {
                    ui.craft_slot_plugin[slot] = next;
                    CraftClampFocusForSelectedSlot(ui);
                    changed = true;
                }
                break;
            }
            default:
            {
                const uint8_t param = static_cast<uint8_t>(focus - kCraftFocusParamStart);
                switch(param)
                {
                    case 0:
                    {
                        const uint8_t next = static_cast<uint8_t>(WrapMenuIndex(
                            static_cast<int32_t>(ui.craft_capture_rate[slot]), e.value, kCraftCaptureRateCount));
                        if(next != ui.craft_capture_rate[slot])
                        {
                            ui.craft_capture_rate[slot] = next;
                            changed = true;
                        }
                        break;
                    }
                    case 1:
                    {
                        const uint8_t next = static_cast<uint8_t>(WrapMenuIndex(
                            static_cast<int32_t>(ui.craft_capture_bits[slot]), e.value, kCraftCaptureBitsCount));
                        if(next != ui.craft_capture_bits[slot])
                        {
                            ui.craft_capture_bits[slot] = next;
                            changed = true;
                        }
                        break;
                    }
                    case 2:
                    {
                        const int next = ClampCraftInt(static_cast<int>(ui.craft_capture_input[slot]) + e.value, 0, 100);
                        if(next != static_cast<int>(ui.craft_capture_input[slot]))
                        {
                            ui.craft_capture_input[slot] = static_cast<uint8_t>(next);
                            changed = true;
                        }
                        break;
                    }
                    case 3:
                    {
                        const uint8_t next = static_cast<uint8_t>(WrapMenuIndex(
                            static_cast<int32_t>(ui.craft_capture_filter[slot]), e.value, kCraftCaptureFilterCount));
                        if(next != ui.craft_capture_filter[slot])
                        {
                            ui.craft_capture_filter[slot] = next;
                            changed = true;
                        }
                        break;
                    }
                    case 4:
                    {
                        const uint8_t next = static_cast<uint8_t>(WrapMenuIndex(
                            static_cast<int32_t>(ui.craft_capture_curve[slot]), e.value, kCraftCaptureCurveCount));
                        if(next != ui.craft_capture_curve[slot])
                        {
                            ui.craft_capture_curve[slot] = next;
                            changed = true;
                        }
                        break;
                    }
                    case 5:
                    {
                        const int next = ClampCraftInt(static_cast<int>(ui.craft_capture_age[slot]) + e.value, 0, 100);
                        if(next != static_cast<int>(ui.craft_capture_age[slot]))
                        {
                            ui.craft_capture_age[slot] = static_cast<uint8_t>(next);
                            changed = true;
                        }
                        break;
                    }
                    default:
                        break;
                }
                break;
            }
        }

        if(changed)
            ui.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        if(CraftNormalizedFocusIndex(ui) != kCraftFocusLoad)
            return false;

        ui.sd_delete_mode = false;
        ui.sd_rename_mode = false;
        ui.sample_rename_active = false;
        ui.sd_manage_context_active = false;
        ui.craft_browser_open = false;
        ui.craft_browser_wait_for_load = false;
        if(UiNav_Push(ui.ui_nav, UiScreenId::SdBrowse))
        {
            ui.craft_browser_open = true;
            ui.ui_dirty = true;
            return true;
        }
        return false;
    }

    return false;
}

void CraftMenu_Render(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.display)
        return;

    AppUiState& ui = *ctx.ui;
    OledPager& d = *ctx.display;
    const uint8_t focus = CraftNormalizedFocusIndex(ui);
    const uint8_t active_slot = CraftClampSlotIndex(ui.craft_active_slot);
    const char* active_slot_name = kCraftSlotNames[active_slot % kCraftSlotNameCount];
    const char* active_plugin_label = kCraftPluginLabels[ui.craft_slot_plugin[active_slot] % kCraftPluginCount];
    char load_label[sizeof(ui.craft_loaded_name)];
    FormatCraftLoadText(ui, load_label, sizeof(load_label));

    d.Fill(false);
    DrawTopRightMicroLabel(d, "craft");

    constexpr int kTopLabelX = 2;
    constexpr int kLoadY = 2;
    constexpr int kTopY = 12;
    constexpr int kTopValueX = 56;
    constexpr int kPluginLabelX = 2;
    constexpr int kPluginY = 22;
    constexpr int kPluginValueX = 56;
    constexpr int kTinySlotLabelY = 10;
    constexpr int kTinyPluginLabelY = 20;

    if(focus == kCraftFocusLoad)
        DrawCraftMicroInvertedValueBox(d, load_label, kTopLabelX, kLoadY);
    else
        DrawCraftMicroValue(d, load_label, kTopLabelX + 3, kLoadY);

    DrawCraftTinyCaseSensitiveLabel(d, "slot#:", kTopLabelX, kTinySlotLabelY);
    if(focus == kCraftFocusSlot)
        DrawCraftTinyFocusedValueBox(d, active_slot_name, kTopValueX, kTopY);
    else
        DrawCraftMicroValue(d, active_slot_name, kTopValueX + 3, kTopY);

    DrawCraftTinyLabel(d, "plugin:", kPluginLabelX, kTinyPluginLabelY);
    if(focus == kCraftFocusPlugin)
        DrawCraftMicroFocusedValueBox(d, active_plugin_label, kPluginValueX, kPluginY);
    else
        DrawCraftMicroValue(d, active_plugin_label, kPluginValueX + 3, kPluginY);

    if(ui.craft_slot_plugin[active_slot] != kCraftPluginCapture)
        return;

    constexpr int kGridX[2] = {2, 66};
    constexpr int kGridY[3] = {34, 43, 52};
    for(uint8_t param = 0; param < kCraftCaptureParamCount; ++param)
    {
        const int col = param % 2;
        const int row = param / 2;
        if(focus == static_cast<uint8_t>(kCraftFocusParamStart + param))
        {
            char value[16];
            FormatCraftCaptureValue(ui, active_slot, param, value, sizeof(value));
            if(param == 0u || param == 1u || param == 2u || param == 5u)
                DrawCraftTinyFocusedValueBox(d, value, kGridX[col], kGridY[row]);
            else
                DrawCraftMicroFocusedValueBox(d, value, kGridX[col], kGridY[row]);
        }
        else
        {
            DrawCraftMicroLabel(d, CraftCaptureParamLabel(param), kGridX[col], kGridY[row]);
        }
    }
}

bool BakeMenu_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    (void)ctx;
    (void)e;
    return false;
}

void BakeMenu_Render(UiScreenCtx& ctx)
{
    if(!ctx.display)
        return;
    DrawBlankPlaceholder(*ctx.display, "bake");
}

bool RecordRenderMenu_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.ui || !ctx.shared || !ctx.worker)
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
                    ClearRecordRenderStatus(ui);
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
                    ClearRecordRenderStatus(ui);
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
        ClearRecordRenderStatus(ui);
        ui.record_render_preview_trigger_pending = true;
        ui.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc
       && (ui.record_render_focus % 3u) == 2u)
    {
        const bool normal_rec_active
            = (ctx.shared->recording.rec_active.load(std::memory_order_acquire) != 0u);
        const bool render_capture_active
            = (ctx.shared->recording.render_active.load(std::memory_order_acquire) != 0u);
        const bool wav_load_busy
            = (ctx.shared->sample.publish.sd_wav_load_busy.load(std::memory_order_acquire) != 0u);
        if(normal_rec_active || render_capture_active)
        {
            SetRecordRenderStatus(ui, "REC BUSY");
            ui.ui_dirty = true;
            return true;
        }
        if(ctx.worker->ui_req_busy || wav_load_busy || ui.record_render_phase == RecordRenderPhase::SaveWait
           || ui.render_sample_rename_wait_for_worker)
        {
            SetRecordRenderStatus(ui, "BUSY");
            ui.ui_dirty = true;
            return true;
        }
        ClearRecordRenderStatus(ui);
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

void RecordRenderExecute_OnEnter(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.shared || !ctx.recording)
        return;

    AppUiState& ui = *ctx.ui;
    AppSharedState& shared = *ctx.shared;
    AppRecordingState& recording = *ctx.recording;

    RecordRender_DiscardTemp(ui, recording, shared);
    ClearRecordRenderStatus(ui);
    ui.record_render_phase = RecordRenderPhase::CaptureStarting;
    ui.record_render_note = RecordRenderMidiNoteValue(ui);
    ui.record_render_capture_started_ms = 0;
    ui.record_render_all_notes_off_sent = false;
    ui.record_render_note_on_due_ms = 0;
    ui.record_render_note_off_due_ms = 0;
    ui.record_render_note_on_sent = false;
    ui.record_render_note_off_sent = false;
    ui.record_render_review_focus = 0;
    ui.render_review_trim_entry = shared.recording.rec_edit;
    ui.render_review_trim_has_entry = false;
    ui.render_sample_rename_active = false;
    ui.render_sample_rename_wait_for_worker = false;
    ui.record_render_save_stem[0] = '\0';
    shared.recording.render_done.store(0, std::memory_order_release);
    shared.recording.render_frames.store(0, std::memory_order_release);
    shared.recording.render_stop_req.store(0, std::memory_order_release);
    shared.recording.render_start_req.store(1, std::memory_order_release);
    ui.ui_dirty = true;
}

bool RecordRenderExecute_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.ui)
        return false;
    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPodEnc)
        return true;
    return false;
}

void RecordRenderExecute_Render(UiScreenCtx& ctx)
{
    if(!ctx.display || !ctx.shared)
        return;

    OledPager& d = *ctx.display;
    d.Fill(false);
    Record_RenderLiveCaptureStyle(ctx, "RENDERING - 5 SEC MAX", 1.0f);
}

bool RecordRenderReview_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.ui || !ctx.shared || !ctx.recording || !ctx.worker)
        return false;
    if(ctx.shift)
        return false;

    AppUiState& ui = *ctx.ui;
    AppSharedState& shared = *ctx.shared;
    AppRecordingState& recording = *ctx.recording;

    if(ui.record_render_phase == RecordRenderPhase::SaveWait)
        return true;

    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod && e.value != 0)
    {
        ui.record_render_review_focus
            = static_cast<uint8_t>(WrapMenuIndex(static_cast<int32_t>(ui.record_render_review_focus),
                                                 e.value,
                                                 3));
        ui.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPodEnc)
    {
        return true;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPod2)
    {
        if(shared.recording.rec_sample.pcm != nullptr && shared.recording.rec_sample.length > 0u)
        {
            recording.record_preview_hold = true;
            recording.record_preview_restart = recording.record_preview_gate;
            ui.ui_dirty = true;
        }
        return true;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        if(ui.record_render_review_focus == 0u)
        {
            if(shared.recording.rec_sample.pcm != nullptr && shared.recording.rec_sample.length > 0u)
            {
                ui.wave_edit_source = WaveEditSource::RenderReview;
                if(UiNav_Push(ui.ui_nav, UiScreenId::PerformWaveEdit))
                    ui.ui_dirty = true;
            }
        }
        else if(ui.record_render_review_focus == 1u)
        {
            char stem[kSdRenameStemMax + 1u];
            if(ui.record_render_save_stem[0] != '\0')
                std::snprintf(stem, sizeof(stem), "%s", ui.record_render_save_stem);
            else
                BuildDefaultRenderSaveStem(ui, stem, sizeof(stem));
            std::snprintf(ui.project_rename_draft, sizeof(ui.project_rename_draft), "%s", stem);
            ui.project_rename_length = static_cast<uint8_t>(std::strlen(ui.project_rename_draft));
            ui.render_sample_rename_active = true;
            ui.render_sample_rename_wait_for_worker = false;
            ClearRecordRenderStatus(ui);
            if(UiNav_Push(ui.ui_nav, UiScreenId::RenameProject))
                ui.ui_dirty = true;
        }
        else
        {
            const bool physical_review = RecordRenderReviewReturnsToPhysicalRecord(ui);
            RecordRender_DiscardTemp(ui, recording, shared);
            UiNav_Pop(ui.ui_nav);
            if(physical_review)
                Record_RestoreArmedSource(ui, recording, shared);
            ui.ui_dirty = true;
        }
        return true;
    }

    return false;
}

void RecordRenderReview_Render(UiScreenCtx& ctx)
{
    if(!ctx.display || !ctx.ui || !ctx.shared)
        return;

    AppUiState& ui = *ctx.ui;
    const Sample& sample = ctx.shared->recording.rec_sample;
    const SampleEdit* edit = (sample.length > 0u) ? &ctx.shared->recording.rec_edit : nullptr;
    const bool waveform_focused = (ui.record_render_review_focus == 0u);
    OledPager& d = *ctx.display;
    d.Fill(false);
    DrawTopRightMicroLabel(d, "review");

    if(ui.record_render_phase == RecordRenderPhase::SaveWait && ui.sd.save_in_progress)
    {
        d.SetCursor(0, 16);
        d.WriteString("SAVING", Font_6x8, true);
        const int bar_x = 8;
        const int bar_y = 30;
        const int bar_w = 112;
        const int bar_h = 8;
        d.DrawRect(bar_x, bar_y, bar_x + bar_w, bar_y + bar_h, true, false);
        const int fill_w = static_cast<int>((static_cast<uint64_t>(bar_w - 2) * ui.sd.save_progress) / 100u);
        if(fill_w > 0)
            d.DrawRect(bar_x + 1, bar_y + 1, bar_x + fill_w, bar_y + bar_h - 1, true, true);
        DrawTinyString(d, ui.sd.save_name, 2, 48, true);
        return;
    }

    if(sample.length > 0u)
    {
        if(waveform_focused)
        {
            d.DrawRect(0, 12, 127, 49, true, true);
            DrawWaveformPreview(d, sample, edit, 0, 12, 128, 38, false, false, false, false);
        }
        else
        {
            DrawWaveformPreview(d, sample, edit, 0, 12, 128, 38, true, false, false, false);
        }
    }
    else
    {
        if(waveform_focused)
        {
            d.DrawRect(0, 12, 127, 49, true, true);
            DrawTinyString(d, "no audio", 40, 30, false);
        }
        else
        {
            DrawTinyString(d, "no audio", 40, 30, true);
        }
    }

    if(ui.record_render_status[0] != '\0')
        DrawTinyString(d, ui.record_render_status, 2, 45, true);

    DrawRecordRenderReviewActions(d, ui.record_render_review_focus);
}
