#pragma once

#include <cstdint>

struct AppSharedState;
struct AppUiState;
struct AppWorkerState;
struct SdBrowserState;

bool StartNormalize(AppUiState& ui, AppSharedState& shared);
bool NormalizeStep(AppUiState& ui, AppSharedState& shared, uint16_t budget_us);
bool LoopFindCurrent(AppUiState& ui, AppSharedState& shared);

bool StartSave(AppUiState& ui, AppSharedState& shared);
bool SaveStep(SdBrowserState& sd,
              AppSharedState& shared,
              AppWorkerState& worker,
              uint16_t budget_us);
bool DeleteWavAtIndex(SdBrowserState& sd, uint16_t idx);
bool RenameWavAtIndex(SdBrowserState& sd, uint16_t idx, const char* new_stem);
