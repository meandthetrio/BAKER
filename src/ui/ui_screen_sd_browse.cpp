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

#include <cstdio>
#include <cstring>

using namespace daisy;

static void EnsureScanRequested(UiScreenCtx& ctx)
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

static void BuildRenameDraftFromName(const char* name, char* out, size_t out_n)
{
    if(!out || out_n == 0u)
        return;
    out[0] = '\0';
    if(!name || name[0] == '\0')
        return;

    std::snprintf(out, out_n, "%s", name);
    const size_t len = std::strlen(out);
    if(len > 4u)
    {
        char* ext = out + (len - 4u);
        const bool wav_ext = (ext[0] == '.')
                             && ((ext[1] == 'w') || (ext[1] == 'W'))
                             && ((ext[2] == 'a') || (ext[2] == 'A'))
                             && ((ext[3] == 'v') || (ext[3] == 'V'));
        if(wav_ext)
            ext[0] = '\0';
    }
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

            if(ctx.ui->sd_delete_mode)
            {
                ctx.ui->sd_delete_index = idx;
                ExtractBaseName(sd.paths[idx],
                                ctx.ui->sd_delete_name,
                                sizeof(ctx.ui->sd_delete_name));
                UiNav_Push(ctx.ui->ui_nav, UiScreenId::SdDeleteConfirm);
                ctx.ui->ui_dirty = true;
                return true;
            }
            if(ctx.ui->sd_rename_mode)
            {
                ctx.ui->sample_rename_active = true;
                ctx.ui->sample_rename_index = idx;
                BuildRenameDraftFromName(sd.names[idx],
                                         ctx.ui->project_rename_draft,
                                         sizeof(ctx.ui->project_rename_draft));
                ctx.ui->project_rename_length =
                    static_cast<uint8_t>(std::strlen(ctx.ui->project_rename_draft));
                if(UiNav_Push(ctx.ui->ui_nav, UiScreenId::RenameProject))
                    ctx.ui->ui_dirty = true;
                return true;
            }

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
    const int label_x = menu_x + 10;

    for(uint8_t row = 0; row < sd.menu.rows; ++row)
    {
        const uint8_t idx = static_cast<uint8_t>(sd.menu.scroll + row);
        if(idx >= sd.menu.count || !sd.menu.items)
            break;

        const int row_y = menu_y + static_cast<int>(row) * layout.line_h;
        if(idx == sd.menu.cursor)
        {
            DrawRencFocusString6x8(d, sd.menu.items[idx].label, label_x, row_y);
            continue;
        }

        d.SetCursor(label_x, row_y);
        d.WriteString(sd.menu.items[idx].label, Font_6x8, true);
    }
}
