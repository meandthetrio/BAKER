#include "ui_screens_internal.h"

#include "app_state.h"
#include "ui_input.h"
#include "ui_list_menu.h"
#include "ui_value_edit.h"
#include "ui_layout.h"
#include "oled_pager.h"
#include "ui_requests.h"
#include "sd_browser_state.h"
#include "sample_edit.h"

#include <cstdio>
#include <cstring>

using namespace daisy;

void SdBrowse_OnEnter(UiScreenCtx& ctx)
{
    if(!ctx.app)
        return;

    SdBrowserState& sd = ctx.app->sd;
    const UiLayout layout = UiLayout_Default();
    const uint8_t rows = (layout.rows_body > 1) ? static_cast<uint8_t>(layout.rows_body - 1) : 1;
    if(!sd.menu_inited || sd.menu_rows != rows)
    {
        sd.menu_rows = rows;
        SdBrowser_RebuildMenu(sd);
    }

    if(!sd.scan_in_progress && !sd.scan_done)
    {
        UiReq req{UiReqType::ScanSdWavs, 0, 0};
        UiReq_Push(*ctx.app, req);
        sd.scan_in_progress = true;
        SdBrowser_SetStatus(sd, "SCANNING");
        ctx.app->ui_dirty = true;
    }
}

bool SdBrowse_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.app)
        return false;

    SdBrowserState& sd = ctx.app->sd;
    if(ctx.shift)
        return false;

    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod)
    {
        if(UiListMenu_OnEnc(sd.menu, e.value))
        {
            ctx.app->ui_dirty = true;
            return true;
        }
        return false;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        if(sd.wav_count > 0 && !sd.scan_in_progress)
        {
            const uint16_t idx = sd.menu.cursor;

            if(ctx.app->sd_delete_mode)
            {
                ctx.app->sd_delete_index = idx;
                ExtractBaseName(sd.paths[idx],
                                ctx.app->sd_delete_name,
                                sizeof(ctx.app->sd_delete_name));
                UiNav_Push(ctx.app->ui_nav, UiScreenId::SdDeleteConfirm);
                ctx.app->ui_dirty = true;
                return true;
            }

            const uint8_t layer_count = static_cast<uint8_t>(
                sizeof(ctx.app->engine_sample_path) / sizeof(ctx.app->engine_sample_path[0]));
            if(ctx.app->engine_load_target_layer < layer_count)
            {
                const uint8_t target = ctx.app->engine_load_target_layer & 1u;
                ctx.app->sd_current_slot.store(target ^ 1u, std::memory_order_release);
                std::snprintf(ctx.app->engine_sample_path[target],
                              sizeof(ctx.app->engine_sample_path[target]),
                              "%s",
                              sd.paths[idx]);
                ExtractBaseName(sd.paths[idx],
                                ctx.app->engine_sample_name[target],
                                sizeof(ctx.app->engine_sample_name[target]));
            }
            UiReq req{UiReqType::LoadWavIndex, idx, 0};
            UiReq_Push(*ctx.app, req);
            sd.load_in_progress = true;
            sd.load_progress = 0;
            SdBrowser_SetStatus(sd, "LOADING");
            if(ctx.app->engine_load_from_perform)
                UiNav_Pop(ctx.app->ui_nav);
            ctx.app->ui_dirty = true;
        }
        return true;
    }

    return false;
}

void SdBrowse_Render(UiScreenCtx& ctx)
{
    if(!ctx.app || !ctx.display)
        return;

    SdBrowserState& sd = ctx.app->sd;
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
    const int arrow_x = menu_x + 4;
    const int label_x = menu_x + 10;

    auto draw_filled_right_triangle = [&](int cx, int cy)
    {
        for(int dx = 0; dx < 4; ++dx)
        {
            const int px = cx + dx;
            const int half_h = dx;
            for(int yy = cy - half_h; yy <= cy + half_h; ++yy)
                d.DrawPixel(px, yy, true);
        }
    };

    for(uint8_t row = 0; row < sd.menu.rows; ++row)
    {
        const uint8_t idx = static_cast<uint8_t>(sd.menu.scroll + row);
        if(idx >= sd.menu.count || !sd.menu.items)
            break;

        const int row_y = menu_y + static_cast<int>(row) * layout.line_h;
        if(idx == sd.menu.cursor)
        {
            const int arrow_cy = row_y + (layout.line_h / 2) - 1;
            draw_filled_right_triangle(arrow_x, arrow_cy);
        }

        d.SetCursor(label_x, row_y);
        d.WriteString(sd.menu.items[idx].label, Font_6x8, true);
    }
}

bool SdDeleteConfirm_OnEnter(UiScreenCtx& ctx)
{
    if(!ctx.app)
        return false;

    AppState& app = *ctx.app;
    SdBrowserState& sd = app.sd;

    const uint16_t idx = app.sd_delete_index;
    if(!app.sd_delete_mode || idx >= sd.wav_count || sd.scan_in_progress)
    {
        SdBrowser_SetStatus(sd, "DEL ERR");
        UiNav_Pop(app.ui_nav);
        app.sd_delete_mode = false;
        app.ui_dirty = true;
        return true;
    }

    UiReq del{UiReqType::DeleteWavIndex, idx, 0};
    UiReq scan{UiReqType::ScanSdWavs, 0, 0};
    (void)UiReq_Push(app, del);
    (void)UiReq_Push(app, scan);

    sd.scan_in_progress = true;
    sd.scan_done = false;
    SdBrowser_SetStatus(sd, "DELETING");

    app.sd_delete_mode = false;
    UiNav_Pop(app.ui_nav);
    app.ui_dirty = true;
    return true;
}

void SdDeleteConfirm_Render(UiScreenCtx& ctx)
{
    if(!ctx.app || !ctx.display)
        return;

    AppState& app = *ctx.app;
    OledPager& d = *ctx.display;
    d.Fill(false);

    const UiLayout layout = UiLayout_Default();
    char status[16];
    BuildStatus(app, status, sizeof(status));
    UiDraw_Header(d, layout, "DELETE WAV", status);

    d.SetCursor(layout.x, layout.y_body);
    char namebuf[32];
    if(app.sd_delete_name[0] != '\0')
        std::snprintf(namebuf, sizeof(namebuf), "%s", app.sd_delete_name);
    else
        std::snprintf(namebuf, sizeof(namebuf), "(no file)");
    d.WriteString(namebuf, Font_6x8, true);

    d.SetCursor(layout.x, layout.y_body + layout.line_h * 2);
    d.WriteString("ARE YOU SURE?", Font_6x8, true);
    d.SetCursor(layout.x, layout.y_body + layout.line_h * 3);
    d.WriteString("R:YES   L:NO", Font_6x8, true);
}

enum SampleEditItem : uint8_t
{
    SE_TrimStart = 0,
    SE_TrimEnd,
    SE_LoopEnable,
    SE_LoopStart,
    SE_LoopEnd,
    SE_Normalize,
    SE_LoopFind,
    SE_SaveWav,
    SE_Count
};

static void EnsureSampleEditMenu(AppState& app, uint8_t rows)
{
    static const UiMenuItem items[] = {
        {"TRIM START", UiScreenId::COUNT, UiReqType::None},
        {"TRIM END", UiScreenId::COUNT, UiReqType::None},
        {"LOOP EN", UiScreenId::COUNT, UiReqType::None},
        {"LOOP START", UiScreenId::COUNT, UiReqType::None},
        {"LOOP END", UiScreenId::COUNT, UiReqType::None},
        {"NORMALIZE", UiScreenId::COUNT, UiReqType::None},
        {"LOOP FIND", UiScreenId::COUNT, UiReqType::None},
        {"SAVE WAV", UiScreenId::COUNT, UiReqType::None},
    };

    if(app.sample_edit_menu_inited && app.sample_edit_menu.rows == rows)
        return;
    UiListMenu_Init(app.sample_edit_menu,
                    items,
                    static_cast<uint8_t>(sizeof(items) / sizeof(items[0])),
                    rows);
    app.sample_edit_menu_inited = true;
}

static uint32_t FramesToMs(uint32_t frames)
{
    return (frames + 24u) / 48u;
}

static uint32_t MsToFrames(uint32_t ms)
{
    return ms * 48u;
}

bool SampleEdit_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.app)
        return false;

    AppState& app = *ctx.app;
    if(ctx.shift)
        return false;

    const uint8_t slot = app.sd_current_slot.load(std::memory_order_relaxed) & 1u;
    SampleEdit edit = app.sd_edit_slots[slot];
    const uint32_t frames = (slot < kSdSampleSlots && app.sd_slots[slot].length > 0)
                            ? app.sd_slots[slot].length
                            : 0;

    const UiLayout layout = UiLayout_Default();
    const uint8_t info_lines = 4;
    const uint8_t rows = (layout.rows_body > info_lines)
                             ? static_cast<uint8_t>(layout.rows_body - info_lines)
                             : 1;
    EnsureSampleEditMenu(app, rows);

    if(app.value_edit.active)
    {
        if(e.type == UiInputType::EncDelta && e.id == kUiEncExt)
        {
            if(UiValueEdit_OnEnc(app.value_edit, e.value))
                app.ui_dirty = true;
            return true;
        }
        if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
        {
            const int16_t v = app.value_edit.value_i;
            const uint32_t v_ms = (v < 0) ? 0u : static_cast<uint32_t>(v);
            const uint32_t v_frames = MsToFrames(v_ms);
            switch(app.sample_edit_menu.cursor)
            {
                case SE_TrimStart:
                    edit.start_frame = v_frames;
                    break;
                case SE_TrimEnd:
                    edit.end_frame = v_frames;
                    break;
                case SE_LoopEnable:
                    edit.loop_enable = (v != 0) ? 1 : 0;
                    break;
                case SE_LoopStart:
                    edit.loop_start = v_frames;
                    break;
                case SE_LoopEnd:
                    edit.loop_end = v_frames;
                    break;
                default:
                    break;
            }
            SampleEdit_Clamp(edit, frames);
            app.sd_edit_slots[slot] = edit;
            app.sd_edit_pending = edit;
            app.sd_edit_slot.store(slot, std::memory_order_release);
            app.sd_edit_gen.fetch_add(1, std::memory_order_acq_rel);
            app.sd_edit_ready.store(1, std::memory_order_release);
            UiValueEdit_Commit(app.value_edit);
            app.ui_dirty = true;
            return true;
        }
        return false;
    }

    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt)
    {
        if(UiListMenu_OnEnc(app.sample_edit_menu, e.value))
        {
            app.ui_dirty = true;
            return true;
        }
        return false;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        const uint8_t idx = app.sample_edit_menu.cursor;
        if(idx == SE_Normalize)
        {
            UiReq req{UiReqType::NormalizeCurrent, 0, 0};
            UiReq_Push(app, req);
            SdBrowser_SetStatus(app.sd, "NORMALIZE");
            app.ui_dirty = true;
            return true;
        }
        if(idx == SE_LoopFind)
        {
            UiReq req{UiReqType::LoopFindCurrent, 0, 0};
            UiReq_Push(app, req);
            SdBrowser_SetStatus(app.sd, "LOOP FIND");
            app.ui_dirty = true;
            return true;
        }
        if(idx == SE_SaveWav)
        {
            UiReq req{UiReqType::SaveRenderedWavCurrent, 0, 0};
            if(UiReq_Push(app, req))
            {
                SdBrowser_SetSaveStatus(app.sd, "SAVING");
                app.sd.save_progress = 0;
                app.sd.save_in_progress = true;
            }
            else
            {
                SdBrowser_SetSaveStatus(app.sd, "SAVE ERR");
            }
            app.ui_dirty = true;
            return true;
        }

        UiValueSpec spec{};
        const char* label = "";
        int16_t start_i = 0;
        switch(idx)
        {
            case SE_TrimStart:
                label = "TRIM S";
                spec = {UiValueType::Norm01, 1, 0, 5000, nullptr, 0};
                start_i = static_cast<int16_t>(FramesToMs(edit.start_frame));
                break;
            case SE_TrimEnd:
                label = "TRIM E";
                spec = {UiValueType::Norm01, 1, 0, 5000, nullptr, 0};
                start_i = static_cast<int16_t>(FramesToMs(edit.end_frame));
                break;
            case SE_LoopEnable:
                label = "LOOP";
                spec = {UiValueType::Bool, 1, 0, 1, nullptr, 0};
                start_i = edit.loop_enable ? 1 : 0;
                break;
            case SE_LoopStart:
                label = "LP S";
                spec = {UiValueType::Norm01, 1, 0, 5000, nullptr, 0};
                start_i = static_cast<int16_t>(FramesToMs(edit.loop_start));
                break;
            case SE_LoopEnd:
                label = "LP E";
                spec = {UiValueType::Norm01, 1, 0, 5000, nullptr, 0};
                start_i = static_cast<int16_t>(FramesToMs(edit.loop_end));
                break;
            default:
                return false;
        }
        UiValueEdit_Begin(app.value_edit, label, spec, start_i);
        app.ui_dirty = true;
        return true;
    }

    return false;
}

void SampleEdit_Render(UiScreenCtx& ctx)
{
    if(!ctx.app || !ctx.display)
        return;

    AppState& app = *ctx.app;
    const uint8_t slot = app.sd_current_slot.load(std::memory_order_relaxed) & 1u;
    const SampleEdit& edit = app.sd_edit_slots[slot];

    OledPager& d = *ctx.display;
    d.Fill(false);

    const UiLayout layout = UiLayout_Default();
    char status[16];
    BuildStatus(app, status, sizeof(status));
    UiDraw_Header(d, layout, "SAMPLE EDIT", status);

    const uint8_t info_lines = 4;
    const uint8_t rows = (layout.rows_body > info_lines)
                             ? static_cast<uint8_t>(layout.rows_body - info_lines)
                             : 1;
    EnsureSampleEditMenu(app, rows);

    const uint32_t start_ms = FramesToMs(edit.start_frame);
    const uint32_t end_ms = FramesToMs(edit.end_frame);
    const uint32_t loop_s_ms = FramesToMs(edit.loop_start);
    const uint32_t loop_e_ms = FramesToMs(edit.loop_end);
    uint32_t gain_pct = static_cast<uint32_t>(edit.gain * 100.0f + 0.5f);
    if(gain_pct > 999u)
        gain_pct = 999u;

    char info[32];
    d.SetCursor(layout.x, layout.y_body);
    std::snprintf(info, sizeof(info), "ST:%04lu EN:%04lu",
                  (unsigned long)start_ms,
                  (unsigned long)end_ms);
    d.WriteString(info, Font_6x8, true);

    d.SetCursor(layout.x, layout.y_body + layout.line_h);
    std::snprintf(info, sizeof(info), "LP:%c LS:%04lu",
                  edit.loop_enable ? '1' : '0',
                  (unsigned long)loop_s_ms);
    d.WriteString(info, Font_6x8, true);

    d.SetCursor(layout.x, layout.y_body + layout.line_h * 2);
    std::snprintf(info, sizeof(info), "LE:%04lu G:%03lu",
                  (unsigned long)loop_e_ms,
                  (unsigned long)gain_pct);
    d.WriteString(info, Font_6x8, true);

    d.SetCursor(layout.x, layout.y_body + layout.line_h * 3);
    if(app.sd.save_in_progress)
    {
        std::snprintf(info, sizeof(info), "SAVING %03u%%",
                      static_cast<unsigned>(app.sd.save_progress));
        d.WriteString(info, Font_6x8, true);
    }
    else if(app.sd.save_status[0] != '\0')
    {
        if(std::strncmp(app.sd.save_status, "SAVED", 5) == 0
           && app.sd.save_name[0] != '\0')
        {
            std::snprintf(info, sizeof(info), "SAVED:%s", app.sd.save_name);
        }
        else
        {
            std::snprintf(info, sizeof(info), "%s", app.sd.save_status);
        }
        d.WriteString(info, Font_6x8, true);
    }

    const uint8_t count = app.sample_edit_menu.count;
    for(uint8_t row = 0; row < app.sample_edit_menu.rows; ++row)
    {
        const uint8_t idx = static_cast<uint8_t>(app.sample_edit_menu.scroll + row);
        if(idx >= count)
            break;

        char buf[32];
        const char prefix = (idx == app.sample_edit_menu.cursor) ? '>' : ' ';
        switch(idx)
        {
            case SE_TrimStart:
                std::snprintf(buf, sizeof(buf), "%c TRIM S:%04lu",
                              prefix, (unsigned long)start_ms);
                break;
            case SE_TrimEnd:
                std::snprintf(buf, sizeof(buf), "%c TRIM E:%04lu",
                              prefix, (unsigned long)end_ms);
                break;
            case SE_LoopEnable:
                std::snprintf(buf, sizeof(buf), "%c LOOP EN:%c",
                              prefix, edit.loop_enable ? '1' : '0');
                break;
            case SE_LoopStart:
                std::snprintf(buf, sizeof(buf), "%c LOOP S:%04lu",
                              prefix, (unsigned long)loop_s_ms);
                break;
            case SE_LoopEnd:
                std::snprintf(buf, sizeof(buf), "%c LOOP E:%04lu",
                              prefix, (unsigned long)loop_e_ms);
                break;
            case SE_Normalize:
                std::snprintf(buf, sizeof(buf), "%c NORMALIZE", prefix);
                break;
            case SE_LoopFind:
                std::snprintf(buf, sizeof(buf), "%c LOOP FIND", prefix);
                break;
            case SE_SaveWav:
                std::snprintf(buf, sizeof(buf), "%c SAVE WAV", prefix);
                break;
            default:
                std::snprintf(buf, sizeof(buf), "%c -", prefix);
                break;
        }

        d.SetCursor(layout.x,
                    layout.y_body + static_cast<int>(row + info_lines) * layout.line_h);
        d.WriteString(buf, Font_6x8, true);
    }

    const bool busy = app.sd.save_in_progress
                      || (app.worker.ui_req_busy
                          && (app.worker.ui_req_active == UiReqType::NormalizeCurrent
                              || app.worker.ui_req_active == UiReqType::LoopFindCurrent
                              || app.worker.ui_req_active == UiReqType::SaveRenderedWavCurrent));
    const char* hint = busy ? "BUSY"
                            : (app.value_edit.active ? "EXT:CHG EXT:OK P2:CANC"
                                                     : "A=SEL  B=BACK");
    UiDraw_Footer(d, layout, hint);
}
