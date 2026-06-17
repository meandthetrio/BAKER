#include "ui_screens_internal.h"
#include "ui_screen_perform_process_internal.h"

#include <cstdio>

#include "app_state_ui.h"
#include "app_state_engine.h"
#include "app_state_recording.h"
#include "app_state_project.h"
#include "app_state_diagnostics.h"
#include "app_state_shared.h"
#include "app_state_worker.h"
#include "express_state.h"
#include "oled_pager.h"
#include "params.h"
#include "ui_input.h"
#include "ui_layout.h"

namespace
{
static bool ProcessMainCursorLocked(const AppSharedState& shared,
                                    const AppEngineState& engine,
                                    uint8_t layer,
                                    uint8_t main_cursor)
{
    (void)shared;
    (void)engine;
    (void)layer;
    (void)main_cursor;
    return false;
}

static void ProcessEnsureValidMainCursor(const AppSharedState& shared,
                                         AppEngineState& engine,
                                         uint8_t layer)
{
    engine.process.perform_process_main_cursor %= 6u;
    if(!ProcessMainCursorLocked(shared, engine, layer, engine.process.perform_process_main_cursor))
        return;

    for(uint8_t step = 1; step < 6u; ++step)
    {
        const uint8_t next = static_cast<uint8_t>((engine.process.perform_process_main_cursor + step) % 6u);
        if(!ProcessMainCursorLocked(shared, engine, layer, next))
        {
            engine.process.perform_process_main_cursor = next;
            if(next >= 2u)
                engine.process.perform_process_fx_cursor = static_cast<uint8_t>((next - 2u) & 0x03u);
            return;
        }
    }
}

static void ProcessEnsureValidDetailParam(const AppSharedState& shared,
                                          AppEngineState& engine,
                                          uint8_t layer,
                                          uint8_t fx_cursor,
                                          uint8_t sat_mode)
{
    const uint8_t cursor = fx_cursor & 0x03u;
    const uint8_t fx_id = engine.process.perform_process_fx_order[cursor];
    const uint8_t count = ProcessDetailParamCount(fx_id, sat_mode);
    if(count == 0u)
        return;

    engine.process.perform_process_detail_param[cursor] %= count;
    (void)shared;
    (void)engine;
    (void)layer;
    (void)fx_id;
}
} // namespace

bool PerformProcess_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.ui || !ctx.params)
        return false;
    if(ctx.shift)
        return false;

    AppUiState& ui = *ctx.ui;
    AppEngineState& engine = *ctx.engine;
    AppSharedState& shared = *ctx.shared;
    const uint8_t layer = engine.perform_nav.perform_layer & 1u;
    ProcessEnsureValidMainCursor(shared, engine, layer);
    ProcessEnsureValidDetailParam(shared, engine, layer, engine.process.perform_process_fx_cursor,
                                  ctx.params->EditTargets().sat_mode);
    const uint8_t main_cursor = static_cast<uint8_t>(engine.process.perform_process_main_cursor % 6u);
    const bool main_selects_fx = (main_cursor >= 2u);
    const uint8_t cursor = main_selects_fx ? static_cast<uint8_t>((main_cursor - 2u) & 0x03u)
                                           : static_cast<uint8_t>(engine.process.perform_process_fx_cursor & 0x03u);
    const uint8_t fx_id = engine.process.perform_process_fx_order[cursor];
    auto apply_fx_reorder = [&](int encoder_delta) -> bool
    {
        if(!ctx.rshift || !main_selects_fx || encoder_delta == 0)
            return false;
        const int from = static_cast<int>(main_cursor - 2u);
        if(from < 0 || from > 3)
            return true;
        if(engine.process.perform_process_fx_order[from] == 1u)
            return true;
        const int dir = (encoder_delta > 0) ? 1 : -1;
        const int to = from + dir;
        if(to < 0 || to > 2)
            return true;

        const uint8_t tmp = engine.process.perform_process_fx_order[from];
        engine.process.perform_process_fx_order[from] = engine.process.perform_process_fx_order[to];
        engine.process.perform_process_fx_order[to] = tmp;
        engine.process.perform_process_fx_cursor = static_cast<uint8_t>(to);
        engine.process.perform_process_main_cursor = static_cast<uint8_t>(to + 2);

        PerformParamsTargets& t = ctx.params->EditTargets();
        for(int i = 0; i < 4; ++i)
            t.fx_order[i] = engine.process.perform_process_fx_order[i];
        ctx.params->PublishTargets();
        return true;
    };

    // POD2 toggles layer (same behavior as other PERFORM pages).
    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPod2)
    {
        ProcessHandleLayerToggle(ctx);
        return true;
    }

    if(e.type == UiInputType::BtnDown
       && (e.id == kUiBtnPod1 || e.id == kUiBtnPodEnc)
       && (engine.process.perform_process_detail_active || engine.process.perform_process_eq_graph_active))
    {
        engine.process.perform_process_detail_active   = false;
        engine.process.perform_process_eq_graph_active = false;
        ui.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        if(engine.process.perform_process_eq_graph_active)
        {
            ui.ui_dirty = true;
            return true;
        }

        if(!engine.process.perform_process_detail_active)
        {
            if(!main_selects_fx)
            {
                ProcessHandleLayerMuteToggle(ctx, main_cursor & 1u); // 0=VOL A, 1=VOL B
                return true;
            }
            {
                const uint8_t c = static_cast<uint8_t>((engine.process.perform_process_main_cursor - 2u) & 0x03u);
                const uint8_t fid = engine.process.perform_process_fx_order[c];
                if(fid == 1u)
                {
                    engine.process.perform_process_eq_graph_active = true;
                }
                else
                {
                    engine.process.perform_process_detail_active = true;
                    const uint8_t detail_count
                        = ProcessDetailParamCount(fid, ctx.params->EditTargets().sat_mode);
                    if(detail_count > 0u
                       && engine.process.perform_process_detail_param[c] >= detail_count)
                        engine.process.perform_process_detail_param[c] = 0u;
                }
            }
            ui.ui_dirty = true;
            return true;
        }

        // In ADSR-style FX detail, toggles change via encoder scroll, not click.
        ui.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod && e.value != 0)
    {
        if(!engine.process.perform_process_detail_active && !engine.process.perform_process_eq_graph_active
           && apply_fx_reorder(e.value))
        {
            ui.ui_dirty = true;
            return true;
        }
        if(engine.process.perform_process_eq_graph_active && fx_id == 1u)
        {
            PerformParamsTargets& t = ctx.params->EditTargets();
            const float step = 0.018f * static_cast<float>(e.value);
            t.eq_center_norm = Clamp01(t.eq_center_norm + step);
            ctx.params->PublishTargets();
            ui.ui_dirty = true;
            return true;
        }
        if(engine.process.perform_process_detail_active)
        {
            int idx = static_cast<int>(engine.process.perform_process_detail_param[cursor]) + e.value;
            const int count = static_cast<int>(
                ProcessDetailParamCount(fx_id, ctx.params->EditTargets().sat_mode));
            while(idx < 0)
                idx += count;
            while(idx >= count)
                idx -= count;
            engine.process.perform_process_detail_param[cursor] = static_cast<uint8_t>(idx);
            ui.ui_dirty = true;
            return true;
        }
        int idx = static_cast<int>(main_cursor) + e.value;
        do
        {
            while(idx < 0)
                idx += 6;
            while(idx >= 6)
                idx -= 6;
            engine.process.perform_process_main_cursor = static_cast<uint8_t>(idx);
            idx += (e.value < 0) ? -1 : 1;
        } while(ProcessMainCursorLocked(shared, engine, layer, engine.process.perform_process_main_cursor));
        if(engine.process.perform_process_main_cursor >= 2u)
            engine.process.perform_process_fx_cursor = static_cast<uint8_t>((engine.process.perform_process_main_cursor - 2u) & 0x03u);
        ui.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt && e.value != 0)
    {
        static uint32_t s_last_ext_t_ms = 0u;
        const float delta = UiDeltaNormAccelerated(e.value, e.t_ms, s_last_ext_t_ms, 0.02f);
        if(!engine.process.perform_process_detail_active && !engine.process.perform_process_eq_graph_active
           && apply_fx_reorder(e.value))
        {
            ui.ui_dirty = true;
            return true;
        }
        if(engine.process.perform_process_eq_graph_active && fx_id == 1u)
        {
            ProcessEditEqGraph(ctx, delta);
            ctx.params->PublishTargets();
            ui.ui_dirty = true;
            return true;
        }
        if(engine.process.perform_process_detail_active)
        {
            const uint8_t pidx = engine.process.perform_process_detail_param[cursor];
            const bool changed = ProcessEditFxDetail(ctx, e, fx_id, pidx, delta);
            if(changed)
                ctx.params->PublishTargets();
            ui.ui_dirty = true;
            return true;
        }

        if(ctx.rshift)
        {
            ui.ui_dirty = true;
            return true;
        }

        if(!main_selects_fx)
        {
            ProcessHandleLayerVolumeEdit(ctx, e, main_cursor & 1u);
            return true;
        }

        PerformParamsTargets& t = ctx.params->EditTargets();

        switch(fx_id)
        {
            case 0: // S = saturation drive
                t.sat_drive = Clamp01(t.sat_drive + delta);
                t.sat_on = (t.sat_drive > 0.001f);
                break;
            case 1: // EQ lane is label-only in PROCESS main
                ui.ui_dirty = true;
                return true;
            case 2: // D = delay wet
                t.delay_mix = Clamp01(t.delay_mix + delta);
                t.delay_on = (t.delay_mix > 0.001f);
                break;
            case 3: // R = reverb wet
            default:
                if(engine.process.perform_process_fx_order[cursor] == 3u
                   && ExpressUiTargetLocked(shared, engine, layer, kExpressReverb))
                {
                    ui.ui_dirty = true;
                    return true;
                }
                t.reverb_mix = Clamp01(t.reverb_mix + delta);
                t.reverb_on = (t.reverb_mix > 0.001f);
                break;
        }
        for(int i = 0; i < 4; ++i)
            t.fx_order[i] = engine.process.perform_process_fx_order[i];

        ctx.params->PublishTargets();
        ui.ui_dirty = true;
        return true;
    }

    return false;
}

void PerformProcess_Render(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.display || !ctx.params)
        return;

    AppUiState& ui = *ctx.ui;
    AppEngineState& engine = *ctx.engine;
    AppSharedState& shared = *ctx.shared;
    const uint8_t layer = engine.perform_nav.perform_layer & 1u;
    ProcessEnsureValidMainCursor(shared, engine, layer);
    ProcessEnsureValidDetailParam(shared, engine, layer, engine.process.perform_process_fx_cursor,
                                  ctx.params->EditTargets().sat_mode);
    EngineRefreshLoadedMetadata(ui, engine, shared);

    OledPager& d = *ctx.display;
    d.Fill(false);

    if(engine.process.perform_process_eq_graph_active)
    {
        const uint8_t cursor = engine.process.perform_process_fx_cursor & 0x03u;
        const uint8_t g_fx = engine.process.perform_process_fx_order[cursor];
        if(g_fx == 1u)
        {
            const PerformParamsTargets& tg = ctx.params->TargetsForUI();
            DrawEqGraphScreen(d, tg, ctx.now_ms);
            return;
        }
        engine.process.perform_process_eq_graph_active = false;
    }

    if(engine.process.perform_process_detail_active)
    {
        const uint8_t cursor = engine.process.perform_process_fx_cursor & 0x03u;
        const uint8_t fx_id = engine.process.perform_process_fx_order[cursor];
        const uint8_t pidx = engine.process.perform_process_detail_param[cursor];
        const PerformParamsTargets& t = ctx.params->TargetsForUI();
        DrawFxDetailScreen(d, t, fx_id, pidx, ctx.now_ms, ctx.rshift);
        return;
    }

    const UiLayout layout = UiLayout_Default();
    const char* header_label = "process";
    const int header_w = MicroStringWidth(header_label);
    const int box_w = header_w + 4;
    const int header_box_h = kMicroH + 4;
    int box_x = 128 - box_w;
    if(box_x < 0)
        box_x = 0;
    const bool header_invert_flash
        = static_cast<int32_t>(engine.layer.engine_header_invert_until_ms - ctx.now_ms) > 0;
    if(header_invert_flash)
    {
        d.DrawRect(box_x, 0, box_x + box_w - 1, header_box_h - 1, false, true);
        d.DrawRect(box_x, 0, box_x + box_w - 1, header_box_h - 1, true, false);
        DrawMicroString(d, header_label, box_x + 2, 2, true);
    }
    else
    {
        d.DrawRect(box_x, 0, box_x + box_w - 1, header_box_h - 1, true, true);
        DrawMicroString(d, header_label, box_x + 2, 2, false);
    }

    const uint8_t main_cursor = static_cast<uint8_t>(engine.process.perform_process_main_cursor % 6u);
    const int32_t selected_index = (main_cursor >= 2u) ? static_cast<int32_t>(main_cursor - 2u) : -1;
    const int box_y = layout.y_body;
    const int box_h = layout.y_footer - layout.y_body + layout.line_h;
    const PerformParamsTargets& t = ctx.params->TargetsForUI();

    if(box_h > 24)
    {
        const ProcessLayerVolumeUiState layer_volume_ui = ProcessSyncLayerVolumeUiState(engine, t);
        DrawProcessLayerVolumePane(d, layer_volume_ui, main_cursor, box_y, box_h);
    }

    // Keep right half for FX faders.
    constexpr int kPaneX = 60;
    constexpr int kPaneW = 64;
    const char* labels[4] = {"S", "", "D", "R"};
    float values[4] = {};
    bool hide_rails[4] = {false, false, false, false};
    bool hide_handles[4] = {false, false, false, false};
    int label_y_offsets[4] = {0, 0, 0, 0};
    int rail_bottom_clearance[4] = {0, 0, 0, 0};
    int lane_x[4] = {};
    for(int i = 0; i < 4; ++i)
    {
        const uint8_t fx_id = engine.process.perform_process_fx_order[i];
        const bool hide_locked_reverb = (fx_id == 3u)
                                        && ExpressUiTargetLocked(shared, engine, layer, kExpressReverb);
        switch(fx_id)
        {
            case 0:
                labels[i] = "S";
                values[i] = Clamp01(t.sat_drive);
                label_y_offsets[i] = -2;
                rail_bottom_clearance[i] = 3;
                break;
            case 1:
                labels[i] = "";
                values[i] = 1.0f;
                hide_rails[i] = true;
                hide_handles[i] = true;
                break;
            case 2:
                labels[i] = "D";
                values[i] = Clamp01(t.delay_mix);
                label_y_offsets[i] = -2;
                rail_bottom_clearance[i] = 3;
                break;
            case 3:
            default:
                labels[i] = "R";
                values[i] = Clamp01(t.reverb_mix);
                hide_rails[i] = hide_locked_reverb;
                hide_handles[i] = hide_locked_reverb;
                label_y_offsets[i] = -2;
                rail_bottom_clearance[i] = 3;
                break;
        }
    }

    const int fader_x = kPaneX;
    const int fader_w = kPaneW;
    const int fader_y = box_y + 1;
    const int fader_h = box_h - 2;
    if(fader_w > 4 && fader_h > 4)
    {
        const int32_t shared_selected_index
            = (selected_index >= 0 && selected_index < 4
               && engine.process.perform_process_fx_order[selected_index] != 1u)
                  ? selected_index
                  : -1;
        const int fader_left = fader_x + 4;
        const int fader_right = fader_x + fader_w - 5;
        const int span_x = fader_right - fader_left;
        for(int i = 0; i < 4; ++i)
        {
            lane_x[i] = fader_left;
            if(i > 0 && span_x > 0)
                lane_x[i] = fader_left + (span_x * i) / 3;
        }

        DrawVerticalFadersInRect(d,
                                 fader_x,
                                 fader_y,
                                 fader_w,
                                 fader_h,
                                 labels,
                                 values,
                                 4,
                                 (shared_selected_index >= 0),
                                 shared_selected_index,
                                 nullptr,
                                 nullptr,
                                 hide_rails,
                                 hide_handles,
                                 1,
                                 1,
                                 1,
                                 1,
                                 label_y_offsets,
                                 rail_bottom_clearance);

        DrawProcessFxReorderOverlay(d, fader_x, fader_y, fader_w, fader_h, selected_index, ctx.rshift);

        for(int i = 0; i < 4; ++i)
        {
            if(engine.process.perform_process_fx_order[i] != 1u)
                continue;

            const int lane_left = (i == 0) ? fader_x + 1 : ((lane_x[i - 1] + lane_x[i]) / 2) + 1;
            const int lane_right = (i == 3) ? (fader_x + fader_w - 2)
                                            : ((lane_x[i] + lane_x[i + 1]) / 2) - 1;
            const int lane_top = fader_y + 1;
            const int lane_bottom = fader_y + fader_h - 2;
            const bool focused = (selected_index == i);
            if(lane_right < lane_left || lane_bottom < lane_top)
                break;

            if(focused)
            {
                const int focus_left = lane_left + 1;
                const int focus_top = lane_top + 1;
                const int focus_right = lane_right - 1;
                const int focus_bottom = lane_bottom - 1;
                if(focus_right >= focus_left && focus_bottom >= focus_top)
                    d.DrawRect(focus_left, focus_top, focus_right, focus_bottom, true, true);
            }

            const char* top_label = "E";
            const char* bottom_label = "Q";
            const int top_w = TinyStringWidthCaseSensitiveTightColons(top_label);
            const int bottom_w = TinyStringWidthCaseSensitiveTightColons(bottom_label);
            const int lane_cx = lane_left + ((lane_right - lane_left) / 2);
            const int gap = 4;
            const int total_h = (Font5x7::H * 2) + gap;
            const int top_y = lane_top + ((lane_bottom - lane_top - total_h) / 2);
            const int bottom_y = top_y + Font5x7::H + gap;
            DrawTinyStringCaseSensitive(d, top_label, lane_cx - (top_w / 2), top_y, !focused);
            DrawTinyStringCaseSensitive(d, bottom_label, lane_cx - (bottom_w / 2), bottom_y, !focused);
            break;
        }
    }
}
