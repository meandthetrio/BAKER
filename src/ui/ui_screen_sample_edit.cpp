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

    if(app.ui.sample_edit_menu_inited && app.ui.sample_edit_menu.rows == rows)
        return;
    UiListMenu_Init(app.ui.sample_edit_menu,
                    items,
                    static_cast<uint8_t>(sizeof(items) / sizeof(items[0])),
                    rows);
    app.ui.sample_edit_menu_inited = true;
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

    const uint8_t slot = app.shared.sd_current_slot.load(std::memory_order_relaxed) & 1u;
    SampleEdit edit = app.shared.sd_edit_slots[slot];
    const uint32_t frames = (slot < kSdSampleSlots && app.shared.sd_slots[slot].length > 0)
                            ? app.shared.sd_slots[slot].length
                            : 0;

    const UiLayout layout = UiLayout_Default();
    const uint8_t info_lines = 4;
    const uint8_t rows = (layout.rows_body > info_lines)
                             ? static_cast<uint8_t>(layout.rows_body - info_lines)
                             : 1;
    EnsureSampleEditMenu(app, rows);

    if(app.ui.value_edit.active)
    {
        if(e.type == UiInputType::EncDelta && e.id == kUiEncExt)
        {
            if(UiValueEdit_OnEnc(app.ui.value_edit, e.value))
                app.ui.ui_dirty = true;
            return true;
        }
        if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
        {
            const int16_t v = app.ui.value_edit.value_i;
            const uint32_t v_ms = (v < 0) ? 0u : static_cast<uint32_t>(v);
            const uint32_t v_frames = MsToFrames(v_ms);
            switch(app.ui.sample_edit_menu.cursor)
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
            app.shared.sd_edit_slots[slot] = edit;
            app.shared.sd_edit_pending = edit;
            app.shared.sd_edit_slot.store(slot, std::memory_order_release);
            app.shared.sd_edit_gen.fetch_add(1, std::memory_order_acq_rel);
            app.shared.sd_edit_ready.store(1, std::memory_order_release);
            UiValueEdit_Commit(app.ui.value_edit);
            app.ui.ui_dirty = true;
            return true;
        }
        return false;
    }

    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt)
    {
        if(UiListMenu_OnEnc(app.ui.sample_edit_menu, e.value))
        {
            app.ui.ui_dirty = true;
            return true;
        }
        return false;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        const uint8_t idx = app.ui.sample_edit_menu.cursor;
        if(idx == SE_Normalize)
        {
            UiReq req{UiReqType::NormalizeCurrent, 0, 0};
            UiReq_Push(app, req);
            SdBrowser_SetStatus(app.ui.sd, "NORMALIZE");
            app.ui.ui_dirty = true;
            return true;
        }
        if(idx == SE_LoopFind)
        {
            UiReq req{UiReqType::LoopFindCurrent, 0, 0};
            UiReq_Push(app, req);
            SdBrowser_SetStatus(app.ui.sd, "LOOP FIND");
            app.ui.ui_dirty = true;
            return true;
        }
        if(idx == SE_SaveWav)
        {
            UiReq req{UiReqType::SaveRenderedWavCurrent, 0, 0};
            if(UiReq_Push(app, req))
            {
                SdBrowser_SetSaveStatus(app.ui.sd, "SAVING");
                app.ui.sd.save_progress = 0;
                app.ui.sd.save_in_progress = true;
            }
            else
            {
                SdBrowser_SetSaveStatus(app.ui.sd, "SAVE ERR");
            }
            app.ui.ui_dirty = true;
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
        UiValueEdit_Begin(app.ui.value_edit, label, spec, start_i);
        app.ui.ui_dirty = true;
        return true;
    }

    return false;
}

void SampleEdit_Render(UiScreenCtx& ctx)
{
    if(!ctx.app || !ctx.display)
        return;

    AppState& app = *ctx.app;
    const uint8_t slot = app.shared.sd_current_slot.load(std::memory_order_relaxed) & 1u;
    const SampleEdit& edit = app.shared.sd_edit_slots[slot];

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
    if(app.ui.sd.save_in_progress)
    {
        std::snprintf(info, sizeof(info), "SAVING %03u%%",
                      static_cast<unsigned>(app.ui.sd.save_progress));
        d.WriteString(info, Font_6x8, true);
    }
    else if(app.ui.sd.save_status[0] != '\0')
    {
        if(std::strncmp(app.ui.sd.save_status, "SAVED", 5) == 0
           && app.ui.sd.save_name[0] != '\0')
        {
            std::snprintf(info, sizeof(info), "SAVED:%s", app.ui.sd.save_name);
        }
        else
        {
            std::snprintf(info, sizeof(info), "%s", app.ui.sd.save_status);
        }
        d.WriteString(info, Font_6x8, true);
    }

    const uint8_t count = app.ui.sample_edit_menu.count;
    for(uint8_t row = 0; row < app.ui.sample_edit_menu.rows; ++row)
    {
        const uint8_t idx = static_cast<uint8_t>(app.ui.sample_edit_menu.scroll + row);
        if(idx >= count)
            break;

        char buf[32];
        const char prefix = (idx == app.ui.sample_edit_menu.cursor) ? '>' : ' ';
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

    const bool busy = app.ui.sd.save_in_progress
                      || (app.worker.ui_req_busy
                          && (app.worker.ui_req_active == UiReqType::NormalizeCurrent
                              || app.worker.ui_req_active == UiReqType::LoopFindCurrent
                              || app.worker.ui_req_active == UiReqType::SaveRenderedWavCurrent));
    const char* hint = busy ? "BUSY"
                            : (app.ui.value_edit.active ? "EXT:CHG EXT:OK P2:CANC"
                                                     : "A=SEL  B=BACK");
    UiDraw_Footer(d, layout, hint);
}
