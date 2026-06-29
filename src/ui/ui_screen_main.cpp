#include "ui_screens_internal.h"
#include "ui_screen_record_internal.h"

#include "app_state_ui.h"
#include "app_state_engine.h"
#include "app_state_recording.h"
#include "app_state_project.h"
#include "app_state_diagnostics.h"
#include "app_state_shared.h"
#include "app_state_worker.h"
#include "bake_psola.h"
#include "craft/craft_chain.h"
#include "ui_worker_craft.h"
#include "bk_file_format.h"
#include "bk_file_reader.h"
#include "bk_file_writer.h"
#include "ff.h"
#include "mem_regions.h"
#include "sampler_sample.h"
#include "sd_sample_pool.h"

// Wrappers exposed from main.cpp (where the DaisyPod handle + display
// renderer live statically). The PSOLA bake uses these to suspend the
// audio interrupt for the duration of pitch renders, and to push display
// updates from inside its blocking main-loop work so the progress overlay
// actually advances on the OLED.
extern "C" void Pod_StopAudio();
extern "C" void Pod_StartAudio();
extern "C" void Pod_ForceDisplayRefresh();
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
static const char* kRecordMenuLabels[kRecordMenuCount] = {"LINE IN", "MICROPHONE", "PERFORM"};

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
    const int text_x = x + 2;
    if(std::strcmp(label, "samples") == 0)
    {
        DrawMicroString(d, "sam", text_x, 2, false);
        DrawMicroString(d, "p", text_x + (3 * kMicroAdvance) + 1, 2, false);
        DrawMicroString(d, "les", text_x + (4 * kMicroAdvance), 2, false);
        return;
    }
    DrawMicroString(d, label, text_x, 2, false);
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
    if(value_focused)
        d.DrawRect(box_x, box_y, box_x + box_w - 1, box_y + box_h - 1, true, false);
    DrawTinyString(d, value, box_x + 3, y, true);
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
    DrawTopRightMicroLabel(d, "perform");

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

// Draws only the selected [start,end) window of a sample, stretched to fill the
// preview rectangle. The review screen shows just the trimmed window (the full
// waveform lives in the trim editor); min/max peaks per pixel column.
static void DrawSampleWindow(OledPager& d,
                             const Sample& sample,
                             uint32_t start,
                             uint32_t end,
                             int x,
                             int y,
                             int w,
                             int h,
                             bool on)
{
    if(sample.pcm == nullptr || sample.length == 0u || w <= 0 || h <= 0)
        return;
    if(end > sample.length)
        end = sample.length;
    if(start >= end)
        start = (end > 0u) ? (end - 1u) : 0u;
    int cols = w;
    if(cols > 128)
        cols = 128;
    const int mid = y + h / 2;
    const int amp_h = (h - 1) / 2;

    static int16_t col_min[128];
    static int16_t col_max[128];
    const int peak = WaveformColumns(sample.pcm, start, end, cols, col_min, col_max);
    // Auto-fit the window peak to ~95% of the half-height (5% margin).
    const float disp = 0.95f * static_cast<float>(amp_h) / static_cast<float>(peak);
    for(int px = 0; px < cols; ++px)
    {
        int top = mid - static_cast<int>(static_cast<float>(col_max[px]) * disp);
        int bot = mid - static_cast<int>(static_cast<float>(col_min[px]) * disp);
        if(top < y)
            top = y;
        if(bot > y + h - 1)
            bot = y + h - 1;
        if(bot < top)
            bot = top;
        d.DrawLine(x + px, top, x + px, bot, on);
    }
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

// Reset the SD Manager screen state to the "fresh entry" defaults: focus on
// the header row, sort by number ascending, no style filter, no rename/delete
// modes. Used by both the samples-menu SD MANAGER entry and the bake screen's
// sample-row entry so both paths land on the same predictable starting state.
static void ResetSdManageEntryState(AppUiState& ui)
{
    ui.sd_delete_mode                = false;
    ui.sample_rename_active          = false;
    ui.sd_manage_focus_index         = kProjectPresetsHeaderCount;
    ui.sd_manage_top_row             = 0u;
    ui.sd_manage_sort_mode           = ProjectPresetsSortMode::Number;
    ui.sd_manage_sort_descending     = false;
    ui.sd_manage_style_filter        = kSampleStyleFilterAll;
    ui.sd_manage_style_picker_cursor = 0u;
    ui.sd_manage_visible_count       = 0u;
    ui.sd_manage_current_index       = 0u;
    ui.sd_manage_action_cursor       = 0u;
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
            ResetSdManageEntryState(*ctx.ui);
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
static constexpr uint8_t kCraftPluginCount = 14u;
static constexpr uint8_t kCraftFocusLoad = 0u;
static constexpr uint8_t kCraftFocusSlot = 1u;
static constexpr uint8_t kCraftFocusPlugin = 2u;
static constexpr uint8_t kCraftFocusParamStart = 3u;
static constexpr int32_t kCraftSlotNameCount = 3;

// CRAFT plugin labels — Home Frequency voice (V2). Names are plain world-words;
// the mechanism each one drives is unchanged from the original spec:
//   copy = generation-loss digital degrade (was "capture")
//   dial = ring-mod / heterodyne "between stations" (was "alias" slot)
//   snap = transient burn (was "trans")
//   warm = body saturation (was "body")
//   howl = feedback fold / comb resonance (was "fdbk")
//   warp = tape wow & flutter (was "multi" slot)
//   fresh = Audio Refresh — STFT spectral-whitening exciter (sonicWORX decode)
//   thru = identity STFT passthrough — bare-framing CPU baseline (Spectral Toolkit)
//   zero = Phase Zeroing — keep magnitude, align all phases (robotic monotone)
//   rand = Phase Randomization — keep magnitude, scatter phase (tone -> pad/noise)
//   still = Spectral Freeze — capture one frame and hold it (sustained spectral pad)
//   thick = Spectral Thickening — peak-detect + harmonic sidebands (octave/fifth/third)
//   smear = Spectral Delay — per-bin delay (lows lag, highs lead); smears across freq
static const char* kCraftPluginLabels[kCraftPluginCount]
    = {"----", "copy", "dial", "snap", "warm", "howl", "warp", "fresh", "thru", "zero", "rand", "still", "thick", "smear"};
static const char* kCraftSlotNames[kCraftSlotNameCount] = {"one", "two", "three"};
// CRAFT param labels + value sets now live in the descriptor table
// (src/dsp/craft/craft_params.{h,cpp}), the single source of truth shared by
// the UI and the DSP. Per-(plugin,param) mapping for "copy": rate=sample-rate
// reduction, bits=bit-depth crush, drive=input coloration, tone=bandwidth
// filter, curve=transfer curve, wear=age/wear instability.
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

static uint8_t CraftActivePlugin(const AppUiState& ui)
{
    const uint8_t slot = CraftClampSlotIndex(ui.craft_active_slot);
    return static_cast<uint8_t>(ui.craft_slot_plugin[slot] % kCraftPluginCount);
}

static uint8_t CraftActiveParamCount(const AppUiState& ui)
{
    return craft::CraftPluginParamCount(CraftActivePlugin(ui));
}

// Focus layout: Load(0), Slot(1), Plugin(2), [params...], Render(last).
// The Render action is always present (it needs a loaded sample, not a plugin).
static uint8_t CraftRenderFocusIndex(const AppUiState& ui)
{
    return static_cast<uint8_t>(kCraftFocusParamStart + CraftActiveParamCount(ui));
}

static uint8_t CraftFocusCount(const AppUiState& ui)
{
    return static_cast<uint8_t>(kCraftFocusParamStart + CraftActiveParamCount(ui) + 1u);
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

static const char* CraftParamLabel(uint8_t plugin, uint8_t param)
{
    const craft::CraftParamDesc* d = craft::CraftGetParamDesc(plugin, param);
    return (d && d->label) ? d->label : "";
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

static void FormatCraftParamValue(const AppUiState& ui, uint8_t slot, uint8_t param, char* out, size_t out_n)
{
    if(!out || out_n == 0u)
        return;
    out[0] = '\0';

    const uint8_t                slot_ix = CraftClampSlotIndex(slot);
    const uint8_t                plugin  = CraftActivePlugin(ui);
    const craft::CraftParamDesc* d       = craft::CraftGetParamDesc(plugin, param);
    if(!d)
        return;

    const uint8_t raw = ui.craft_param[slot_ix][plugin][param];
    if(d->kind == craft::CraftParamKind::Enum)
    {
        const uint8_t count = (d->count > 0u) ? d->count : 1u;
        const uint8_t idx   = static_cast<uint8_t>(raw % count);
        std::snprintf(out, out_n, "%s", (d->enum_labels && d->enum_labels[idx]) ? d->enum_labels[idx] : "");
    }
    else if(d->vcenter != 0u)
    {
        // Bipolar display: raw stored 0..N, shown signed as (raw - vcenter).
        const int v = static_cast<int>(raw) - static_cast<int>(d->vcenter);
        if(v == 0)
            std::snprintf(out, out_n, "0");
        else
            std::snprintf(out, out_n, "%+d", v);
    }
    else
    {
        std::snprintf(out, out_n, "%u", static_cast<unsigned>(raw));
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

    // Button2: start the preview audition. Zero-latency (cheap) configs run LIVE on
    // the audio thread (A1) — instant, params heard in real time, UI fully free.
    // Latency (STFT/fresh) configs still use the worker render-then-play path until
    // engine gating lands (A2). Ignored while a render is already in flight.
    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPod2)
    {
        if(!ctx.shared || ui.craft_loaded_path[0] == '\0')
            return true;
        if(ctx.shared->bake_preview.craft_render_active.load(std::memory_order_acquire) != 0u)
            return true;
        // A2b: ALL effects — cheap AND STFT (fresh) — audition LIVE on the audio
        // thread. Engine gating frees the CPU for the live STFT. The worker render
        // path is now unused (removed in A3).
        CraftPreviewStartLive(ui, *ctx.shared);
        ui.ui_dirty = true;
        return true;
    }

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
        // audio_changed: only edits that alter the rendered result (plugin swap,
        // param value). Slot-view navigation and the save-target toggle do not
        // dirty the preview. Drives craft_preview_dirty -> orange LED.
        bool audio_changed = false;
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
                    changed       = true;
                    audio_changed = true;
                }
                break;
            }
            default:
            {
                // Render focus: Ext rotate toggles new (left) / overwrite (right).
                if(focus == CraftRenderFocusIndex(ui))
                {
                    const bool next_over = (e.value > 0);
                    if(next_over != ui.craft_render_overwrite)
                    {
                        ui.craft_render_overwrite = next_over;
                        changed = true;
                    }
                    break;
                }

                // Param focus: descriptor-driven edit (enum wrap / scalar clamp).
                const uint8_t                param  = static_cast<uint8_t>(focus - kCraftFocusParamStart);
                const uint8_t                plugin = CraftActivePlugin(ui);
                const craft::CraftParamDesc* d      = craft::CraftGetParamDesc(plugin, param);
                if(d)
                {
                    uint8_t& v = ui.craft_param[slot][plugin][param];
                    if(d->kind == craft::CraftParamKind::Enum)
                    {
                        const int32_t count = (d->count > 0u) ? static_cast<int32_t>(d->count) : 1;
                        const uint8_t next  = static_cast<uint8_t>(
                            WrapMenuIndex(static_cast<int32_t>(v % count), e.value, count));
                        if(next != v)
                        {
                            v             = next;
                            changed       = true;
                            audio_changed = true;
                        }
                    }
                    else
                    {
                        const int next = ClampCraftInt(
                            static_cast<int>(v) + e.value, static_cast<int>(d->vmin), static_cast<int>(d->count));
                        if(next != static_cast<int>(v))
                        {
                            v             = static_cast<uint8_t>(next);
                            changed       = true;
                            audio_changed = true;
                        }
                    }
                }
                break;
            }
        }

        if(changed)
        {
            ui.ui_dirty = true;
            // Render-then-play model: an audio-affecting edit invalidates the
            // in-RAM preview, so flag it for re-render (LED2 -> orange). The
            // preview is no longer live-updated mid-playback.
            if(audio_changed)
            {
                // Live audition running: push the new config straight to the audio
                // thread (heard instantly) and stay "clean" (LED green). Otherwise
                // flag the worker re-render and restart its debounce.
                if(ctx.shared
                   && ctx.shared->bake_preview.craft_chain_active.load(std::memory_order_acquire) != 0u)
                {
                    PublishCraftCfgLive(ui, *ctx.shared);
                    ui.craft_preview_dirty = false;
                }
                else
                {
                    ui.craft_preview_dirty    = true;
                    ui.craft_preview_dirty_ms = e.t_ms;
                }
            }
        }
        return true;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        const uint8_t focus = CraftNormalizedFocusIndex(ui);
        if(focus == kCraftFocusLoad)
        {
            ui.sd_delete_mode = false;
            ui.sample_rename_active = false;
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

        if(focus == CraftRenderFocusIndex(ui))
        {
            // Render the chain to a sample (new file or overwrite source).
            // Requires a loaded source.
            if(ui.craft_loaded_path[0] == '\0')
                return true;
            const UiReq req{UiReqType::CraftRenderToWav,
                            static_cast<uint16_t>(ui.craft_render_overwrite ? 1u : 0u),
                            0u};
            if(ctx.worker && UiReq_Push(*ctx.ui, *ctx.worker, req))
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

    // Render action — always present (Ext-click triggers it). Sits on the free
    // right side of the plugin row; shows the save mode (new / overwrite).
    const char* render_text = ui.craft_render_overwrite ? "r:ovr" : "r:new";
    constexpr int kRenderX  = 92;
    constexpr int kRenderY  = kPluginY;
    if(focus == CraftRenderFocusIndex(ui))
        DrawCraftMicroFocusedValueBox(d, render_text, kRenderX, kRenderY);
    else
        DrawCraftMicroValue(d, render_text, kRenderX + 3, kRenderY);

    // Param grid (descriptor-driven) for the active plugin. Plugins with no
    // params (not yet implemented) simply draw nothing here.
    const uint8_t plugin      = CraftActivePlugin(ui);
    const uint8_t param_count = CraftActiveParamCount(ui);
    constexpr int kGridX[2]   = {2, 66};
    constexpr int kGridY[3]   = {34, 43, 52};
    for(uint8_t param = 0; param < param_count; ++param)
    {
        const int col = param % 2;
        const int row = param / 2;
        if(focus == static_cast<uint8_t>(kCraftFocusParamStart + param))
        {
            char value[16];
            FormatCraftParamValue(ui, active_slot, param, value, sizeof(value));
            const craft::CraftParamDesc* pd = craft::CraftGetParamDesc(plugin, param);
            const bool use_micro = (pd && pd->kind == craft::CraftParamKind::Enum && std::strlen(value) > 3u);
            if(use_micro)
                DrawCraftMicroFocusedValueBox(d, value, kGridX[col], kGridY[row]);
            else
                DrawCraftTinyFocusedValueBox(d, value, kGridX[col], kGridY[row]);
        }
        else
        {
            DrawCraftMicroLabel(d, CraftParamLabel(plugin, param), kGridX[col], kGridY[row]);
        }
    }
}

// Bake screen: three focusable rows — sample / root note / bake button.
// LEnc scroll cycles focus with wrap. REnc scroll on focused root changes the
// MIDI note (clamped 0..127, no wrap). REnc Click is dispatched by the router
// to BakeMenu_OnEnter below. LEnc Click is handled by the router (pop nav).
//
// The bake button is a no-op stub in this slice — the bake worker, progress
// screen, and cancel chord are deferred to a later plan.
// Focus indices: 0=sample, 1=root, 2=range, 3=bake. Row 3 is a visual
// blank spacer; row 4 (bake) is reached by focus value 3.
static constexpr int32_t kBakeFocusCount = 4;

// Bake range selector: four width presets, each centered on the canonical
// C4-anchored position. Width index N gives a default range
// [kBakeRangeDefaultLo[N], kBakeRangeDefaultHi[N]] in MIDI numbers; the
// user can then shift both bounds by ±12 per RShift+REnc click.
static constexpr uint8_t kBakeRangeDefaultLo[4] = { 60u, 48u, 36u, 24u }; // C4,C3,C2,C1
static constexpr uint8_t kBakeRangeDefaultHi[4] = { 72u, 84u, 96u, 108u }; // C5,C6,C7,C8
static constexpr uint8_t kBakeRangeWidthCount    = 4u;

static inline uint8_t BakeRangeLo_(uint8_t width_idx, int8_t pos_off)
{
    const int v = int(kBakeRangeDefaultLo[width_idx]) + 12 * int(pos_off);
    return static_cast<uint8_t>(v);
}
static inline uint8_t BakeRangeHi_(uint8_t width_idx, int8_t pos_off)
{
    const int v = int(kBakeRangeDefaultHi[width_idx]) + 12 * int(pos_off);
    return static_cast<uint8_t>(v);
}
// Returns true if shifting `width_idx`'s default range by `pos_off`
// octaves leaves both bounds within [kLoNote, kHiNote] (C1..C8).
static inline bool BakeRangePosOffsetValid_(uint8_t width_idx, int8_t pos_off)
{
    const int lo = int(kBakeRangeDefaultLo[width_idx]) + 12 * int(pos_off);
    const int hi = int(kBakeRangeDefaultHi[width_idx]) + 12 * int(pos_off);
    return lo >= int(bk::kLoNote) && hi <= int(bk::kHiNote);
}

// STAGE 2 TEMPORARY: PSOLA test-bake fired by REnc Click on the bake button.
// The whole sequence runs synchronously on the main thread:
//   1. Load the picked .wav from SD into the bake-preview SDRAM buffer.
//   2. PSOLA-pitch up 1 semitone (chunked progress).
//   3. PSOLA-pitch down 1 semitone (chunked progress).
//   4. Write /test.bk with source at root + the two pitched slices + 82
//      silent slots.
// Audio is suspended for the duration (PSOLA is heavy + main-loop-blocking).
// Pod_ForceDisplayRefresh() is called between phases so the progress
// overlay on the bake screen updates as work advances.
//
// Stage 3 replaces this with the rename screen + dedicated progress
// screen + async worker dispatch + full 84-pitch bake.
//
// Result codes (returned, mapped to status string by the caller):
//   0 = success
//   1 = no sample picked
//   2 = WAV load / parse / format failure
//   3 = PSOLA failed (likely heap)
//   4 = .bk write failed

// Single PSOLA scratch buffer (.sdram_bss). The streaming writer ships each
// slice to SD as soon as PSOLA fills this buffer, then the next slice
// overwrites it. 47 simultaneous slices would be ~22 MB; one shared buffer
// is 480 KB.
ADSR2_SECTION(".sdram_bss") static int16_t s_bake_slice_scratch[bake::kMaxFrames];

// Bake writes here first; on success we push the rename screen and
// f_rename the temp into /<stem>.bk. On cancel we f_unlink it. Any
// orphaned temp from a prior crash is unlinked at the start of every
// bake so it can never collide with the next write.
static constexpr const char* kBakeTempPath = "/_bake.tmp";
// Phase labels for the macro steps of the bake (not the PSOLA-internal
// labels — those come from kPsolaPhaseLabel inside bake_psola.cpp).
static void BakeProgress_SetMacro_(AppUiState& ui,
                                   uint8_t     slice_done,
                                   uint8_t     percent,
                                   const char* label)
{
    ui.bake_progress_active     = true;
    ui.bake_progress_slice_done = slice_done;
    ui.bake_progress_percent    = percent;
    std::snprintf(ui.bake_progress_label,
                  sizeof(ui.bake_progress_label),
                  "%s",
                  label ? label : "");
    Pod_ForceDisplayRefresh();
}

// State threaded into the PSOLA chunk callback. Per-slice base percent
// + slice-done index let one shared C-style callback handle both pitches.
struct PsolaProgressState
{
    AppUiState* ui;
    uint8_t     slice_done_during; // X to show during this slice's chunks
    uint8_t     base_percent;      // bar start for this slice
    uint8_t     span_percent;      // bar span for this slice
};
static PsolaProgressState s_psola_progress;

static void BakePsolaProgressCb_(uint8_t chunk_index, const char* label)
{
    AppUiState* ui = s_psola_progress.ui;
    if(ui == nullptr)
        return;
    // Linear within-slice progress: chunk_index 0 → 0%, chunk_index
    // (kPsolaChunks-1) → (span * (kPsolaChunks-1)/kPsolaChunks)%.
    const uint32_t within = (static_cast<uint32_t>(chunk_index) * s_psola_progress.span_percent)
                            / bake::kPsolaChunks;
    const uint8_t percent = static_cast<uint8_t>(s_psola_progress.base_percent + within);
    BakeProgress_SetMacro_(*ui, s_psola_progress.slice_done_during, percent, label);
}

// BkWriter_End silence-pad progress callback. Drives the bar from 0→100%
// during the silence-fill phase (slots the bake didn't populate) so the
// user sees movement instead of a frozen 100% bar while ~10 MB of zeros
// stream to SD. The slice_done counter holds at psola_done so the X/N
// readout stays on the meaningful PSOLA count instead of jumping.
static AppUiState* s_bake_finalize_ui         = nullptr;
static uint8_t     s_bake_finalize_psola_done = 0;
static void BakeFinalizeProgressCb_(uint32_t pad_done, uint32_t pad_total)
{
    if(s_bake_finalize_ui == nullptr || pad_total == 0u)
        return;
    char buf[20];
    std::snprintf(buf, sizeof(buf), "finalizing %u/%u",
                  static_cast<unsigned>(pad_done),
                  static_cast<unsigned>(pad_total));
    const uint8_t percent
        = static_cast<uint8_t>((static_cast<uint64_t>(pad_done) * 100u) / pad_total);
    BakeProgress_SetMacro_(*s_bake_finalize_ui, s_bake_finalize_psola_done, percent, buf);
}

static int BakeMenu_RunPsolaTestBake_(AppUiState& ui, AppSharedState& shared)
{
    if(ui.bake_sample_path[0] == '\0')
        return 1;

    // The source is pre-loaded into the SDRAM bake-preview buffer at
    // sample-select time (see SdManageMenu_OnEvent's bake-pick branch in
    // ui_screen_sd_browse.cpp, which pushes a LoadWavToBakePreview UiReq).
    // If the user pressed bake before the worker finished the load, the
    // Sample handle will still be unpopulated.
    const Sample& src_sample = shared.bake_preview.sample;
    if(src_sample.pcm == nullptr || src_sample.length == 0u)
        return 2;

    uint32_t source_frames = src_sample.length;
    if(source_frames > bake::kMaxFrames)
        source_frames = bake::kMaxFrames;
    const int16_t* source = src_sample.pcm;

    const uint8_t root = ui.bake_root_note;

    // Pull the selected bake range from the range row's UI state. The
    // .bk file written to SD is exactly (hi - lo + 1) slots wide; layer-B
    // buffer is sized for the max (85) but only this many slices land on
    // disk.
    const uint8_t range_lo = BakeRangeLo_(ui.bake_range_width_idx,
                                          ui.bake_range_position_offset);
    const uint8_t range_hi = BakeRangeHi_(ui.bake_range_width_idx,
                                          ui.bake_range_position_offset);
    const uint8_t range_slice_count = static_cast<uint8_t>(range_hi - range_lo + 1u);

    // Count PSOLA jobs: every slot in the selected range that isn't the
    // root and whose semitone delta is within ±kMaxSemitones. Notes the
    // PSOLA engine can't reach (e.g. shifting >48 semitones at the
    // widest C1-C8 with an extreme root) fall through to silence.
    int psola_planned = 0;
    for(uint8_t midi = range_lo; midi <= range_hi; ++midi)
    {
        const int delta = int(midi) - int(root);
        if(delta == 0)
            continue; // root slot, not PSOLA
        if(delta < -bake::kMaxSemitones || delta > bake::kMaxSemitones)
            continue;
        ++psola_planned;
    }
    ui.bake_progress_slice_total = static_cast<uint8_t>(psola_planned);

    // Begin the progress overlay BEFORE stopping audio so the first frame
    // of the overlay is on screen by the time PSOLA crunches CPU.
    BakeProgress_SetMacro_(ui, 0u, 0u, "psola: setup");
    Pod_StopAudio();

    bk::BkFileHeader hdr = bk::MakeDefaultHeader();
    hdr.source_duration_samples = source_frames;
    hdr.root_midi_note          = root;
    hdr.lo_note                 = range_lo;
    hdr.hi_note                 = range_hi;
    hdr.algorithm_id            = static_cast<uint8_t>(bk::kAlgorithmPsola);
    std::snprintf(hdr.source_name, sizeof(hdr.source_name), "%s", ui.bake_sample_name);

    // Clear any leftover temp from a crashed bake so the next f_open
    // (FA_CREATE_ALWAYS) is always writing a fresh file at a known path.
    f_unlink(kBakeTempPath);

    bk::BkWriter w;
    if(!bk::BkWriter_Begin(w, kBakeTempPath, hdr, source_frames))
    {
        Pod_StartAudio();
        ui.bake_progress_active = false;
        return 4;
    }

    // Fused compute+write loop. Walks the selected range in forward order;
    // for each slot, picks one of:
    //   - root:           write the unmodified source.
    //   - in-range pitch: PSOLA into s_bake_slice_scratch, write the scratch.
    //   - PSOLA out-of-engine-range: write a silence slice.
    // X/N counter advances only on PSOLA outputs. The bar reflects overall
    // slot progress (slot_offset+1)/range_slice_count so it climbs
    // monotonically through any silence and PSOLA slots alike.
    auto slot_base_pct = [&](uint8_t slot_off) -> uint8_t {
        return static_cast<uint8_t>((uint32_t(slot_off) * 100u) / uint32_t(range_slice_count));
    };
    auto slot_done_pct = [&](uint8_t slot_off) -> uint8_t {
        return static_cast<uint8_t>((uint32_t(slot_off + 1u) * 100u) / uint32_t(range_slice_count));
    };

    int psola_done = 0;
    for(uint8_t midi = range_lo; midi <= range_hi; ++midi)
    {
        const uint8_t slot_off = static_cast<uint8_t>(midi - range_lo);
        const int     delta    = int(midi) - int(root);
        const int16_t* src = nullptr;

        if(midi == int(root))
        {
            src = source;
        }
        else if(delta >= -bake::kMaxSemitones && delta <= bake::kMaxSemitones
                && psola_planned > 0)
        {
            // Compute this slot's PSOLA into the shared scratch buffer.
            // PSOLA's within-chunk progress fills the slot's bar window.
            s_psola_progress.ui                = &ui;
            s_psola_progress.slice_done_during = static_cast<uint8_t>(psola_done);
            s_psola_progress.base_percent      = slot_base_pct(slot_off);
            s_psola_progress.span_percent
                = static_cast<uint8_t>(slot_done_pct(slot_off) - slot_base_pct(slot_off));
            if(!bake::RunPitchShiftChunked(source,
                                           source_frames,
                                           delta,
                                           s_bake_slice_scratch,
                                           BakePsolaProgressCb_))
            {
                bk::BkWriter_Close(w, kBakeTempPath); // best-effort, unlinks
                Pod_StartAudio();
                ui.bake_progress_active = false;
                return 3;
            }
            src = s_bake_slice_scratch;
        }
        // else: src stays nullptr → silence slot.

        if(!bk::BkWriter_WriteSlice(w, src))
        {
            bk::BkWriter_Close(w, "/test.bk"); // best-effort, unlinks
            Pod_StartAudio();
            ui.bake_progress_active = false;
            return 4;
        }

        // Snap the bar to this slot's done-boundary and pick a label that
        // tells the user what just happened. The X/N counter only bumps
        // for real PSOLA outputs.
        const char* label;
        if(src == s_bake_slice_scratch)
        {
            ++psola_done;
            label = "psola: write";
        }
        else if(src == source)
        {
            label = "writing root";
        }
        else
        {
            label = "writing silence";
        }
        BakeProgress_SetMacro_(ui,
                               static_cast<uint8_t>(psola_done),
                               slot_done_pct(slot_off),
                               label);
    }

    // Finalize: TWO distinct phases, both visible in the bar. (1) Silence
    // pad — usually 0 slots now that the main loop walks every slot 0..71
    // and writes silence inline; PadOnly is only a safety net for partial
    // walks. (2) f_close — FATFS flushes the FAT chain + directory entry
    // for the ~34 MB file; this is the multi-second stall and previously
    // looked like a frozen "done" bar.
    BakeProgress_SetMacro_(ui, static_cast<uint8_t>(psola_done), 0u, "finalizing: pad");
    s_bake_finalize_ui = &ui;
    s_bake_finalize_psola_done = static_cast<uint8_t>(psola_done);
    const bool padded = bk::BkWriter_PadOnly(w, BakeFinalizeProgressCb_);
    s_bake_finalize_ui = nullptr;
    if(!padded)
    {
        bk::BkWriter_Close(w, kBakeTempPath); // best-effort, unlinks
        Pod_StartAudio();
        ui.bake_progress_active = false;
        return 4;
    }
    // Flip to the close label BEFORE f_close so the user sees the
    // transition; the bar holds at 100% during the (silent, multi-second)
    // FATFS flush instead of looking stalled on "finalizing: pad 37/37".
    BakeProgress_SetMacro_(ui, static_cast<uint8_t>(psola_done), 100u, "finalizing: close");
    if(!bk::BkWriter_Close(w, kBakeTempPath))
    {
        Pod_StartAudio();
        ui.bake_progress_active = false;
        return 4;
    }
    BakeProgress_SetMacro_(ui, static_cast<uint8_t>(psola_done), 100u, "done");
    Pod_StartAudio();

    // Final state: the overlay can hang briefly at 100% until the user
    // interacts, then we clear. Caller will set bake_progress_active=false
    // when it writes the status string.
    return 0;
}

// OnEnter (screen-activation) slot: fires every time BakeMenu becomes the
// active screen — including when popping back from the SD-Manager picker.
// Clears the bake_browser_open flag so a subsequent SD-manager entry from a
// *different* screen cannot inadvertently route its selection into bake
// state. The SD-manager selection path already clears the flag on a
// successful pick; this catches the LEnc-Click "cancel without selecting"
// case where the flag would otherwise stay stuck true. Also defensively stops
// any in-flight bake preview — covers the case where the user pressed
// Button2, then LEnc-clicked to exit before the worker finished loading.
void BakeMenu_OnScreenEnter(UiScreenCtx& ctx)
{
    if(!ctx.ui)
        return;
    ctx.ui->bake_browser_open = false;
    if(ctx.shared)
        ctx.shared->bake_preview.stop_req.store(1, std::memory_order_release);
}

bool BakeMenu_OnEnter(UiScreenCtx& ctx)
{
    if(!ctx.ui)
        return false;
    AppUiState& ui = *ctx.ui;
    switch(ui.bake_focus)
    {
        case 0:
            // Sample row: open the SD Manager in bake-pick mode. The full SD
            // Manager UI (sort + style filter + rich list rendering) is reused;
            // SdManageMenu_OnEvent reads bake_browser_open and on REnc Click on
            // a sample row routes the picked path back into bake state instead
            // of pushing the SdManageActionMenu overlay (no load/rename/delete
            // in bake mode). Initial state matches a fresh samples-menu entry.
            ResetSdManageEntryState(ui);
            ui.bake_browser_open = true;
            if(UiNav_Push(ui.ui_nav, UiScreenId::SdManageMenu))
            {
                ui.ui_dirty = true;
                return true;
            }
            // Push failed (stack full): undo the flag so a stale value can't
            // hijack a future SD-manager entry from a different screen.
            ui.bake_browser_open = false;
            return false;
        case 1:
            // Root note: REnc Click does nothing; scroll changes value.
            return false;
        case 2:
            // Range row: REnc Click does nothing; scroll changes width,
            // RShift+scroll shifts position (both in OnEvent below).
            return false;
        case 3:
        default:
        {
            // Trigger PSOLA bake. Progress overlay (bake_progress_*) is
            // driven from inside RunPsolaTestBake_; we clear it here once
            // the call returns. Failure surfaces via the progress label
            // sticking on a final state until the next bake (no
            // bake_test_status row to fall back on now that the screen is
            // the 5-row layout).
            if(!ctx.shared)
            {
                BakeProgress_SetMacro_(ui, 0u, 0u, "no shared");
                ui.ui_dirty = true;
                return true;
            }
            const int rc = BakeMenu_RunPsolaTestBake_(ui, *ctx.shared);
            if(rc == 0)
            {
                // Bake succeeded: the .bk payload is on SD at kBakeTempPath
                // waiting for a name. Route into the shared rename screen
                // in bake mode; RenameProject_OnEvent's save path will
                // f_rename the temp into /<stem>.bk and pop back here.
                ui.bake_progress_active = false;
                ui.bake_rename_active   = true;
                ui.bake_save_stem[0]    = '\0';
                ui.bake_save_status[0]  = '\0';
                if(!UiNav_Push(ui.ui_nav, UiScreenId::RenameProject))
                {
                    // Nav stack full — orphan would linger forever, so
                    // clean up and surface the failure on the bake screen.
                    f_unlink(kBakeTempPath);
                    ui.bake_rename_active = false;
                    BakeProgress_SetMacro_(ui, 0u, 0u, "nav full");
                }
            }
            else
            {
                const char* msg = "WRITE FAIL";
                switch(rc)
                {
                    case 1: msg = "no sample";     break;
                    case 2: msg = "still loading"; break;
                    case 3: msg = "PSOLA FAIL";    break;
                    case 4: msg = "WRITE FAIL";    break;
                }
                BakeProgress_SetMacro_(ui, 0u, 0u, msg);
            }
            ui.ui_dirty = true;
            return true;
        }
    }
}

bool BakeMenu_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.ui)
        return false;

    AppUiState& ui = *ctx.ui;

    // RShift + REnc scroll on the range row shifts the range position by
    // ±1 octave (no-op at the C1/C8 boundary). Handled before the
    // non-shift early-return below so the modifier chord reaches here.
    // ctx.shift is LShift only — use ctx.rshift for RShift.
    if(ctx.rshift && e.type == UiInputType::EncDelta && e.id == kUiEncExt
       && e.value != 0 && ui.bake_focus == 2u)
    {
        const int step = (e.value > 0) ? 1 : -1;
        const int8_t next_off = static_cast<int8_t>(ui.bake_range_position_offset + step);
        if(BakeRangePosOffsetValid_(ui.bake_range_width_idx, next_off))
        {
            ui.bake_range_position_offset = next_off;
            ui.ui_dirty = true;
        }
        return true;
    }

    if(ctx.shift || ctx.rshift)
        return false;

    // LEnc scroll cycles focus with wrap.
    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod && e.value != 0)
    {
        ui.bake_focus = static_cast<uint8_t>(WrapMenuIndex(
            static_cast<int32_t>(ui.bake_focus), e.value, kBakeFocusCount));
        ui.ui_dirty = true;
        return true;
    }

    // REnc scroll changes the root note when the root row is focused.
    // Clamp to [0, 127] (no wrap) — per design Q4.
    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt && e.value != 0
       && ui.bake_focus == 1u)
    {
        int next = static_cast<int>(ui.bake_root_note) + static_cast<int>(e.value);
        if(next < 0) next = 0;
        if(next > 127) next = 127;
        const uint8_t clamped = static_cast<uint8_t>(next);
        if(clamped != ui.bake_root_note)
        {
            ui.bake_root_note = clamped;
            ui.ui_dirty = true;
        }
        return true;
    }

    // REnc scroll on the range row cycles the width index (0..3), clamped
    // at the ends. Changing width snaps position back to the canonical
    // C4-centered default so the cycle reads the way the UI promises:
    // C4-C5, C3-C6, C2-C7, C1-C8.
    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt && e.value != 0
       && ui.bake_focus == 2u)
    {
        int next = static_cast<int>(ui.bake_range_width_idx) + static_cast<int>(e.value);
        if(next < 0) next = 0;
        if(next > int(kBakeRangeWidthCount) - 1) next = int(kBakeRangeWidthCount) - 1;
        const uint8_t clamped = static_cast<uint8_t>(next);
        if(clamped != ui.bake_range_width_idx)
        {
            ui.bake_range_width_idx       = clamped;
            ui.bake_range_position_offset = 0;
            ui.ui_dirty = true;
        }
        return true;
    }

    return false;
}

void BakeMenu_Render(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.display)
        return;
    OledPager&        d  = *ctx.display;
    const AppUiState& ui = *ctx.ui;

    d.Fill(false);
    DrawTopRightMicroLabel(d, "bake");

    constexpr int kDisplayH = 64;
    constexpr int kListLeftX = 4;
    constexpr int kRowGapY   = 4;
    const int     text_h     = Font5x7::H;

    // 5 visual rows: sample, root, range, [blank spacer], bake. Row 3 is
    // a non-focusable gap that pushes bake down to the screen's lower
    // edge (where the old psola-status tag used to live).
    constexpr int kRowCount = 5;
    const int total_h = (kRowCount * text_h) + ((kRowCount - 1) * kRowGapY);
    const int start_y = (kDisplayH - total_h) / 2;
    auto row_y = [&](int idx) { return start_y + idx * (text_h + kRowGapY); };

    // Row 0: sample (literal "sample" placeholder, or selected sample name).
    {
        const int y = row_y(0);
        const char* val
            = (ui.bake_sample_path[0] != '\0') ? ui.bake_sample_name : "sample";
        if(ui.bake_focus == 0u)
            DrawFillOnlyTinyString(d, val, kListLeftX, y);
        else
            DrawTinyString(d, val, kListLeftX, y, true);
    }

    // Row 1: root note. Label "root:" + formatted note (e.g. "C4"). When the
    // row is focused, an outline-only border surrounds the note (with 1px gap
    // between glyphs and border) to signal "REnc scroll changes value". No
    // inverted fill — the text stays normal so it remains readable.
    {
        const int y = row_y(1);
        DrawTinyString(d, "root:", kListLeftX, y, true);
        const int note_x = kListLeftX + TinyStringWidth("root:") + 4;
        char      note_buf[8];
        FormatMidiNoteName(ui.bake_root_note, note_buf, sizeof(note_buf));
        // Note text uses the case-sensitive variant so the '#' in "C#4" / "F#5"
        // / etc. renders via its dedicated glyph (DrawTinyString downcases and
        // its glyph table has no '#' entry, so '#' falls back to a '?' shape).
        // Width matches char_w * len for non-colon strings, so TinyStringWidth
        // is still correct for the border geometry below.
        const int note_w = TinyStringWidth(note_buf);
        DrawTinyStringCaseSensitive(d, note_buf, note_x, y, true);
        if(ui.bake_focus == 1u)
        {
            d.DrawRect(note_x - 2,
                       y - 2,
                       note_x + note_w + 1,
                       y + Font5x7::H + 1,
                       true,
                       false);
        }
    }

    // Row 2: range. Label "range" + formatted "Lo-Hi" (e.g. "C4-C5").
    // When focused, the value gets a focus border:
    //   - no RShift:    DOTTED border (REnc scroll changes width).
    //   - RShift held:  SOLID  border (REnc scroll shifts position by octave).
    // The visual swap gives a per-frame indicator that the chord is active.
    {
        const int y = row_y(2);
        DrawTinyString(d, "range", kListLeftX, y, true);
        const uint8_t lo = BakeRangeLo_(ui.bake_range_width_idx,
                                        ui.bake_range_position_offset);
        const uint8_t hi = BakeRangeHi_(ui.bake_range_width_idx,
                                        ui.bake_range_position_offset);
        char lo_buf[8];
        char hi_buf[8];
        FormatMidiNoteName(lo, lo_buf, sizeof(lo_buf));
        FormatMidiNoteName(hi, hi_buf, sizeof(hi_buf));
        char value_buf[20];
        std::snprintf(value_buf, sizeof(value_buf), "%s-%s", lo_buf, hi_buf);
        const int value_x = kListLeftX + TinyStringWidth("range") + 4;
        const int value_w = TinyStringWidth(value_buf);
        DrawTinyStringCaseSensitive(d, value_buf, value_x, y, true);
        if(ui.bake_focus == 2u)
        {
            const int bx0 = value_x - 2;
            const int by0 = y - 2;
            const int bx1 = value_x + value_w + 1;
            const int by1 = y + Font5x7::H + 1;
            if(ctx.rshift)
                d.DrawRect(bx0, by0, bx1, by1, true, false);
            else
                DrawDottedRect(d, bx0, by0, bx1, by1, true);
        }
    }

    // Row 3: blank spacer (no draw).

    // Row 4: bake button.
    {
        const int y = row_y(4);
        if(ui.bake_focus == 3u)
            DrawFillOnlyTinyString(d, "bake", kListLeftX, y);
        else
            DrawTinyString(d, "bake", kListLeftX, y, true);
    }

    // STAGE 2 TEMPORARY: bake-in-progress overlay. Drawn LAST so it covers
    // the underlying bake screen during a bake. Layout:
    //   [centered]   X/2
    //   [centered]   <psola phase label>
    //   [centered]   [████████░░░░░░░░] (progress bar, 80 px)
    if(ui.bake_progress_active)
    {
        constexpr int kBoxX0 = 8;
        constexpr int kBoxY0 = 8;
        constexpr int kBoxX1 = 119;
        constexpr int kBoxY1 = 55;
        // Black fill + white border modal box.
        d.DrawRect(kBoxX0, kBoxY0, kBoxX1, kBoxY1, false, true);
        d.DrawRect(kBoxX0, kBoxY0, kBoxX1, kBoxY1, true, false);

        // Top: "X/2" centered.
        char xy_buf[16];
        std::snprintf(xy_buf,
                      sizeof(xy_buf),
                      "%u/%u",
                      static_cast<unsigned>(ui.bake_progress_slice_done),
                      static_cast<unsigned>(ui.bake_progress_slice_total));
        const int xy_w  = TinyStringWidth(xy_buf);
        const int xy_x  = (kBoxX0 + kBoxX1 - xy_w) / 2;
        const int xy_y  = kBoxY0 + 4;
        DrawTinyString(d, xy_buf, xy_x, xy_y, true);

        // Middle: phase label centered.
        const int lbl_w = TinyStringWidth(ui.bake_progress_label);
        const int lbl_x = (kBoxX0 + kBoxX1 - lbl_w) / 2;
        const int lbl_y = kBoxY0 + 4 + Font5x7::H + 6;
        DrawTinyString(d, ui.bake_progress_label, lbl_x, lbl_y, true);

        // Bottom: progress bar (outline + filled portion).
        const int bar_x0 = kBoxX0 + 8;
        const int bar_x1 = kBoxX1 - 8;
        const int bar_y0 = kBoxY1 - 10;
        const int bar_y1 = kBoxY1 - 4;
        d.DrawRect(bar_x0, bar_y0, bar_x1, bar_y1, true, false);
        const int bar_inner_w = bar_x1 - bar_x0 - 2;
        const int fill_w
            = (bar_inner_w * static_cast<int>(ui.bake_progress_percent)) / 100;
        if(fill_w > 0)
            d.DrawRect(bar_x0 + 1, bar_y0 + 1, bar_x0 + 1 + fill_w, bar_y1 - 1, true, true);
    }
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
        // Button2 auditions only the selected window (press-toggle), matching the
        // trim editor and the windowed review display.
        Sample& s = shared.recording.rec_sample;
        if(s.pcm != nullptr && s.length > 0u)
        {
            if(shared.recording.win_preview_active.load(std::memory_order_acquire) != 0u)
            {
                shared.recording.win_preview_stop_req.store(1, std::memory_order_release);
            }
            else
            {
                uint32_t ws = shared.recording.rec_edit.start_frame;
                uint32_t we = shared.recording.rec_edit.end_frame;
                if(we > s.length)
                    we = s.length;
                if(we <= ws)
                    we = s.length;
                shared.recording.win_preview_sample = s;
                shared.recording.win_preview_start = ws;
                shared.recording.win_preview_end = we;
                shared.recording.win_preview_start_req.store(1, std::memory_order_release);
            }
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
            if(ui.project_rename_length > 10u)
            {
                ui.project_rename_length = 10u;
                ui.project_rename_draft[ui.project_rename_length] = '\0';
            }
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
        // Show only the selected window; the full waveform lives in the trim editor.
        const uint32_t win_start = edit ? edit->start_frame : 0u;
        const uint32_t win_end = edit ? edit->end_frame : sample.length;
        if(waveform_focused)
        {
            d.DrawRect(0, 12, 127, 49, true, true);
            DrawSampleWindow(d, sample, win_start, win_end, 0, 12, 128, 38, false);
        }
        else
        {
            DrawSampleWindow(d, sample, win_start, win_end, 0, 12, 128, 38, true);
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
