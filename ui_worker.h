#pragma once

#include <cstdint>

struct AppState;
class Params;

void UiWorker_Tick(AppState& app, Params& params, uint32_t now_ms, uint16_t budget_us);
