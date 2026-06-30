#include "ui_screens_internal.h"

#include "app_state_ui.h"
#include "app_state_engine.h"
#include "app_state_recording.h"
#include "app_state_project.h"
#include "app_state_diagnostics.h"
#include "app_state_shared.h"
#include "app_state_worker.h"
#include "oled_pager.h"
#include "project_actions.h"
#include "ui_input.h"
#include "ui_layout.h"
#include "ui_requests.h"
#include "ff.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace
{
static constexpr uint8_t kPresetsVisibleRows = 6;
static constexpr uint8_t kStyleFilterVisibleRows = 5;
static constexpr uint8_t kProjectActionCount = 4;
static constexpr uint8_t kSaveProjectMenuOptionCount = 2;
static constexpr int kProjectRowPitch = Font5x7::H + 2;
static constexpr int kProjectNumberX = 2;
static constexpr int kProjectRowBoxX = 1;
static constexpr int kProjectNameX = 25;
static constexpr int kProjectStyleX = 95;
static constexpr int kProjectHeaderButtonY = 0;
static constexpr int kProjectHeaderButtonH = kMicroH + 3;
static constexpr int kProjectHeaderSeparatorY = 10;
static constexpr int kProjectHeaderTextY = 2;
static constexpr int kProjectRowsStartY = 12;
static constexpr int kProjectFocusedRowTopExtra = 1;
static constexpr int kProjectHeaderNumberX0 = 0;
static constexpr int kProjectHeaderNumberX1 = 18;
static constexpr int kProjectHeaderNameX0 = 22;
static constexpr int kProjectHeaderNameX1 = 76;
static constexpr int kProjectHeaderStyleX0 = 80;
static constexpr int kProjectHeaderStyleX1 = 127;
static constexpr int kProjectOverlayX0 = 16;
static constexpr int kProjectOverlayY0 = 10;
static constexpr int kProjectOverlayX1 = 115;
static constexpr int kProjectOverlayY1 = 54;
static constexpr uint8_t kRenameCols = 9;
static constexpr uint8_t kRenameRows = 4;
static constexpr uint8_t kRenameUiMaxLength = 10;
static constexpr int kRenameNameX = 2;
static constexpr int kRenameNameY = 8;
static constexpr int kRenameSaveY = 1;
static constexpr int kRenameGridX = 9;
static constexpr int kRenameGridY = 20;
static constexpr int kRenameGridXPitch = 13;
static constexpr int kRenameGridYPitch = 11;
static const char kRenameGrid[kRenameRows][kRenameCols + 1] = {
    "abcdefghi",
    "jklmnopqr",
    "stuvwxyz0",
    "123456789",
};
static const char kRenameSaveLabel[] = "save";
static const char kRenameCancelLabel[] = "cancel";
// Bake rename: stem cap = kRenameUiMaxLength minus the visible ".bk"
// suffix the rename screen renders. Per design we keep the .bk
// extension visible (unlike sample renames which hide .wav), so the
// editable portion is capped tighter to keep the total display ≤ 10.
static constexpr uint8_t kBakeRenameStemMax = 7;
static constexpr const char* kBakeRenameSuffix = ".bk";
static constexpr const char* kBakeTempPathInternal = "/_bake.tmp";
static const char kSaveProjectNoneLabel[] = "none";
static const char kProjectStylePlaceholder[] = "----";
static const char kProjectMaxStyleLabel[] = "Water";
static const char* kProjectStyleLabels[kProjectStyleCount] = {
    "----",
    "Fire",
    "Earth",
    "Wind",
    "Water",
};
static const char* kProjectStyleFilterLabels[kProjectStyleCount] = {
    "all",
    "Fire",
    "Earth",
    "Wind",
    "Water",
};

void DrawCenteredRenameSaveOverlay(OledPager& d)
{
    static constexpr int kOverlayX0 = 24;
    static constexpr int kOverlayY0 = 20;
    static constexpr int kOverlayX1 = 103;
    static constexpr int kOverlayY1 = 44;
    static constexpr char kOverlayText[] = "saving";

    d.DrawRect(kOverlayX0, kOverlayY0, kOverlayX1, kOverlayY1, false, true);
    d.DrawRect(kOverlayX0, kOverlayY0, kOverlayX1, kOverlayY1, true, false);

    const int text_w = TinyStringWidth(kOverlayText);
    const int text_x = kOverlayX0 + ((kOverlayX1 - kOverlayX0 + 1) - text_w) / 2;
    const int text_y = kOverlayY0 + ((kOverlayY1 - kOverlayY0 + 1) - Font5x7::H) / 2;
    DrawTinyString(d, kOverlayText, text_x, text_y, true);
}

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

void DrawInvertedAltTinyString(OledPager& d, const char* str, int x, int y)
{
    if(!str)
        return;

    int inner_x0 = x - 1;
    int inner_y0 = y - 1;
    int inner_x1 = x + Font5x7::W;
    int inner_y1 = y + Font5x7::H;

    int gap_x0 = inner_x0 - 1;
    int gap_y0 = inner_y0 - 1;
    int gap_x1 = inner_x1 + 1;
    int gap_y1 = inner_y1 + 1;

    int dotted_x0 = gap_x0 - 1;
    int dotted_y0 = gap_y0 - 1;
    int dotted_x1 = gap_x1 + 1;
    int dotted_y1 = gap_y1 + 1;

    if(gap_x0 < 0) gap_x0 = 0;
    if(gap_y0 < 0) gap_y0 = 0;
    if(gap_x1 > 127) gap_x1 = 127;
    if(gap_y1 > 63) gap_y1 = 63;
    if(inner_x0 < 0) inner_x0 = 0;
    if(inner_y0 < 0) inner_y0 = 0;
    if(inner_x1 > 127) inner_x1 = 127;
    if(inner_y1 > 63) inner_y1 = 63;

    d.DrawRect(gap_x0, gap_y0, gap_x1, gap_y1, false, true);
    d.DrawRect(inner_x0, inner_y0, inner_x1, inner_y1, true, true);
    DrawDottedRect(d, dotted_x0, dotted_y0, dotted_x1, dotted_y1, true);
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

void BuildProjectSlotNumber(uint8_t slot, char* out, size_t out_n)
{
    if(!out || out_n == 0)
        return;
    std::snprintf(out, out_n, "%u.", static_cast<unsigned>(slot + 1u));
}

char ToAsciiLower(char c)
{
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

const char* ProjectStyleLabel(ProjectStyleId style)
{
    const uint8_t index = ProjectStyleToStored(style);
    return (index < kProjectStyleCount) ? kProjectStyleLabels[index] : kProjectStylePlaceholder;
}

const char* ProjectStyleDisplayName(const AppProjectState& project, uint8_t slot)
{
    if(slot >= kProjectSlotCount || !project.slot_has_file[slot])
        return kProjectStylePlaceholder;
    return ProjectStyleLabel(project.slot_styles[slot]);
}

uint8_t ProjectStyleFilterCursor(const AppUiState& ui)
{
    if(ui.presets_style_filter == kProjectStyleFilterAll)
        return 0u;
    const uint8_t stored = ui.presets_style_filter;
    return (stored < kProjectStyleCount) ? stored : 0u;
}

ProjectStyleId ProjectStyleFromCursor(uint8_t cursor)
{
    if(cursor == 0u)
        return ProjectStyleId::None;
    return ProjectStyleFromStored(cursor);
}

uint8_t ProjectStyleOptionCount()
{
    return kProjectStyleCount;
}

void BuildProjectSortName(const AppProjectState& project, uint8_t slot, char* out, size_t out_n)
{
    if(!out || out_n == 0u)
        return;

    out[0] = '\0';
    if(slot >= kProjectSlotCount)
        return;

    if(!project.slot_has_file[slot])
    {
        std::snprintf(out, out_n, "%s", "----");
        return;
    }

    if(project.slot_names[slot][0] != '\0')
    {
        std::snprintf(out, out_n, "%s", project.slot_names[slot]);
        return;
    }

    std::snprintf(out, out_n, "project %02u", static_cast<unsigned>(slot + 1u));
}

int CompareProjectSortNames(const AppProjectState& project, uint8_t lhs_slot, uint8_t rhs_slot)
{
    char lhs[16];
    char rhs[16];
    BuildProjectSortName(project, lhs_slot, lhs, sizeof(lhs));
    BuildProjectSortName(project, rhs_slot, rhs, sizeof(rhs));

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

    if(lhs_slot < rhs_slot)
        return -1;
    if(lhs_slot > rhs_slot)
        return 1;
    return 0;
}

int FindVisibleSlotIndex(const AppProjectState& project, uint8_t slot)
{
    for(uint8_t i = 0; i < project.visible_slot_count; ++i)
    {
        if(project.visible_slot_order[i] == slot)
            return static_cast<int>(i);
    }
    return -1;
}

uint8_t PresetsMaxTopRow(const AppProjectState& project);
void ClampPresetsTopRow(AppUiState& ui, const AppProjectState& project);
void EnsurePresetsRowVisible(AppUiState& ui, const AppProjectState& project, uint8_t row_index);
void MovePresetsFocus(AppUiState& ui, AppProjectState& project, int delta);

void SyncCurrentSlotToFocusedRow(AppUiState& ui, AppProjectState& project)
{
    if(ui.presets_focus_index < kProjectPresetsHeaderCount || project.visible_slot_count == 0u)
        return;

    uint8_t row_index = static_cast<uint8_t>(ui.presets_focus_index - kProjectPresetsHeaderCount);
    if(row_index >= project.visible_slot_count)
        row_index = static_cast<uint8_t>(project.visible_slot_count - 1u);
    ui.presets_focus_index = static_cast<uint8_t>(kProjectPresetsHeaderCount + row_index);
    project.current_project_slot = project.visible_slot_order[row_index];
}

void RebuildVisibleProjectOrder(AppUiState& ui, AppProjectState& project)
{
    const bool row_focus = ui.presets_focus_index >= kProjectPresetsHeaderCount;
    const uint8_t selected_slot = project.current_project_slot;

    uint8_t count = 0;
    for(uint8_t slot = 0; slot < kProjectSlotCount; ++slot)
    {
        if(ui.presets_style_filter != kProjectStyleFilterAll)
        {
            if(!project.slot_has_file[slot])
                continue;
            if(ProjectStyleToStored(project.slot_styles[slot]) != ui.presets_style_filter)
                continue;
        }
        project.visible_slot_order[count++] = slot;
    }

    for(uint8_t i = 0; i < count; ++i)
    {
        for(uint8_t j = static_cast<uint8_t>(i + 1u); j < count; ++j)
        {
            bool swap = false;
            if(ui.presets_sort_mode == ProjectPresetsSortMode::Number)
            {
                swap = ui.presets_sort_descending
                           ? (project.visible_slot_order[i] < project.visible_slot_order[j])
                           : (project.visible_slot_order[i] > project.visible_slot_order[j]);
            }
            else
            {
                const int cmp = CompareProjectSortNames(project,
                                                        project.visible_slot_order[i],
                                                        project.visible_slot_order[j]);
                swap = ui.presets_sort_descending ? (cmp < 0) : (cmp > 0);
            }

            if(swap)
            {
                const uint8_t tmp = project.visible_slot_order[i];
                project.visible_slot_order[i] = project.visible_slot_order[j];
                project.visible_slot_order[j] = tmp;
            }
        }
    }

    project.visible_slot_count = count;
    if(count == 0u)
    {
        ui.presets_top_row = 0u;
        if(row_focus)
            ui.presets_focus_index = static_cast<uint8_t>(kProjectPresetsHeaderCount - 1u);
        return;
    }

    int selected_index = FindVisibleSlotIndex(project, selected_slot);
    if(selected_index < 0)
    {
        selected_index = 0;
        project.current_project_slot = project.visible_slot_order[0];
    }

    if(row_focus)
    {
        ui.presets_focus_index = static_cast<uint8_t>(kProjectPresetsHeaderCount + selected_index);
        EnsurePresetsRowVisible(ui, project, static_cast<uint8_t>(selected_index));
    }
    else
    {
        ClampPresetsTopRow(ui, project);
    }
}

uint8_t PresetsFocusCount(const AppProjectState& project)
{
    return static_cast<uint8_t>(kProjectPresetsHeaderCount + project.visible_slot_count);
}

uint8_t PresetsMaxTopRow(const AppProjectState& project)
{
    return (project.visible_slot_count > kPresetsVisibleRows)
               ? static_cast<uint8_t>(project.visible_slot_count - kPresetsVisibleRows)
               : 0u;
}

void ClampPresetsTopRow(AppUiState& ui, const AppProjectState& project)
{
    const uint8_t max_top = PresetsMaxTopRow(project);
    if(ui.presets_top_row > max_top)
        ui.presets_top_row = max_top;
}

void EnsurePresetsRowVisible(AppUiState& ui, const AppProjectState& project, uint8_t row_index)
{
    ClampPresetsTopRow(ui, project);
    if(row_index < ui.presets_top_row)
        ui.presets_top_row = row_index;
    else if(row_index >= static_cast<uint8_t>(ui.presets_top_row + kPresetsVisibleRows))
        ui.presets_top_row = static_cast<uint8_t>(row_index - (kPresetsVisibleRows - 1u));
    ClampPresetsTopRow(ui, project);
}

void MovePresetsFocus(AppUiState& ui, AppProjectState& project, int delta)
{
    if(delta == 0)
        return;

    const uint8_t focus_count = PresetsFocusCount(project);
    if(focus_count == 0u)
        return;

    const int step = (delta > 0) ? 1 : -1;
    int remaining = (delta > 0) ? delta : -delta;
    while(remaining-- > 0)
    {
        if(ui.presets_focus_index < kProjectPresetsHeaderCount)
        {
            ui.presets_focus_index = WrapCursor(ui.presets_focus_index, step, focus_count);
            if(ui.presets_focus_index >= kProjectPresetsHeaderCount)
            {
                const uint8_t row_index = static_cast<uint8_t>(
                    ui.presets_focus_index - kProjectPresetsHeaderCount);
                EnsurePresetsRowVisible(ui, project, row_index);
            }
            continue;
        }

        uint8_t row_index = static_cast<uint8_t>(ui.presets_focus_index - kProjectPresetsHeaderCount);
        if(step > 0)
        {
            if(row_index + 1u >= project.visible_slot_count)
            {
                ui.presets_focus_index = WrapCursor(ui.presets_focus_index, step, focus_count);
                continue;
            }

            ++row_index;
            if(project.visible_slot_count > kPresetsVisibleRows
               && row_index >= static_cast<uint8_t>(ui.presets_top_row + kPresetsVisibleRows))
                ++ui.presets_top_row;
            ui.presets_focus_index = static_cast<uint8_t>(kProjectPresetsHeaderCount + row_index);
        }
        else
        {
            if(row_index == 0u)
            {
                ui.presets_focus_index = WrapCursor(ui.presets_focus_index, step, focus_count);
                continue;
            }

            --row_index;
            if(row_index < ui.presets_top_row && ui.presets_top_row > 0u)
                --ui.presets_top_row;
            ui.presets_focus_index = static_cast<uint8_t>(kProjectPresetsHeaderCount + row_index);
        }
    }

    ClampPresetsTopRow(ui, project);
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
    // Header cells invert + show a border only when FOCUSED. An active style
    // filter no longer keeps the box inverted (the filtered list is the cue).
    (void)filter_active;
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

    if(focused)
    {
        // Inverted (filled) box + border, only when focused.
        d.DrawRect(box_x0,
                   kProjectHeaderButtonY,
                   box_x1,
                   kProjectHeaderButtonY + kProjectHeaderButtonH,
                   true,
                   true);
        d.DrawRect(box_x0,
                   kProjectHeaderButtonY,
                   box_x1,
                   kProjectHeaderButtonY + kProjectHeaderButtonH,
                   true,
                   false);
    }

    const bool text_on = !focused;
    int text_x = box_x0 + 3;
    if(draw_arrow)
    {
        DrawProjectHeaderGlyph(d,
                               arrow_descending ? "^" : "v",
                               text_x,
                               kProjectHeaderTextY,
                               text_on);
        text_x += arrow_w;
    }
    DrawProjectHeaderGlyph(d, label, text_x, kProjectHeaderTextY, text_on);
}

void DrawProjectHeaderRow(OledPager& d, const AppUiState& ui)
{
    DrawProjectHeaderButton(d,
                            kProjectHeaderNumberX0,
                            kProjectHeaderNumberX1,
                            "#",
                            ui.presets_focus_index == 0u,
                            ui.presets_sort_mode == ProjectPresetsSortMode::Number,
                            ui.presets_sort_descending,
                            false);
    DrawProjectHeaderButton(d,
                            kProjectHeaderNameX0,
                            kProjectHeaderNameX1,
                            "name",
                            ui.presets_focus_index == 1u,
                            ui.presets_sort_mode == ProjectPresetsSortMode::Name,
                            ui.presets_sort_descending,
                            false);
    DrawProjectHeaderButton(d,
                            kProjectHeaderStyleX0,
                            kProjectHeaderStyleX1,
                            "style",
                            ui.presets_focus_index == 2u,
                            ui.presets_style_filter != kProjectStyleFilterAll, // down arrow when filtered
                            false,                                             // descending=false -> "v"
                            ui.presets_style_filter != kProjectStyleFilterAll);
}

void DrawProjectSlotRow(OledPager& d, const AppProjectState& project, uint8_t slot, int row_y, bool focused)
{
    char number[5];
    BuildProjectSlotNumber(slot, number, sizeof(number));
    const char* name = ProjectActions_DisplayName(project, slot);
    const char* style = ProjectStyleDisplayName(project, slot);

    if(focused)
    {
        const int row_w = (kProjectStyleX + TinyStringWidth(kProjectMaxStyleLabel))
                          - kProjectRowBoxX;
        int x0 = kProjectRowBoxX;
        int y0 = row_y - kProjectFocusedRowTopExtra;
        int x1 = kProjectRowBoxX + row_w;
        int y1 = row_y + Font5x7::H;
        if(x0 < 0) x0 = 0;
        if(y0 < 0) y0 = 0;
        if(x1 > 127) x1 = 127;
        if(y1 > 63) y1 = 63;
        d.DrawRect(x0, y0, x1, y1, true, true);
        DrawTinyString(d, number, kProjectNumberX, row_y, false);
        DrawTinyStringCaseSensitive(d, name, kProjectNameX, row_y, false);
        DrawTinyStringCaseSensitive(d, style, kProjectStyleX, row_y, false);
        return;
    }

    DrawTinyString(d, number, kProjectNumberX, row_y, true);
    DrawTinyStringCaseSensitive(d, name, kProjectNameX, row_y, true);
    DrawTinyStringCaseSensitive(d, style, kProjectStyleX, row_y, true);
}

void RenderPresetsList(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.project || !ctx.display)
        return;

    AppUiState& ui = *ctx.ui;
    AppProjectState& project = *ctx.project;
    OledPager& d = *ctx.display;
    RebuildVisibleProjectOrder(ui, project);
    d.Fill(false);

    DrawProjectHeaderRow(d, ui);

    if(project.visible_slot_count == 0u)
    {
        DrawTinyString(d, "none", 52, 28, true);
        return;
    }

    ClampPresetsTopRow(ui, project);
    const uint8_t top_row = ui.presets_top_row;

    for(uint8_t row = 0; row < kPresetsVisibleRows; ++row)
    {
        const uint8_t visible_index = static_cast<uint8_t>(top_row + row);
        if(visible_index >= project.visible_slot_count)
            break;

        const uint8_t slot = project.visible_slot_order[visible_index];
        const int row_y = kProjectRowsStartY + static_cast<int>(row) * kProjectRowPitch;
        const bool focused = (ui.presets_focus_index
                              == static_cast<uint8_t>(kProjectPresetsHeaderCount + visible_index));
        DrawProjectSlotRow(d, project, slot, row_y, focused);
    }
}

void RenderProjectSlotListWindow(UiScreenCtx& ctx, uint8_t focused_slot)
{
    if(!ctx.project || !ctx.display)
        return;

    AppProjectState& project = *ctx.project;
    OledPager& d = *ctx.display;
    d.Fill(false);

    const uint8_t visible_rows = kPresetsVisibleRows;
    if(visible_rows == 0u)
        return;

    const int total_h = static_cast<int>(visible_rows) * Font5x7::H
                        + static_cast<int>(visible_rows - 1u) * 2;
    const int start_y = (static_cast<int>(d.Height()) - total_h) / 2;

    uint8_t top_row = 0;
    if(focused_slot >= visible_rows)
        top_row = static_cast<uint8_t>(focused_slot - (visible_rows - 1u));
    const uint8_t max_top = static_cast<uint8_t>(kProjectSlotCount - visible_rows);
    if(top_row > max_top)
        top_row = max_top;

    for(uint8_t row = 0; row < visible_rows; ++row)
    {
        const uint8_t slot = static_cast<uint8_t>(top_row + row);
        if(slot >= kProjectSlotCount)
            break;

        const int row_y = start_y + static_cast<int>(row) * kProjectRowPitch;
        const bool focused = (slot == focused_slot);
        DrawProjectSlotRow(d, project, slot, row_y, focused);
    }
}

char RenameGridChar(uint8_t row, uint8_t col, bool uppercase)
{
    char c = kRenameGrid[row % kRenameRows][col % kRenameCols];
    if(uppercase && c >= 'a' && c <= 'z')
        c = static_cast<char>(c - 'a' + 'A');
    return c;
}

bool RenameGridHasShiftAlt(uint8_t row, uint8_t col)
{
    const char c = kRenameGrid[row % kRenameRows][col % kRenameCols];
    return c >= 'a' && c <= 'z';
}

uint8_t RenameMaxLength(const AppUiState& ui)
{
    if(ui.bake_rename_active)
        return kBakeRenameStemMax;
    return kRenameUiMaxLength;
}

void ClampRenameDraft(AppUiState& ui)
{
    const uint8_t cap = RenameMaxLength(ui);
    if(ui.project_rename_length > cap)
        ui.project_rename_length = cap;
    ui.project_rename_draft[ui.project_rename_length] = '\0';
}

void RequestProjectSlotMetadata(AppUiState& ui,
                                AppProjectState& project,
                                AppWorkerState& worker)
{
    if(project.metadata_scan_complete || project.metadata_scan_requested)
        return;

    const UiReq req{UiReqType::ScanProjectSlots, 0, 0};
    if(UiReq_Push(ui, worker, req))
        project.metadata_scan_requested = true;
}

void SetProjectStatusImmediate(AppProjectState& project, uint8_t slot, const char* msg)
{
    std::snprintf(project.project_status,
                  sizeof(project.project_status),
                  "P%02u %s",
                  static_cast<unsigned>(slot + 1u),
                  msg ? msg : "");
}

void PopToShiftMenu(UiNav& nav)
{
    while(UiNav_Active(nav) != UiScreenId::ShiftMenu && UiNav_Pop(nav))
    {
    }
}

const char* ActiveProjectDisplayName(const AppProjectState& project)
{
    if(!project.has_active_project_slot)
        return kSaveProjectNoneLabel;
    return ProjectActions_DisplayName(project, project.active_project_slot);
}

uint8_t RenameTargetSlot(const AppUiState& ui, const AppProjectState& project)
{
    return ui.project_rename_for_new_save ? ui.project_rename_new_save_slot
                                          : project.current_project_slot;
}

bool QueueProjectStyleUpdate(AppUiState& ui,
                             AppProjectState& project,
                             AppWorkerState& worker,
                             uint8_t slot,
                             ProjectStyleId style)
{
    project.project_action = ProjectAction::Style;
    project.project_action_slot = slot;
    SetProjectStatusImmediate(project, slot, "STYLING");

    const UiReq req{UiReqType::UpdateProjectStyle, slot, ProjectStyleToStored(style)};
    if(!UiReq_Push(ui, worker, req))
    {
        SetProjectStatusImmediate(project, slot, "ERR");
        UiNav_Pop(ui.ui_nav);
        UiNav_Push(ui.ui_nav, UiScreenId::ProjectStatus);
        ui.ui_dirty = true;
        return false;
    }

    ui.project_style_update_pending = true;
    ui.project_style_update_pending_slot = slot;
    ui.project_style_update_pending_done_count = worker.ui_req_done_count;
    UiNav_Pop(ui.ui_nav);
    ui.ui_dirty = true;
    return true;
}

void ActivatePresetsHeader(AppUiState& ui, AppProjectState& project)
{
    if(ui.presets_focus_index == 0u)
    {
        if(ui.presets_sort_mode == ProjectPresetsSortMode::Number)
            ui.presets_sort_descending = !ui.presets_sort_descending;
        else
        {
            ui.presets_sort_mode = ProjectPresetsSortMode::Number;
            ui.presets_sort_descending = false;
        }
        RebuildVisibleProjectOrder(ui, project);
        ui.ui_dirty = true;
    }
    else if(ui.presets_focus_index == 1u)
    {
        if(ui.presets_sort_mode == ProjectPresetsSortMode::Name)
            ui.presets_sort_descending = !ui.presets_sort_descending;
        else
        {
            ui.presets_sort_mode = ProjectPresetsSortMode::Name;
            ui.presets_sort_descending = false;
        }
        RebuildVisibleProjectOrder(ui, project);
        ui.ui_dirty = true;
    }
}

bool QueueSettingsSaveRequest(AppUiState& ui,
                              AppProjectState& project,
                              AppWorkerState& worker,
                              uint8_t slot)
{
    project.project_action = ProjectAction::Save;
    project.project_action_slot = slot;
    SetProjectStatusImmediate(project, slot, "SAVING");

    const UiReq req{UiReqType::SaveProject, slot, 0};
    if(!UiReq_Push(ui, worker, req))
    {
        SetProjectStatusImmediate(project, slot, "ERR");
        PopToShiftMenu(ui.ui_nav);
        UiNav_Push(ui.ui_nav, UiScreenId::ProjectStatus);
        ui.ui_dirty = true;
        return false;
    }

    PopToShiftMenu(ui.ui_nav);
    ui.save_project_pending = true;
    ui.save_project_pending_slot = slot;
    ui.save_project_pending_done_count = worker.ui_req_done_count;
    ui.ui_dirty = true;
    return true;
}

bool QueueNamedSaveRequest(AppUiState& ui,
                           AppProjectState& project,
                           AppWorkerState& worker,
                           uint8_t slot)
{
    ui.pending_named_save_active = true;
    ui.pending_named_save_slot = slot;
    std::snprintf(ui.pending_named_save_name,
                  sizeof(ui.pending_named_save_name),
                  "%.*s",
                  static_cast<int>(kRenameUiMaxLength),
                  ui.project_rename_draft);
    ui.project_rename_for_new_save = false;

    if(QueueSettingsSaveRequest(ui, project, worker, slot))
        return true;

    ui.project_rename_for_new_save = true;
    ui.project_rename_new_save_slot = slot;
    ui.pending_named_save_active = false;
    ui.pending_named_save_slot = 0;
    ui.pending_named_save_name[0] = '\0';
    return false;
}

bool QueueRenameRequest(AppUiState& ui, AppProjectState& project, AppWorkerState& worker)
{
    project.pending_rename_slot = project.current_project_slot;
    std::snprintf(project.pending_rename_name,
                  sizeof(project.pending_rename_name),
                  "%.*s",
                  static_cast<int>(kRenameUiMaxLength),
                  ui.project_rename_draft);

    const UiReq req{UiReqType::RenameProject, project.pending_rename_slot, 0};
    if(!UiReq_Push(ui, worker, req))
        return false;

    UiNav_Pop(ui.ui_nav);
    UiNav_Pop(ui.ui_nav);
    ui.ui_dirty = true;
    return true;
}

bool QueueRenameSampleRequest(AppUiState& ui, AppWorkerState& worker)
{
    if(!ui.sample_rename_active || ui.project_rename_length == 0u)
        return false;

    const UiReq req{UiReqType::RenameWavIndex, ui.sample_rename_index, 0};
    if(!UiReq_Push(ui, worker, req))
        return false;

    ui.sample_rename_active = false;
    UiNav_Pop(ui.ui_nav);
    if(UiNav_Active(ui.ui_nav) == UiScreenId::SdManageActionMenu)
        UiNav_Pop(ui.ui_nav);
    ui.ui_dirty = true;
    return true;
}

bool RenameEqualsIgnoreCase(const char* a, const char* b)
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

bool RenderSaveNameExistsInBrowser(const AppUiState& ui, const char* stem)
{
    if(!stem || stem[0] == '\0')
        return false;

    char full_name[kSdNameMax];
    std::snprintf(full_name, sizeof(full_name), "%s.WAV", stem);
    for(uint8_t i = 0; i < ui.sd.wav_count; ++i)
    {
        if(RenameEqualsIgnoreCase(ui.sd.names[i], full_name))
            return true;
    }
    return false;
}

bool QueueRenderSaveRequest(AppUiState& ui, AppWorkerState& worker)
{
    if(!ui.render_sample_rename_active || ui.project_rename_length == 0u)
        return false;

    std::snprintf(ui.record_render_save_stem,
                  sizeof(ui.record_render_save_stem),
                  "%.*s",
                  static_cast<int>(kRenameUiMaxLength),
                  ui.project_rename_draft);
    if(ui.sd.scan_done && RenderSaveNameExistsInBrowser(ui, ui.record_render_save_stem))
    {
        std::snprintf(ui.record_render_status, sizeof(ui.record_render_status), "%s", "NAME EXISTS");
        ui.ui_dirty = true;
        return true;
    }

    const UiReq req{UiReqType::SaveRenderedWavNamed, 0, 0};
    if(!UiReq_Push(ui, worker, req))
    {
        std::snprintf(ui.record_render_status, sizeof(ui.record_render_status), "%s", "SAVE ERR");
        ui.ui_dirty = true;
        return false;
    }

    ui.record_render_status[0] = '\0';
    ui.record_render_phase = RecordRenderPhase::SaveWait;
    ui.render_sample_rename_wait_for_worker = true;
    ui.ui_dirty = true;
    return true;
}

bool QueueSdManageTrimSaveRequest(AppUiState& ui, AppWorkerState& worker)
{
    if(!ui.sd_manage_trim_rename_active || ui.project_rename_length == 0u)
        return false;

    std::snprintf(ui.sd_manage_save_stem,
                  sizeof(ui.sd_manage_save_stem),
                  "%.*s",
                  static_cast<int>(kRenameUiMaxLength),
                  ui.project_rename_draft);
    if(ui.sd.scan_done && RenderSaveNameExistsInBrowser(ui, ui.sd_manage_save_stem))
    {
        SdBrowser_SetSaveStatus(ui.sd, "NAME EXISTS");
        ui.ui_dirty = true;
        return true;
    }

    const UiReq req{UiReqType::SaveSdManageTrimNamed, 0, 0};
    if(!UiReq_Push(ui, worker, req))
    {
        SdBrowser_SetSaveStatus(ui.sd, "SAVE ERR");
        ui.ui_dirty = true;
        return false;
    }

    ui.sd_manage_trim_wait_for_worker = true;
    ui.sd_manage_trim_save_busy = true;
    ui.ui_dirty = true;
    return true;
}

bool QueueCraftRenderSave(AppUiState& ui, AppWorkerState& worker)
{
    if(!ui.craft_render_rename_active || ui.project_rename_length == 0u)
        return false;

    std::snprintf(ui.craft_render_save_stem,
                  sizeof(ui.craft_render_save_stem),
                  "%.*s",
                  static_cast<int>(kRenameUiMaxLength),
                  ui.project_rename_draft);

    // New file: render the chain and save under the typed stem (a=0, stem set).
    const UiReq req{UiReqType::CraftRenderToWav, 0u, 0u};
    if(!UiReq_Push(ui, worker, req))
        return false;

    ui.craft_render_overwrite       = false;
    ui.craft_render_wait_for_worker = true;
    ui.craft_render_done_count      = worker.ui_req_done_count;
    ui.craft_render_rename_active   = false;
    UiNav_Pop(ui.ui_nav); // leave the rename screen, back to craft
    ui.ui_dirty = true;
    return true;
}

void BuildRenameDisplayText(const AppProjectState& project,
                            const AppUiState& ui,
                            char* out,
                            size_t out_n)
{
    (void)project;
    if(!out || out_n == 0)
        return;

    if(ui.bake_rename_active)
    {
        // Show the editable stem followed by the literal ".bk" so the
        // user can see what the saved filename will be (the suffix is
        // not part of the draft, just rendered).
        std::snprintf(out, out_n, "%s%s",
                      ui.project_rename_draft,
                      kBakeRenameSuffix);
        return;
    }
    std::snprintf(out, out_n, "%s", ui.project_rename_draft);
}

// Bake collision check. We don't keep a scanned list of .bk files
// (the SD browser only tracks .wav), so just probe FATFS directly.
bool BakeBkExists(const char* full_path)
{
    if(!full_path || full_path[0] == '\0')
        return false;
    FILINFO info;
    return f_stat(full_path, &info) == FR_OK;
}

bool QueueBakeRenameSave(AppUiState& ui)
{
    if(!ui.bake_rename_active || ui.project_rename_length == 0u)
        return false;

    std::snprintf(ui.bake_save_stem,
                  sizeof(ui.bake_save_stem),
                  "%.*s",
                  static_cast<int>(kBakeRenameStemMax),
                  ui.project_rename_draft);

    char target[24];
    std::snprintf(target, sizeof(target), "/%s%s",
                  ui.bake_save_stem, kBakeRenameSuffix);

    if(BakeBkExists(target))
    {
        std::snprintf(ui.bake_save_status,
                      sizeof(ui.bake_save_status),
                      "%s",
                      "NAME EXISTS");
        ui.ui_dirty = true;
        return true;
    }

    if(f_rename(kBakeTempPathInternal, target) != FR_OK)
    {
        std::snprintf(ui.bake_save_status,
                      sizeof(ui.bake_save_status),
                      "%s",
                      "SAVE ERR");
        ui.ui_dirty = true;
        return true;
    }

    ui.bake_rename_active = false;
    ui.bake_save_status[0] = '\0';
    // Bake finished: show a transient "BAKE FILE SAVED" banner (1 s, auto-dismiss
    // via the UI tick) + a green LED flash, and exit to the Samples screen rather
    // than back to the bake screen. Nav stack here is Samples -> Bake -> Rename, so
    // pop twice (Rename then Bake) to land on Samples.
    ui.bake_created_show = false;
    ui.bake_created_flash_pending = true;
    std::snprintf(ui.saved_overlay_text, sizeof(ui.saved_overlay_text), "BAKE FILE SAVED");
    ui.saved_overlay_pending = true;
    UiNav_Pop(ui.ui_nav); // pop RenameProject
    UiNav_Pop(ui.ui_nav); // pop BakeMenu -> Samples
    ui.ui_dirty = true;
    return true;
}

void CancelBakeRename(AppUiState& ui)
{
    // Bake output is discarded — unlink the temp so the next bake
    // starts clean and the SD card doesn't accumulate orphans.
    f_unlink(kBakeTempPathInternal);
    ui.bake_rename_active = false;
    ui.bake_save_status[0] = '\0';
}
} // namespace

void RebuildVisibleProjectOrderFromMetadata(AppUiState& ui, AppProjectState& project)
{
    RebuildVisibleProjectOrder(ui, project);
}

void Presets_OnEnter(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.project || !ctx.worker)
        return;

    RequestProjectSlotMetadata(*ctx.ui, *ctx.project, *ctx.worker);
    RebuildVisibleProjectOrder(*ctx.ui, *ctx.project);
    if(ctx.ui->presets_focus_index >= PresetsFocusCount(*ctx.project))
        ctx.ui->presets_focus_index = kProjectPresetsHeaderCount;
    SyncCurrentSlotToFocusedRow(*ctx.ui, *ctx.project);
}

bool Presets_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.ui || !ctx.project || !ctx.worker)
        return false;

    AppUiState& ui = *ctx.ui;
    AppProjectState& project = *ctx.project;
    RebuildVisibleProjectOrder(ui, project);

    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod && e.value != 0)
    {
        MovePresetsFocus(ui, project, e.value);
        SyncCurrentSlotToFocusedRow(ui, project);
        ui.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        if(ui.presets_focus_index < kProjectPresetsHeaderCount)
        {
            if(ui.presets_focus_index == 2u)
            {
                ui.presets_style_picker_cursor = ProjectStyleFilterCursor(ui);
                if(UiNav_Push(ui.ui_nav, UiScreenId::PresetsStyleFilter))
                    ui.ui_dirty = true;
                return true;
            }

            ActivatePresetsHeader(ui, project);
            return true;
        }

        if(project.visible_slot_count == 0u)
            return true;

        ui.project_action_cursor = 0;
        ui.project_action_style_cursor = ProjectStyleToStored(project.slot_styles[project.current_project_slot]);
        if(UiNav_Push(ui.ui_nav, UiScreenId::ProjectActionMenu))
            ui.ui_dirty = true;
        return true;
    }

    return false;
}

void Presets_Render(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.project || !ctx.display)
        return;

    RenderPresetsList(ctx);
}

bool ProjectActionMenu_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.ui || !ctx.project || !ctx.worker)
        return false;

    AppUiState& ui = *ctx.ui;
    AppProjectState& project = *ctx.project;
    const uint8_t slot = project.current_project_slot;

    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod && e.value != 0)
    {
        ui.project_action_cursor = WrapCursor(ui.project_action_cursor, e.value, kProjectActionCount);
        ui.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt && e.value != 0)
    {
        if(ui.project_action_cursor == 1u && project.slot_has_file[slot])
        {
            ui.project_action_style_cursor = WrapCursor(ui.project_action_style_cursor,
                                                        e.value,
                                                        kProjectStyleCount);
            ui.ui_dirty = true;
            return true;
        }
        return true;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        if(ui.project_action_cursor == 0u)
        {
            UiNav_Pop(ui.ui_nav);
            return ProjectActions_TriggerRequest(ui, project, *ctx.worker, UiReqType::LoadProject, slot);
        }

        if(ui.project_action_cursor == 1u)
        {
            if(project.slot_has_file[slot])
                return QueueProjectStyleUpdate(ui,
                                               project,
                                               *ctx.worker,
                                               slot,
                                               ProjectStyleFromStored(ui.project_action_style_cursor));
            return true;
        }

        if(ui.project_action_cursor == 2u)
        {
            ui.project_rename_for_new_save = false;
            if(project.slot_has_file[slot] && UiNav_Push(ui.ui_nav, UiScreenId::RenameProject))
            {
                ui.ui_dirty = true;
                return true;
            }
            return true;
        }

        if(project.slot_has_file[slot])
        {
            ui.project_delete_mode = true;
            ui.project_delete_slot = slot;
            if(UiNav_Push(ui.ui_nav, UiScreenId::ProjectDeleteConfirm))
                ui.ui_dirty = true;
        }
        ui.project_rename_for_new_save = false;
        return true;
    }

    return false;
}

void ProjectActionMenu_Render(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.project || !ctx.display)
        return;

    AppUiState& ui = *ctx.ui;
    AppProjectState& project = *ctx.project;
    OledPager& d = *ctx.display;

    RenderPresetsList(ctx);

    const char* name = ProjectActions_DisplayName(project, project.current_project_slot);
    const bool slot_has_file = project.slot_has_file[project.current_project_slot];
    const char* style = slot_has_file
                            ? ProjectStyleLabel(ProjectStyleFromStored(ui.project_action_style_cursor))
                            : kProjectStylePlaceholder;

    const int overlay_x0 = kProjectOverlayX0;
    const int overlay_y0 = kProjectOverlayY0;
    const int overlay_x1 = kProjectOverlayX1;
    const int overlay_y1 = kProjectOverlayY1;
    d.DrawRect(overlay_x0, overlay_y0, overlay_x1, overlay_y1, false, true);
    d.DrawRect(overlay_x0, overlay_y0, overlay_x1, overlay_y1, true, false);

    DrawTinyStringCaseSensitive(d, name, overlay_x0 + 4, overlay_y0 + 4, true);
    if(ui.project_action_cursor == 0u)
        DrawFillOnlyTinyString(d, "load", overlay_x0 + 8, overlay_y0 + 18);
    else
        DrawTinyString(d, "load", overlay_x0 + 8, overlay_y0 + 18, true);

    const int style_label_x = overlay_x0 + 50;
    const int style_label_y = overlay_y0 + 18;
    if(ui.project_action_cursor == 1u)
    {
        const int style_label_w = TinyStringWidth(kProjectMaxStyleLabel);
        DrawRencFocusFrame(d, style_label_x, style_label_y, style_label_w, Font5x7::H);
        DrawTinyStringCaseSensitive(d, style, style_label_x, style_label_y, false);
    }
    else
    {
        DrawTinyString(d, "style", style_label_x, style_label_y, true);
    }

    if(ui.project_action_cursor == 2u)
        DrawFillOnlyTinyString(d, "rename", overlay_x0 + 8, overlay_y0 + 32);
    else
        DrawTinyString(d, "rename", overlay_x0 + 8, overlay_y0 + 32, true);

    if(ui.project_action_cursor == 3u && slot_has_file)
        DrawFillOnlyTinyString(d, "delete", style_label_x, overlay_y0 + 32);
    else
        DrawTinyString(d, "delete", style_label_x, overlay_y0 + 32, true);
}

bool PresetsStyleFilter_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.ui || !ctx.project)
        return false;

    AppUiState& ui = *ctx.ui;
    AppProjectState& project = *ctx.project;

    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod && e.value != 0)
    {
        ui.presets_style_picker_cursor = WrapCursor(ui.presets_style_picker_cursor,
                                                    e.value,
                                                    ProjectStyleOptionCount());
        ui.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        ui.presets_style_filter = (ui.presets_style_picker_cursor == 0u)
                                      ? kProjectStyleFilterAll
                                      : ProjectStyleToStored(ProjectStyleFromCursor(
                                            ui.presets_style_picker_cursor));
        RebuildVisibleProjectOrder(ui, project);
        UiNav_Pop(ui.ui_nav);
        ui.ui_dirty = true;
        return true;
    }

    return false;
}

void PresetsStyleFilter_Render(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.project || !ctx.display)
        return;

    AppUiState& ui = *ctx.ui;
    OledPager& d = *ctx.display;
    RenderPresetsList(ctx);

    const int overlay_x0 = 24;
    const int overlay_y0 = 8;
    const int overlay_x1 = 103;
    const int overlay_y1 = 63;
    d.DrawRect(overlay_x0, overlay_y0, overlay_x1, overlay_y1, false, true);
    d.DrawRect(overlay_x0, overlay_y0, overlay_x1, overlay_y1, true, false);

    const uint8_t option_count = ProjectStyleOptionCount();
    uint8_t focused_option = 0u;
    int focused_x = 0;
    int focused_y = 0;
    bool has_focused_option = false;

    for(uint8_t option = 0; option < option_count; ++option)
    {
        int option_x = 0;
        int option_y = 0;
        ComputeStyleFilterOptionPosition(overlay_x0,
                                         overlay_y0,
                                         overlay_x1,
                                         overlay_y1,
                                         kProjectStyleFilterLabels[option],
                                         option,
                                         option_x,
                                         option_y);
        if(option == ui.presets_style_picker_cursor)
        {
            focused_option = option;
            focused_x = option_x;
            focused_y = option_y;
            has_focused_option = true;
            continue;
        }

        DrawTinyStringCaseSensitive(
            d, kProjectStyleFilterLabels[option], option_x, option_y, true);
    }

    if(has_focused_option)
    {
        DrawFillOnlyTinyStringCaseSensitive(
            d, kProjectStyleFilterLabels[focused_option], focused_x, focused_y);
    }
}

void RenameProject_OnEnter(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.project)
        return;

    AppUiState& ui = *ctx.ui;
    AppProjectState& project = *ctx.project;
    ui.ui_lshift_held = false;
    ui.ui_parent_preview_active = false;
    ui.ui_parent_preview_from_top = 0;
    ui.ui_parent_preview_mode = 0;
    ui.ui_parent_preview_origin_screen = UiScreenId::COUNT;
    ui.ui_parent_preview_origin_main_cursor = 0;
    ui.ui_parent_preview_origin_fx_cursor = 0;
    ui.ui_parent_preview_origin_process_detail = false;
    ui.ui_parent_preview_origin_process_eq_graph = false;
    if(ui.sample_rename_active || ui.render_sample_rename_active || ui.sd_manage_trim_rename_active
       || ui.craft_render_rename_active)
    {
        ClampRenameDraft(ui);
        ui.project_rename_grid_col = 0;
        ui.project_rename_grid_row = 0;
        ui.project_rename_focus = ProjectRenameFocus::Grid;
        ui.ui_dirty = true;
        return;
    }
    if(ui.bake_rename_active)
    {
        // Always start with an empty stem — bakes don't carry forward
        // the source-sample name (the rename screen is the user's
        // chance to give the bake its own identity).
        ui.project_rename_draft[0] = '\0';
        ui.project_rename_length = 0;
        ui.bake_save_status[0] = '\0';
        ui.project_rename_grid_col = 0;
        ui.project_rename_grid_row = 0;
        ui.project_rename_focus = ProjectRenameFocus::Grid;
        ui.ui_dirty = true;
        return;
    }
    if(ui.project_rename_for_new_save)
    {
        ui.project_rename_draft[0] = '\0';
    }
    else
    {
        const uint8_t slot = RenameTargetSlot(ui, project);
        std::snprintf(ui.project_rename_draft,
                      sizeof(ui.project_rename_draft),
                      "%s",
                      project.slot_names[slot]);
    }
    ui.project_rename_length = static_cast<uint8_t>(std::strlen(ui.project_rename_draft));
    ClampRenameDraft(ui);
    ui.project_rename_grid_col = 0;
    ui.project_rename_grid_row = 0;
    ui.project_rename_focus = ProjectRenameFocus::Grid;
    ui.ui_dirty = true;
}

bool RenameProject_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.ui || !ctx.project || !ctx.worker)
        return false;

    AppUiState& ui = *ctx.ui;
    AppProjectState& project = *ctx.project;

    if(ui.render_sample_rename_active && ui.render_sample_rename_wait_for_worker)
        return true;
    if(ui.sd_manage_trim_rename_active && ui.sd_manage_trim_wait_for_worker)
        return true;

    if(e.type == UiInputType::EncDelta && e.value != 0)
    {
        if(e.id == kUiEncPod)
        {
            if(ui.project_rename_focus == ProjectRenameFocus::Save
               || ui.project_rename_focus == ProjectRenameFocus::Cancel)
                return true;
            ui.project_rename_grid_col = WrapCursor(ui.project_rename_grid_col, e.value, kRenameCols);
            ui.ui_dirty = true;
            return true;
        }
        if(e.id == kUiEncExt)
        {
            if(ui.project_rename_focus == ProjectRenameFocus::Save
               || ui.project_rename_focus == ProjectRenameFocus::Cancel)
            {
                if(e.value > 0)
                {
                    ui.project_rename_focus = ProjectRenameFocus::Grid;
                    ui.project_rename_grid_row = 0;
                    ui.ui_dirty = true;
                }
                return true;
            }

            if(e.value < 0 && ui.project_rename_grid_row == 0u)
            {
                ui.project_rename_focus = ProjectRenameFocus::Save;
                ui.ui_dirty = true;
                return true;
            }
            ui.project_rename_grid_row = WrapCursor(ui.project_rename_grid_row, e.value, kRenameRows);
            ui.ui_dirty = true;
            return true;
        }
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        if(ui.project_rename_focus == ProjectRenameFocus::Cancel)
        {
            if(ui.sample_rename_active)
                ui.sample_rename_active = false;
            if(ui.render_sample_rename_active)
            {
                ui.render_sample_rename_active = false;
                ui.render_sample_rename_wait_for_worker = false;
            }
            if(ui.sd_manage_trim_rename_active)
            {
                ui.sd_manage_trim_rename_active = false;
                ui.sd_manage_trim_wait_for_worker = false;
                ui.sd_manage_trim_save_busy = false;
            }
            if(ui.bake_rename_active)
                CancelBakeRename(ui);
            if(ui.craft_render_rename_active)
            {
                ui.craft_render_rename_active = false;
                ui.craft_render_wait_for_worker = false;
            }
            ui.project_rename_for_new_save = false;
            UiNav_Pop(ui.ui_nav);
            ui.ui_dirty = true;
            return true;
        }

        if(ui.project_rename_focus == ProjectRenameFocus::Save)
        {
            if(ui.sample_rename_active)
            {
                if(ui.project_rename_length == 0u)
                    return true;
                return QueueRenameSampleRequest(ui, *ctx.worker);
            }
            if(ui.render_sample_rename_active)
            {
                if(ui.project_rename_length == 0u)
                    return true;
                return QueueRenderSaveRequest(ui, *ctx.worker);
            }
            if(ui.sd_manage_trim_rename_active)
            {
                if(ui.project_rename_length == 0u)
                    return true;
                return QueueSdManageTrimSaveRequest(ui, *ctx.worker);
            }
            if(ui.bake_rename_active)
            {
                if(ui.project_rename_length == 0u)
                    return true;
                return QueueBakeRenameSave(ui);
            }
            if(ui.craft_render_rename_active)
            {
                if(ui.project_rename_length == 0u)
                    return true;
                return QueueCraftRenderSave(ui, *ctx.worker);
            }
            if(ui.project_rename_for_new_save)
            {
                if(ui.project_rename_length == 0u)
                    return true;
                return QueueNamedSaveRequest(ui,
                                             project,
                                             *ctx.worker,
                                             ui.project_rename_new_save_slot);
            }
            return QueueRenameRequest(ui, project, *ctx.worker);
        }

        if(ui.project_rename_length >= RenameMaxLength(ui))
            return true;

        if(ui.render_sample_rename_active)
            ui.record_render_status[0] = '\0';
        if(ui.bake_rename_active)
            ui.bake_save_status[0] = '\0';
        ui.project_rename_draft[ui.project_rename_length] =
            RenameGridChar(ui.project_rename_grid_row,
                           ui.project_rename_grid_col,
                           ctx.rshift);
        ++ui.project_rename_length;
        ui.project_rename_draft[ui.project_rename_length] = '\0';
        ui.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPodEnc)
    {
        if(ui.project_rename_length > 0u)
        {
            if(ui.render_sample_rename_active)
                ui.record_render_status[0] = '\0';
            if(ui.bake_rename_active)
                ui.bake_save_status[0] = '\0';
            --ui.project_rename_length;
            ui.project_rename_draft[ui.project_rename_length] = '\0';
            ui.ui_dirty = true;
            return true;
        }

        if(ui.sample_rename_active)
        {
            ui.sample_rename_active = false;
        }
        if(ui.render_sample_rename_active)
        {
            ui.render_sample_rename_active = false;
            ui.render_sample_rename_wait_for_worker = false;
        }
        if(ui.sd_manage_trim_rename_active)
        {
            ui.sd_manage_trim_rename_active = false;
            ui.sd_manage_trim_wait_for_worker = false;
            ui.sd_manage_trim_save_busy = false;
        }
        if(ui.bake_rename_active)
            CancelBakeRename(ui);
        if(ui.craft_render_rename_active)
        {
            ui.craft_render_rename_active = false;
            ui.craft_render_wait_for_worker = false;
        }
        ui.project_rename_for_new_save = false;
        UiNav_Pop(ui.ui_nav);
        ui.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPod2)
    {
        if(ui.project_rename_focus == ProjectRenameFocus::Save)
            ui.project_rename_focus = ProjectRenameFocus::Cancel;
        else
            ui.project_rename_focus = ProjectRenameFocus::Save;
        ui.ui_dirty = true;
        return true;
    }

    return false;
}

void RenameProject_Render(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.project || !ctx.display)
        return;

    AppUiState& ui = *ctx.ui;
    AppProjectState& project = *ctx.project;
    OledPager& d = *ctx.display;
    d.Fill(false);

    char display_name[24];
    BuildRenameDisplayText(project, ui, display_name, sizeof(display_name));
    d.SetCursor(kRenameNameX, kRenameNameY);
    d.WriteString(display_name, Font_6x8, true);

    const bool show_cancel = (ui.project_rename_focus == ProjectRenameFocus::Cancel);
    const bool action_focused = (ui.project_rename_focus == ProjectRenameFocus::Save)
                                || (ui.project_rename_focus == ProjectRenameFocus::Cancel);
    const char* action_label = show_cancel ? kRenameCancelLabel : kRenameSaveLabel;
    const int action_x = static_cast<int>(d.Width()) - TinyStringWidth(action_label) - 3;
    if(action_focused)
        DrawFillOnlyTinyString(d, action_label, action_x, kRenameSaveY);
    else
        DrawTinyString(d, action_label, action_x, kRenameSaveY, true);

    for(uint8_t row = 0; row < kRenameRows; ++row)
    {
        for(uint8_t col = 0; col < kRenameCols; ++col)
        {
            const bool focused = (ui.project_rename_focus == ProjectRenameFocus::Grid)
                                 && (row == ui.project_rename_grid_row)
                                 && (col == ui.project_rename_grid_col);
            const bool has_shift_alt = RenameGridHasShiftAlt(row, col);
            const bool show_shift_alt = focused && has_shift_alt && ctx.rshift;
            char label[2] = {RenameGridChar(row, col, show_shift_alt), '\0'};
            const int x = kRenameGridX + static_cast<int>(col) * kRenameGridXPitch;
            const int y = kRenameGridY + static_cast<int>(row) * kRenameGridYPitch;
            if(focused)
            {
                if(show_shift_alt || !has_shift_alt)
                    DrawFillOnlyTinyStringCaseSensitive(d, label, x, y);
                else
                    DrawInvertedAltTinyString(d, label, x, y);
            }
            else
            {
                DrawTinyStringCaseSensitive(d, label, x, y, true);
            }
        }
    }

    if(ui.render_sample_rename_active && ui.record_render_status[0] != '\0')
        DrawTinyString(d, ui.record_render_status, 2, 56, true);
    else if(ui.sd_manage_trim_rename_active
            && !ui.sd_manage_trim_save_busy
            && ui.sd.save_status[0] != '\0')
        DrawTinyString(d, ui.sd.save_status, 2, 56, true);
    else if(ui.bake_rename_active && ui.bake_save_status[0] != '\0')
        DrawTinyString(d, ui.bake_save_status, 2, 56, true);

    if(ui.sd_manage_trim_rename_active && ui.sd_manage_trim_wait_for_worker && ui.sd_manage_trim_save_busy)
        DrawCenteredRenameSaveOverlay(d);
}

bool SaveProjectMenu_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.ui || !ctx.project || !ctx.worker)
        return false;

    AppUiState& ui = *ctx.ui;
    AppProjectState& project = *ctx.project;
    const bool has_overwrite = project.has_active_project_slot;

    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod && e.value != 0)
    {
        if(has_overwrite)
            ui.save_project_menu_cursor = WrapCursor(ui.save_project_menu_cursor,
                                                     e.value,
                                                     kSaveProjectMenuOptionCount);
        else
            ui.save_project_menu_cursor = 0u;
        ui.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        if(ui.save_project_menu_cursor == 0u)
        {
            ui.save_project_slot_cursor = 0;
            ui.save_project_confirm_cursor = 1;
            if(UiNav_Push(ui.ui_nav, UiScreenId::SaveProjectSlots))
                ui.ui_dirty = true;
            return true;
        }

        if(has_overwrite)
            return QueueSettingsSaveRequest(ui, project, *ctx.worker, project.active_project_slot);
        return true;
    }

    return false;
}

void SaveProjectMenu_Render(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.project || !ctx.display)
        return;

    AppUiState& ui = *ctx.ui;
    const AppProjectState& project = *ctx.project;
    OledPager& d = *ctx.display;

    ShiftMenu_Render(ctx);

    const bool has_overwrite = project.has_active_project_slot;
    const char* active_name = ActiveProjectDisplayName(project);
    const int overlay_x0 = 20;
    const int overlay_y0 = 10;
    const int overlay_x1 = 107;
    const int overlay_y1 = has_overwrite ? 48 : 38;
    d.DrawRect(overlay_x0, overlay_y0, overlay_x1, overlay_y1, false, true);
    d.DrawRect(overlay_x0, overlay_y0, overlay_x1, overlay_y1, true, false);

    DrawTinyStringCaseSensitive(d, active_name, overlay_x0 + 4, overlay_y0 + 4, true);
    if(ui.save_project_menu_cursor == 0u)
        DrawFillOnlyTinyString(d, "new", overlay_x0 + 8, overlay_y0 + 18);
    else
        DrawTinyString(d, "new", overlay_x0 + 8, overlay_y0 + 18, true);

    if(has_overwrite)
    {
        if(ui.save_project_menu_cursor == 1u)
            DrawFillOnlyTinyString(d, "overwrite", overlay_x0 + 8, overlay_y0 + 30);
        else
            DrawTinyString(d, "overwrite", overlay_x0 + 8, overlay_y0 + 30, true);
    }
}

bool SaveProjectSlots_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.ui || !ctx.project || !ctx.worker)
        return false;

    AppUiState& ui = *ctx.ui;
    AppProjectState& project = *ctx.project;

    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod && e.value != 0)
    {
        ui.save_project_slot_cursor = ProjectActions_WrapSlot(static_cast<int>(ui.save_project_slot_cursor)
                                                              + e.value);
        ui.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        const uint8_t slot = ui.save_project_slot_cursor;
        if(project.slot_has_file[slot])
        {
            ui.save_project_confirm_cursor = 1;
            if(UiNav_Push(ui.ui_nav, UiScreenId::SaveProjectConfirm))
                ui.ui_dirty = true;
            return true;
        }

        ui.project_rename_for_new_save = true;
        ui.project_rename_new_save_slot = slot;
        if(UiNav_Push(ui.ui_nav, UiScreenId::RenameProject))
            ui.ui_dirty = true;
        else
            ui.project_rename_for_new_save = false;
        return true;
    }

    return false;
}

void SaveProjectSlots_Render(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.project || !ctx.display)
        return;

    RenderProjectSlotListWindow(ctx, ctx.ui->save_project_slot_cursor);
}

bool SaveProjectConfirm_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.ui || !ctx.project || !ctx.worker)
        return false;

    AppUiState& ui = *ctx.ui;
    AppProjectState& project = *ctx.project;

    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod && e.value != 0)
    {
        ui.save_project_confirm_cursor = WrapCursor(ui.save_project_confirm_cursor, e.value, 2u);
        ui.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        if(ui.save_project_confirm_cursor == 0u)
            return QueueSettingsSaveRequest(ui, project, *ctx.worker, ui.save_project_slot_cursor);

        UiNav_Pop(ui.ui_nav);
        ui.ui_dirty = true;
        return true;
    }

    return false;
}

void SaveProjectConfirm_Render(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.project || !ctx.display)
        return;

    AppUiState& ui = *ctx.ui;
    AppProjectState& project = *ctx.project;
    OledPager& d = *ctx.display;

    SaveProjectSlots_Render(ctx);

    const char* label = ProjectActions_DisplayName(project, ui.save_project_slot_cursor);
    const int backdrop_x0 = 8;
    const int backdrop_y0 = 8;
    const int backdrop_x1 = 119;
    const int backdrop_y1 = 56;
    const int overlay_x0 = 16;
    const int overlay_y0 = 16;
    const int overlay_x1 = 111;
    const int overlay_y1 = 48;
    d.DrawRect(backdrop_x0, backdrop_y0, backdrop_x1, backdrop_y1, false, true);
    d.DrawRect(overlay_x0, overlay_y0, overlay_x1, overlay_y1, false, true);
    DrawTinyString(d, "overwrite?", overlay_x0 + 6, overlay_y0 + 4, true);
    DrawTinyString(d, label, overlay_x0 + 6, overlay_y0 + 14, true);
    if(ui.save_project_confirm_cursor == 0u)
        DrawFillOnlyTinyString(d, "yes", overlay_x0 + 8, overlay_y0 + 26);
    else
        DrawTinyString(d, "yes", overlay_x0 + 8, overlay_y0 + 26, true);

    if(ui.save_project_confirm_cursor == 1u)
        DrawFillOnlyTinyString(d, "no", overlay_x0 + 52, overlay_y0 + 26);
    else
        DrawTinyString(d, "no", overlay_x0 + 52, overlay_y0 + 26, true);
}
