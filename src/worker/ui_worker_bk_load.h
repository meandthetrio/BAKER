#pragma once

#include <cstdint>

struct AppSharedState;
struct SdBrowserState;

// Async .bk multisample load into layer B (UiReqType::LoadBkIndex). Streamed one
// SDMMC DMA read per worker tick (see ui_worker_bk_load.cpp).
bool StartBkLoadInternal(SdBrowserState& sd, AppSharedState& shared, uint16_t index);
bool BkLoadStepInternal(SdBrowserState& sd, AppSharedState& shared, uint16_t budget);
