#pragma once

#include <cstdint>

struct AppState;
enum class UiReqType : uint8_t;

uint8_t ProjectActions_WrapSlot(int slot);
bool ProjectActions_TriggerRequest(AppState& app, UiReqType req_type, uint8_t slot);
