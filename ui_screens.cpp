#include "ui_screens.h"

#include "app_state.h"
#include "params.h"
#include "ui_input.h"
#include "ui_list_menu.h"
#include "ui_value_edit.h"
#include "ui_layout.h"
#include "oled_pager.h"
#include "mod_matrix.h"
#include "macros.h"
#include "ui_requests.h"
#include "sd_browser_state.h"
#include "sample_edit.h"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace daisy;

static float Clamp01(float x)
{
    if(x < 0.0f) return 0.0f;
    if(x > 1.0f) return 1.0f;
    return x;
}

static float ClampSigned(float x)
{
    if(x < -1.0f) return -1.0f;
    if(x > 1.0f) return 1.0f;
    return x;
}

static int ToPct01(float x)
{
    if(x < 0.0f) x = 0.0f;
    if(x > 1.0f) x = 1.0f;
    return static_cast<int>(x * 100.0f + 0.5f);
}

static char DstChar(uint8_t dst)
{
    return (dst == static_cast<uint8_t>(ModDest::FilterCutoff)) ? 'C' : 'P';
}

UiScreenId UiNav_Active(const UiNav& nav)
{
    return nav.stack[nav.top];
}

bool UiNav_Push(UiNav& nav, UiScreenId next)
{
    if(nav.top + 1 >= kUiStackMax)
        return false;
    nav.top++;
    nav.stack[nav.top] = next;
    return true;
}

bool UiNav_Pop(UiNav& nav)
{
    if(nav.top == 0)
        return false;
    nav.top--;
    return true;
}

static const UiMenuItem kHudMenuItems[] = {
    {"SD BROWSE", UiScreenId::SdBrowse, UiReqType::None},
    {"SAMPLE EDIT", UiScreenId::SampleEdit, UiReqType::None},
    {"SAVE PROJECT", UiScreenId::COUNT, UiReqType::SaveProject},
    {"LOAD PROJECT", UiScreenId::COUNT, UiReqType::LoadProject},
    {"FX", UiScreenId::Fx, UiReqType::None},
    {"MOD", UiScreenId::Mod, UiReqType::None},
    {"MACRO", UiScreenId::Macro, UiReqType::None},
    {"REBUILD", UiScreenId::COUNT, UiReqType::RebuildCache},
    {"LOAD", UiScreenId::COUNT, UiReqType::LoadSample},
    {"SAVE", UiScreenId::COUNT, UiReqType::SavePreset},
};

static void EnsureHudMenu(AppState& app)
{
    if(app.hud_menu_inited)
        return;
    UiListMenu_Init(app.hud_menu,
                    kHudMenuItems,
                    static_cast<uint8_t>(sizeof(kHudMenuItems) / sizeof(kHudMenuItems[0])),
                    3);
    app.hud_menu_inited = true;
}

static bool Hud_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.app)
        return false;
    EnsureHudMenu(*ctx.app);
    if(ctx.shift)
        return false;

    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt)
    {
        if(UiListMenu_OnEnc(ctx.app->hud_menu, e.value))
        {
            ctx.app->ui_dirty = true;
            return true;
        }
        return false;
    }
    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        const UiMenuItem& item = ctx.app->hud_menu.items[ctx.app->hud_menu.cursor];
        if(item.req != UiReqType::None)
        {
            UiReq req{item.req, 0, 0};
            UiReq_Push(*ctx.app, req);
            ctx.app->ui_dirty = true;
            return true;
        }
        if(item.screen != UiScreenId::COUNT && UiNav_Push(ctx.app->ui_nav, item.screen))
            ctx.app->ui_dirty = true;
        return true;
    }

    return false;
}

static void Hud_Render(UiScreenCtx& ctx)
{
    if(!ctx.app || !ctx.display)
        return;

    const AppState& app = *ctx.app;
    EnsureHudMenu(*ctx.app);

    const uint32_t peak_cycles   = app.audio_cycles_peak.load(std::memory_order_relaxed);
    const uint32_t budget_cycles = app.audio_budget_cycles.load(std::memory_order_relaxed);
    uint32_t cpu_pct = 0;
    if(budget_cycles > 0)
        cpu_pct = (peak_cycles * 100u + (budget_cycles / 2u)) / budget_cycles;
    if(cpu_pct > 999u)
        cpu_pct = 999u;
    const uint32_t late_cnt = app.audio_late_count.load(std::memory_order_relaxed);

    const uint32_t ovf_mod = app.ui_in_ovf % 1000;
    uint32_t hi = app.ui_in_hi;
    if(hi > 99u)
        hi = 99u;
    const char* sd_ok = app.sd.sd_ok ? "OK" : "ER";
    uint32_t wavs = app.sd.wav_count;
    if(wavs > 99u)
        wavs = 99u;
    const uint32_t ld = app.sd.load_in_progress ? app.sd.load_progress : 0;

    OledPager& d = *ctx.display;
    d.Fill(false);

    const UiLayout layout = UiLayout_Default();
    char status[16];
    BuildStatus(app, status, sizeof(status));
    UiDraw_Header(d, layout, "HUD", status);

    char buf[32];
    d.SetCursor(layout.x, layout.y_body);
    std::snprintf(buf,
                  sizeof(buf),
                  "U:%02lu C:%04lu CPU:%03lu",
                  (unsigned long)app.ui_hz,
                  (unsigned long)app.ctrl_hz,
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

    UiListMenu_Render(ctx.app->hud_menu,
                      d,
                      layout.x,
                      layout.y_body + layout.line_h * 3,
                      layout.line_h);

    const char* footer = (app.project_status[0] != '\0')
                             ? app.project_status
                             : "EXT:SEL EXT:ENT P2:BACK";
    UiDraw_Footer(d, layout, footer);
}

static bool Fx_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.app || !ctx.params)
        return false;

    static constexpr uint8_t kFxFieldCount = 4;
    static constexpr float kLpfMinHz = 80.0f;
    static constexpr float kLpfMaxHz = 12000.0f;

    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt && !ctx.app->value_edit.active)
    {
        int next = static_cast<int>(ctx.app->fx_field_cursor) + e.value;
        while(next < 0) next += kFxFieldCount;
        while(next >= kFxFieldCount) next -= kFxFieldCount;
        ctx.app->fx_field_cursor = static_cast<uint8_t>(next);
        ctx.app->ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        if(!ctx.app->value_edit.active)
        {
            const auto& t = ctx.params->TargetsForUI();
            int16_t start_i = 0;
            UiValueSpec spec{};
            const char* label = "";
            switch(ctx.app->fx_field_cursor)
            {
                case 0: // Delay On
                    label = "DLY";
                    spec = {UiValueType::Bool, 1, 0, 1, nullptr, 0};
                    start_i = t.delay_on ? 1 : 0;
                    break;
                case 1: // Delay Mix
                    label = "MIX";
                    spec = {UiValueType::Norm01, 1, 0, 100, nullptr, 0};
                    start_i = (int16_t)(Clamp01(t.delay_mix) * 100.0f + 0.5f);
                    break;
                case 2: // Sat On
                    label = "SAT";
                    spec = {UiValueType::Bool, 1, 0, 1, nullptr, 0};
                    start_i = t.sat_on ? 1 : 0;
                    break;
                case 3: // LPF
                default:
                {
                    label = "LPF";
                    spec = {UiValueType::Norm01, 1, 0, 100, nullptr, 0};
                    float hz = t.lpf_cutoff_hz;
                    if(hz < kLpfMinHz) hz = kLpfMinHz;
                    if(hz > kLpfMaxHz) hz = kLpfMaxHz;
                    const float ratio = kLpfMaxHz / kLpfMinHz;
                    float norm = std::log(hz / kLpfMinHz) / std::log(ratio);
                    start_i = (int16_t)(Clamp01(norm) * 100.0f + 0.5f);
                    break;
                }
            }
            UiValueEdit_Begin(ctx.app->value_edit, label, spec, start_i);
            ctx.app->ui_dirty = true;
        }
        else
        {
            PerformParamsTargets& t = ctx.params->EditTargets();
            const int16_t v = ctx.app->value_edit.value_i;
            switch(ctx.app->fx_field_cursor)
            {
                case 0:
                    t.delay_on = (v != 0);
                    break;
                case 1:
                    t.delay_mix = Clamp01((float)v / 100.0f);
                    break;
                case 2:
                    t.sat_on = (v != 0);
                    break;
                case 3:
                default:
                {
                    const float ratio = kLpfMaxHz / kLpfMinHz;
                    const float norm = Clamp01((float)v / 100.0f);
                    t.lpf_cutoff_hz = kLpfMinHz * std::pow(ratio, norm);
                    break;
                }
            }
            ctx.params->PublishTargets();
            UiValueEdit_Commit(ctx.app->value_edit);
            ctx.app->ui_dirty = true;
        }
        return true;
    }

    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt && ctx.app->value_edit.active)
    {
        if(UiValueEdit_OnEnc(ctx.app->value_edit, e.value))
            ctx.app->ui_dirty = true;
        return true;
    }

    if(ctx.app->value_edit.active)
        return true;

    return false;
}

static void Fx_Render(UiScreenCtx& ctx)
{
    if(!ctx.app || !ctx.params || !ctx.display)
        return;

    const auto& t = ctx.params->TargetsForUI();

    OledPager& d = *ctx.display;
    d.Fill(false);

    const UiLayout layout = UiLayout_Default();
    char status[16];
    BuildStatus(*ctx.app, status, sizeof(status));
    UiDraw_Header(d, layout, "FX", status);

    char buf[32];
    const uint32_t lpf_hz = static_cast<uint32_t>(t.lpf_cutoff_hz + 0.5f);
    char lpf_buf[12];
    if(lpf_hz >= 1000)
        std::snprintf(lpf_buf, sizeof(lpf_buf), "%2luk", (unsigned long)((lpf_hz + 500) / 1000));
    else
        std::snprintf(lpf_buf, sizeof(lpf_buf), "%3lu", (unsigned long)lpf_hz);

    const uint8_t cursor = ctx.app->fx_field_cursor;
    d.SetCursor(layout.x, layout.y_body);
    std::snprintf(buf, sizeof(buf), "%c DLY:%c", (cursor == 0) ? '>' : ' ',
                  t.delay_on ? '1' : '0');
    d.WriteString(buf, Font_6x8, true);

    d.SetCursor(layout.x, layout.y_body + layout.line_h);
    std::snprintf(buf, sizeof(buf), "%c MIX:%03d", (cursor == 1) ? '>' : ' ',
                  ToPct01(t.delay_mix));
    d.WriteString(buf, Font_6x8, true);

    d.SetCursor(layout.x, layout.y_body + layout.line_h * 2);
    std::snprintf(buf, sizeof(buf), "%c SAT:%c", (cursor == 2) ? '>' : ' ',
                  t.sat_on ? '1' : '0');
    d.WriteString(buf, Font_6x8, true);

    d.SetCursor(layout.x, layout.y_body + layout.line_h * 3);
    std::snprintf(buf, sizeof(buf), "%c LPF:%s", (cursor == 3) ? '>' : ' ',
                  lpf_buf);
    d.WriteString(buf, Font_6x8, true);

    UiValueEdit_Render(ctx.app->value_edit, d, layout.x, layout.y_body + layout.line_h * 4);

    const char* hint = ctx.app->value_edit.active ? "EXT:CHG EXT:OK P2:CANC"
                                                   : "EXT:MOVE EXT:EDIT P2:BACK";
    UiDraw_Footer(d, layout, hint);
}

static bool Mod_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.app)
        return false;

    static constexpr uint8_t kModFieldCount = 4;
    static const char* kRouteLabels[kMaxModRoutes] = {"R0", "R1", "R2", "R3"};
    static const char* kDstLabels[2] = {"CUT", "PIT"};

    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt && !ctx.app->value_edit.active)
    {
        int next = static_cast<int>(ctx.app->mod_field_cursor) + e.value;
        while(next < 0) next += kModFieldCount;
        while(next >= kModFieldCount) next -= kModFieldCount;
        ctx.app->mod_field_cursor = static_cast<uint8_t>(next);
        ctx.app->ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        if(!ctx.app->value_edit.active)
        {
            const uint8_t r_idx = ctx.app->mod_route_selected % kMaxModRoutes;
            const ModRoute& r = ctx.app->mod_routes_ui[r_idx];
            int16_t start_i = 0;
            UiValueSpec spec{};
            const char* label = "";
            switch(ctx.app->mod_field_cursor)
            {
                case 0: // Route Select
                    label = "ROUTE";
                    spec = {UiValueType::Enum, 1, 0, (int16_t)(kMaxModRoutes - 1),
                            kRouteLabels, (uint8_t)kMaxModRoutes};
                    start_i = (int16_t)r_idx;
                    break;
                case 1: // Enable
                    label = "EN";
                    spec = {UiValueType::Bool, 1, 0, 1, nullptr, 0};
                    start_i = r.enabled ? 1 : 0;
                    break;
                case 2: // Amount
                    label = "AMT";
                    spec = {UiValueType::Bipolar1, 1, -100, 100, nullptr, 0};
                    start_i = (int16_t)(ClampSigned(r.amount) * 100.0f);
                    break;
                case 3: // Destination
                default:
                    label = "DST";
                    spec = {UiValueType::Enum, 1, 0, 1, kDstLabels, 2};
                    start_i = (int16_t)(r.dst ? 1 : 0);
                    break;
            }
            UiValueEdit_Begin(ctx.app->value_edit, label, spec, start_i);
            ctx.app->ui_dirty = true;
        }
        else
        {
            const int16_t v = ctx.app->value_edit.value_i;
            uint8_t r_idx = ctx.app->mod_route_selected % kMaxModRoutes;
            ModRoute& r = ctx.app->mod_routes_ui[r_idx];
            bool publish = false;

            switch(ctx.app->mod_field_cursor)
            {
                case 0:
                    ctx.app->mod_route_selected = (uint8_t)v % kMaxModRoutes;
                    break;
                case 1:
                    r.enabled = (v != 0) ? 1 : 0;
                    publish = true;
                    break;
                case 2:
                    r.amount = ClampSigned((float)v / 100.0f);
                    publish = true;
                    break;
                case 3:
                default:
                    r.dst = (v != 0) ? static_cast<uint8_t>(ModDest::Pitch)
                                     : static_cast<uint8_t>(ModDest::FilterCutoff);
                    publish = true;
                    break;
            }

            if(publish)
                ModMatrix_Publish(ctx.app->mod_matrix, ctx.app->mod_routes_ui);
            UiValueEdit_Commit(ctx.app->value_edit);
            ctx.app->ui_dirty = true;
        }
        return true;
    }

    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt && ctx.app->value_edit.active)
    {
        if(UiValueEdit_OnEnc(ctx.app->value_edit, e.value))
            ctx.app->ui_dirty = true;
        return true;
    }

    if(ctx.app->value_edit.active)
        return true;

    return false;
}

static void Mod_Render(UiScreenCtx& ctx)
{
    if(!ctx.app || !ctx.display)
        return;

    const uint8_t r_idx = ctx.app->mod_route_selected % kMaxModRoutes;
    const ModRoute& r = ctx.app->mod_routes_ui[r_idx];
    int amt = (int)(r.amount * 100.0f);
    if(amt > 99) amt = 99;
    if(amt < -99) amt = -99;

    OledPager& d = *ctx.display;
    d.Fill(false);

    const UiLayout layout = UiLayout_Default();
    char status[16];
    BuildStatus(*ctx.app, status, sizeof(status));
    UiDraw_Header(d, layout, "MOD", status);

    char buf[32];
    const uint8_t cursor = ctx.app->mod_field_cursor;
    d.SetCursor(layout.x, layout.y_body);
    std::snprintf(buf, sizeof(buf), "%c R:%u",
                  (cursor == 0) ? '>' : ' ',
                  (unsigned)r_idx);
    d.WriteString(buf, Font_6x8, true);

    d.SetCursor(layout.x, layout.y_body + layout.line_h);
    std::snprintf(buf, sizeof(buf), "%c EN:%c",
                  (cursor == 1) ? '>' : ' ',
                  r.enabled ? '1' : '0');
    d.WriteString(buf, Font_6x8, true);

    d.SetCursor(layout.x, layout.y_body + layout.line_h * 2);
    std::snprintf(buf, sizeof(buf), "%c AMT:%+03d",
                  (cursor == 2) ? '>' : ' ',
                  amt);
    d.WriteString(buf, Font_6x8, true);

    d.SetCursor(layout.x, layout.y_body + layout.line_h * 3);
    std::snprintf(buf, sizeof(buf), "%c DST:%c",
                  (cursor == 3) ? '>' : ' ',
                  DstChar(r.dst));
    d.WriteString(buf, Font_6x8, true);

    UiValueEdit_Render(ctx.app->value_edit, d, layout.x, layout.y_body + layout.line_h * 4);

    const char* hint = ctx.app->value_edit.active ? "EXT:CHG EXT:OK P2:CANC"
                                                   : "EXT:MOVE EXT:EDIT P2:BACK";
    UiDraw_Footer(d, layout, hint);
}

static bool Macro_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.app)
        return false;

    bool changed = false;
    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod)
    {
        const uint8_t sel = ctx.app->macro_ui.selected % kNumMacros;
        float v = ctx.app->macro_ui.value[sel];
        v = Clamp01(v + (float)e.value * 0.02f);
        ctx.app->macro_ui.value[sel] = v;
        Macros_Publish(*ctx.app, ctx.app->macro_ui);
        changed = true;
    }

    if(changed)
    {
        ctx.app->ui_dirty = true;
        return true;
    }

    return false;
}

static void Macro_Render(UiScreenCtx& ctx)
{
    if(!ctx.app || !ctx.display)
        return;

    const uint8_t sel = ctx.app->macro_ui.selected % kNumMacros;
    uint32_t mac_val = (uint32_t)(ctx.app->macro_ui.value[sel] * 100.0f + 0.5f);
    if(mac_val > 100)
        mac_val = 100;

    const char sel_char = (sel == 0) ? 'A' : 'B';

    OledPager& d = *ctx.display;
    d.Fill(false);

    const UiLayout layout = UiLayout_Default();
    char status[16];
    BuildStatus(*ctx.app, status, sizeof(status));
    UiDraw_Header(d, layout, "MAC", status);

    char buf[32];
    d.SetCursor(layout.x, layout.y_body);
    std::snprintf(buf, sizeof(buf), "MAC %c V:%03lu",
                  sel_char,
                  (unsigned long)mac_val);
    d.WriteString(buf, Font_6x8, true);

    UiDraw_Footer(d, layout, "POD:VAL P2:BACK");
}

static void SdBrowse_OnEnter(UiScreenCtx& ctx)
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

static bool SdBrowse_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.app)
        return false;

    SdBrowserState& sd = ctx.app->sd;
    if(ctx.shift)
        return false;

    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt)
    {
        if(UiListMenu_OnEnc(sd.menu, e.value))
        {
            ctx.app->ui_dirty = true;
            return true;
        }
        return false;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPod1)
    {
        if(sd.wav_count > 0 && !sd.scan_in_progress)
        {
            const uint16_t idx = sd.menu.cursor;
            UiReq req{UiReqType::LoadWavIndex, idx, 0};
            UiReq_Push(*ctx.app, req);
            sd.load_in_progress = true;
            sd.load_progress = 0;
            SdBrowser_SetStatus(sd, "LOADING");
            ctx.app->ui_dirty = true;
        }
        return true;
    }

    return false;
}

static void SdBrowse_Render(UiScreenCtx& ctx)
{
    if(!ctx.app || !ctx.display)
        return;

    SdBrowserState& sd = ctx.app->sd;
    const UiLayout layout = UiLayout_Default();
    const bool show_status = (sd.status[0] != '\0');
    uint8_t lines_used = 1 + (show_status ? 1 : 0);
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

    char status[16];
    BuildStatus(*ctx.app, status, sizeof(status));
    UiDraw_Header(d, layout, "SD BROWSE", status);

    const char* sd_ok = sd.sd_ok ? "OK" : "ER";
    uint32_t wavs = sd.wav_count;
    if(wavs > 99u)
        wavs = 99u;
    const uint32_t ld = sd.load_in_progress ? sd.load_progress : 0;
    uint32_t gen = ctx.app->sd_published_gen.load(std::memory_order_relaxed);
    if(gen > 99u)
        gen %= 100u;
    uint8_t cur_slot = ctx.app->sd_current_slot.load(std::memory_order_relaxed);

    char buf[32];
    d.SetCursor(layout.x, layout.y_body);
    if(sd.scan_in_progress)
        std::snprintf(buf, sizeof(buf), "SD:%s SCAN...", sd_ok);
    else
        std::snprintf(buf, sizeof(buf), "SD:%s W:%02lu L:%03lu",
                      sd_ok,
                      (unsigned long)wavs,
                      (unsigned long)ld);
    d.WriteString(buf, Font_6x8, true);

    if(show_status && lines_used > 1)
    {
        d.SetCursor(layout.x, layout.y_body + layout.line_h);
        std::snprintf(buf, sizeof(buf), "MSG:%s", sd.status);
        d.WriteString(buf, Font_6x8, true);
    }
    else if(lines_used > 1)
    {
        d.SetCursor(layout.x, layout.y_body + layout.line_h);
        std::snprintf(buf, sizeof(buf), "GEN:%02lu CUR:%u",
                      (unsigned long)gen,
                      (unsigned)cur_slot);
        d.WriteString(buf, Font_6x8, true);
    }

    UiListMenu_Render(sd.menu,
                      d,
                      layout.x,
                      layout.y_body + layout.line_h * lines_used,
                      layout.line_h);

    UiDraw_Footer(d, layout, "A=LOAD  B=BACK");
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

static bool SampleEdit_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
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

    if(e.type == UiInputType::BtnDown
       && (e.id == kUiBtnExtEnc || e.id == kUiBtnPod1))
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

static void SampleEdit_Render(UiScreenCtx& ctx)
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
                      || (app.ui_req_busy
                          && (app.ui_req_active == UiReqType::NormalizeCurrent
                              || app.ui_req_active == UiReqType::LoopFindCurrent
                              || app.ui_req_active == UiReqType::SaveRenderedWavCurrent));
    const char* hint = busy ? "BUSY"
                            : (app.value_edit.active ? "EXT:CHG EXT:OK P2:CANC"
                                                     : "A=SEL  B=BACK");
    UiDraw_Footer(d, layout, hint);
}

const UiScreen& GetScreen(UiScreenId id)
{
    static const UiScreen hud{UiScreenId::Hud, nullptr, nullptr, Hud_OnEvent, Hud_Render};
    static const UiScreen fx{UiScreenId::Fx, nullptr, nullptr, Fx_OnEvent, Fx_Render};
    static const UiScreen mod{UiScreenId::Mod, nullptr, nullptr, Mod_OnEvent, Mod_Render};
    static const UiScreen macro{UiScreenId::Macro, nullptr, nullptr, Macro_OnEvent, Macro_Render};
    static const UiScreen sd{UiScreenId::SdBrowse, SdBrowse_OnEnter, nullptr, SdBrowse_OnEvent, SdBrowse_Render};
    static const UiScreen se{UiScreenId::SampleEdit, nullptr, nullptr, SampleEdit_OnEvent, SampleEdit_Render};

    switch(id)
    {
        case UiScreenId::Hud:
            return hud;
        case UiScreenId::Fx:
            return fx;
        case UiScreenId::Mod:
            return mod;
        case UiScreenId::Macro:
            return macro;
        case UiScreenId::SdBrowse:
            return sd;
        case UiScreenId::SampleEdit:
            return se;
        default:
            return hud;
    }
}

void UiRouter_DispatchEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.app)
        return;
    const UiScreen& s = GetScreen(UiNav_Active(ctx.app->ui_nav));
    if(s.OnEvent)
        s.OnEvent(ctx, e);
}

void UiRouter_Render(UiScreenCtx& ctx)
{
    if(!ctx.app)
        return;
    const UiScreen& s = GetScreen(UiNav_Active(ctx.app->ui_nav));
    if(s.Render)
        s.Render(ctx);
}
