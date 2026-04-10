#include "mod_matrix.h"

static ModRoute MakeRoute(ModSource src, ModDest dst, float amount, bool enabled)
{
    ModRoute r;
    r.src = static_cast<uint8_t>(src);
    r.dst = static_cast<uint8_t>(dst);
    r.amount = amount;
    r.enabled = enabled ? 1 : 0;
    return r;
}

void ModMatrix_InitDefaults(ModMatrixState& state, ModRoute* ui_routes)
{
    if(ui_routes)
    {
        ui_routes[0] = MakeRoute(ModSource::LFO, ModDest::FilterCutoff, 0.5f, true);
        ui_routes[1] = MakeRoute(ModSource::ModEnv, ModDest::FilterCutoff, 0.5f, true);
        for(size_t i = 2; i < kMaxModRoutes; ++i)
            ui_routes[i] = MakeRoute(ModSource::LFO, ModDest::FilterCutoff, 0.0f, false);
    }

    for(size_t i = 0; i < kMaxModRoutes; ++i)
    {
        const ModRoute r = ui_routes ? ui_routes[i]
                                     : MakeRoute(ModSource::LFO,
                                                 ModDest::FilterCutoff,
                                                 0.0f,
                                                 false);
        state.routes_a[i] = r;
        state.routes_b[i] = r;
    }

    state.routes_sel.store(0, std::memory_order_relaxed);
    state.routes_gen.store(0, std::memory_order_relaxed);
}

void ModMatrix_Publish(ModMatrixState& state, const ModRoute* ui_routes)
{
    if(!ui_routes)
        return;

    const uint8_t sel = state.routes_sel.load(std::memory_order_relaxed) & 1u;
    ModRoute* dst = (sel == 0) ? state.routes_b : state.routes_a;

    for(size_t i = 0; i < kMaxModRoutes; ++i)
        dst[i] = ui_routes[i];

    state.routes_sel.store(sel ^ 1u, std::memory_order_release);
    state.routes_gen.fetch_add(1u, std::memory_order_release);
}
