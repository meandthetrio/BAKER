#pragma once

#include <cstdint>

#include "sample_style.h"

struct AppSharedState;
struct AppUiState;
struct AppWorkerState;
struct SdBrowserState;

enum class SampleSaveSource : uint8_t
{
    LiveSlot = 0,
    Recording,
    SdManage,
};

bool StartNormalize(AppUiState& ui, AppSharedState& shared);
bool NormalizeStep(AppUiState& ui, AppSharedState& shared, uint16_t budget_us);
bool LoopFindCurrent(AppUiState& ui, AppSharedState& shared);

bool StartSave(AppUiState& ui,
               AppSharedState& shared,
               SampleSaveSource save_source,
               const char* save_stem = nullptr,
               const char* replace_path = nullptr);
bool SaveStep(SdBrowserState& sd,
              AppSharedState& shared,
              AppWorkerState& worker,
              uint16_t budget_us);
bool DeleteWavAtIndex(SdBrowserState& sd, uint16_t idx);
bool RenameWavAtIndex(SdBrowserState& sd, uint16_t idx, const char* new_stem);
bool UpdateWavStyleAtIndex(SdBrowserState& sd, uint16_t idx, SampleStyle desired_style);
