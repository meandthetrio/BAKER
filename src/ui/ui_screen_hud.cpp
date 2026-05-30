#include "ui_screens_internal.h"

#include <cstdio>

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
#include "ui_list_menu.h"

static const UiMenuItem kHudMenuItems[] = {
    {"SD BROWSE", UiScreenId::SdBrowse, UiReqType::None},
    {"SAMPLE EDIT", UiScreenId::SampleEdit, UiReqType::None},
    {"FX", UiScreenId::Fx, UiReqType::None},
    {"MOD", UiScreenId::Mod, UiReqType::None},
    {"MACRO", UiScreenId::Macro, UiReqType::None},
    {"REBUILD", UiScreenId::COUNT, UiReqType::RebuildCache},
    {"LOAD", UiScreenId::COUNT, UiReqType::LoadSample},
    {"SAVE", UiScreenId::COUNT, UiReqType::SavePreset},
};

static void EnsureHudMenu(AppUiState& ui)
{
    if(ui.hud_menu_inited)
        return;
    UiListMenu_Init(ui.hud_menu,
                    kHudMenuItems,
                    static_cast<uint8_t>(sizeof(kHudMenuItems) / sizeof(kHudMenuItems[0])),
                    3);
    ui.hud_menu_inited = true;
}

bool Hud_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.ui)
        return false;
    EnsureHudMenu(*ctx.ui);
    if(ctx.shift)
        return false;

    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod)
    {
        if(UiListMenu_OnEnc(ctx.ui->hud_menu, e.value))
        {
            ctx.ui->ui_dirty = true;
            return true;
        }
        return false;
    }
    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        const UiMenuItem& item = ctx.ui->hud_menu.items[ctx.ui->hud_menu.cursor];
        if(item.req != UiReqType::None)
        {
            UiReq req{item.req, 0, 0};
            UiReq_Push(*ctx.ui, *ctx.worker, req);
            ctx.ui->ui_dirty = true;
            return true;
        }
        if(item.screen == UiScreenId::SdBrowse)
        {
            ctx.ui->sd_delete_mode = false;
            ctx.ui->sample_rename_active = false;
        }
        if(item.screen != UiScreenId::COUNT && UiNav_Push(ctx.ui->ui_nav, item.screen))
            ctx.ui->ui_dirty = true;
        return true;
    }

    return false;
}

void Hud_Render(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.display)
        return;

    const AppUiState& ui = *ctx.ui;
    AppDiagnosticsState& diag = *ctx.diag;
    AppSharedState& shared = *ctx.shared;
    EnsureHudMenu(*ctx.ui);

    const uint32_t peak_cycles   = diag.audio_cycles_peak.load(std::memory_order_relaxed);
    const uint32_t budget_cycles = diag.audio_budget_cycles.load(std::memory_order_relaxed);
    uint32_t cpu_pct = 0;
    if(budget_cycles > 0)
        cpu_pct = (peak_cycles * 100u + (budget_cycles / 2u)) / budget_cycles;
    if(cpu_pct > 999u)
        cpu_pct = 999u;
    const uint32_t late_cnt = diag.audio_late_count.load(std::memory_order_relaxed);

    const uint32_t ovf_mod = ui.ui_in_ovf % 1000;
    uint32_t hi = ui.ui_in_hi;
    if(hi > 99u)
        hi = 99u;
    const char* sd_ok = ui.sd.sd_ok ? "OK" : "ER";
    uint32_t wavs = ui.sd.wav_count;
    if(wavs > 99u)
        wavs = 99u;
    const uint32_t ld = ui.sd.load_in_progress ? ui.sd.load_progress : 0;

    OledPager& d = *ctx.display;
    d.Fill(false);

    const UiLayout layout = UiLayout_Default();
    char status[16];
    BuildStatus(shared, status, sizeof(status));
    UiDraw_Header(d, layout, "HUD", status);

    char buf[32];
    d.SetCursor(layout.x, layout.y_body);
    std::snprintf(buf,
                  sizeof(buf),
                  "U:%02lu C:%04lu CPU:%03lu",
                  (unsigned long)ui.ui_hz,
                  (unsigned long)ui.ctrl_hz,
                  (unsigned long)cpu_pct);
    d.WriteString(buf, Font_6x8, true);

    d.SetCursor(layout.x, layout.y_body + layout.line_h);
    std::snprintf(buf, sizeof(buf), "LATE:%lu UIQO:%03lu H:%02lu",
                  (unsigned long)late_cnt,
                  (unsigned long)ovf_mod,
                  (unsigned long)hi);
    d.WriteString(buf, Font_6x8, true);

    d.SetCursor(layout.x, layout.y_body + layout.line_h * 2);
    std::snprintf(buf, sizeof(buf), "SD:%s W:%02lu L:%03lu",
                  sd_ok,
                  (unsigned long)wavs,
                  (unsigned long)ld);
    d.WriteString(buf, Font_6x8, true);

    UiListMenu_Render(ctx.ui->hud_menu,
                      d,
                      layout.x,
                      layout.y_body + layout.line_h * 3,
                      layout.line_h);

    UiDraw_Footer(d, layout, "EXT:SEL EXT:ENT P2:BACK");
}
