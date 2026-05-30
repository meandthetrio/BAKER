#include "ui_screens.h"

#include "app_state_diagnostics.h"
#include "app_state_ui.h"
#include "app_state_engine.h"
#include "ui_input.h"

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

void UiRouter_DispatchEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.ui)
        return;

    const UiScreenId active = UiNav_Active(ctx.ui->ui_nav);
    const UiScreen& s = GetScreen(active);

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPodEnc)
    {
        // SHIFT uses BACK to cancel pending bootloader arm before leaving.
        if(active == UiScreenId::ShiftMenu && s.OnEvent && s.OnEvent(ctx, e))
            return;

        // Record uses BACK for in-screen state transitions (review/back-confirm/stop).
        if(active == UiScreenId::Record && s.OnEvent && s.OnEvent(ctx, e))
            return;

        // Render execute/review own BACK for capture safety and temp-render discard.
        if((active == UiScreenId::RecordRenderExecute || active == UiScreenId::RecordRenderReview)
           && s.OnEvent && s.OnEvent(ctx, e))
            return;

        // PROCESS detail uses BACK to return from FX detail to MASTER BUS,
        // not to leave the PROCESS screen entirely.
        if(active == UiScreenId::PerformProcess && s.OnEvent && s.OnEvent(ctx, e))
            return;

        // EXPRESS detail uses BACK to return from PolyPorto detail to EXPRESS main.
        if(active == UiScreenId::PerformExpress && s.OnEvent && s.OnEvent(ctx, e))
            return;

        // TRIM screen uses BACK to cancel pending start/end edits.
        if(active == UiScreenId::PerformWaveEdit && s.OnEvent && s.OnEvent(ctx, e))
            return;

        // RENAME PROJECT uses LEnc click as in-screen delete, not global back.
        if(active == UiScreenId::RenameProject && s.OnEvent && s.OnEvent(ctx, e))
            return;

        if(UiNav_Pop(ctx.ui->ui_nav))
            ctx.ui->ui_dirty = true;
        return;
    }

    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc && s.on_enter)
    {
        if(s.on_enter(ctx))
        {
            ctx.ui->ui_dirty = true;
            return;
        }
    }

    if(s.OnEvent)
        s.OnEvent(ctx, e);
}

void UiRouter_Render(UiScreenCtx& ctx)
{
    if(!ctx.ui)
        return;

    if(ctx.diag && ctx.diag->overlay.modal_active)
    {
        const UiScreen& active = GetScreen(UiNav_Active(ctx.ui->ui_nav));
        if(active.Render)
            active.Render(ctx);
        return;
    }

    // Hold LShift to temporarily preview parent without changing nav state.
    if(ctx.lshift && ctx.ui->ui_parent_preview_active)
    {
        if(ctx.ui->ui_parent_preview_mode == 2
           && UiNav_Active(ctx.ui->ui_nav) == UiScreenId::PerformProcess)
        {
            // In-screen parent: PROCESS detail / EQ graph -> PROCESS main.
            const bool saved_detail = ctx.engine->process.perform_process_detail_active;
            const bool saved_eqg    = ctx.engine->process.perform_process_eq_graph_active;
            ctx.engine->process.perform_process_detail_active  = false;
            ctx.engine->process.perform_process_eq_graph_active = false;
            const UiScreen& active = GetScreen(UiScreenId::PerformProcess);
            if(active.Render)
                active.Render(ctx);
            ctx.engine->process.perform_process_detail_active   = saved_detail;
            ctx.engine->process.perform_process_eq_graph_active = saved_eqg;
            return;
        }

        if(ctx.ui->ui_parent_preview_mode == 1 && ctx.ui->ui_nav.top > 0)
        {
            const UiScreenId parent_id = ctx.ui->ui_nav.stack[ctx.ui->ui_nav.top - 1];
            const UiScreen& parent = GetScreen(parent_id);
            if(parent.Render)
            {
                parent.Render(ctx);
                return;
            }
        }
    }

    const UiScreen& active = GetScreen(UiNav_Active(ctx.ui->ui_nav));
    if(active.Render)
        active.Render(ctx);
}
