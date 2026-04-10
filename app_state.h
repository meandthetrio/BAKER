#pragma once
#include <cstdint>

#include "app_state_diagnostics.h"
#include "app_state_engine.h"
#include "app_state_project.h"
#include "app_state_recording.h"
#include "app_state_shared.h"
#include "app_state_ui.h"
#include "app_state_worker.h"

struct AppState
{
    AppUiState ui{};
    AppProjectState project{};
    AppWorkerState worker{};
    AppRecordingState recording{};
    AppEngineState engine{};
    AppDiagnosticsState diag{};
    AppSharedState shared{};
};

static inline const char* WaveChar(uint8_t w)
{
    return (w == 0) ? "S" : "P";
}
