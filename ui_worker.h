#pragma once

#include <cstdint>

struct AppState;

void UiWorker_Tick(AppState& app, uint32_t now_ms, uint16_t budget_us);
