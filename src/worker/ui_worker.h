#pragma once

#include <cstdint>

struct AppEngineState;
struct AppProjectState;
struct AppSharedState;
struct AppUiState;
struct AppWorkerState;
class Params;

// Top-level worker orchestration seam.
void UiWorker_Tick(AppUiState& ui,
                   AppProjectState& project,
                   AppEngineState& engine,
                   AppSharedState& shared,
                   AppWorkerState& worker,
                   Params& params,
                   uint32_t now_ms,
                   uint16_t budget_us);
