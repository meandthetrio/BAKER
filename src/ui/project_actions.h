#pragma once

#include <cstdint>

struct AppUiState;
struct AppProjectState;
struct AppWorkerState;
struct AppState;
enum class UiReqType : uint8_t;

uint8_t ProjectActions_WrapSlot(int slot);
bool ProjectActions_TriggerRequest(AppUiState& ui,
                                   AppProjectState& project,
                                   AppWorkerState& worker,
                                   UiReqType req_type,
                                   uint8_t slot);
bool ProjectActions_TriggerRequest(AppState& app, UiReqType req_type, uint8_t slot);
