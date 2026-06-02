#pragma once

#include <cstdint>

struct AppEngineState;
struct AppSharedState;
struct AppWorkerState;
struct AppUiState;
struct ProjectRestoreState;
struct SdBrowserState;

void ClearProjectRestoreStateInternal(ProjectRestoreState& project_restore);

bool StartLoadInternal(SdBrowserState& sd, AppSharedState& shared, uint16_t index);
bool StartLoadSdManage(SdBrowserState& sd, AppSharedState& shared, uint16_t index);
bool LoadStepInternal(SdBrowserState& sd,
                      AppWorkerState& worker,
                      AppEngineState& engine,
                      AppSharedState& shared,
                      ProjectRestoreState& project_restore,
                      uint16_t budget);
