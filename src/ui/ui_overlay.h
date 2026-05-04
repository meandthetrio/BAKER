#pragma once

#include <cstdint>

struct AppState;
struct AppDiagnosticsState;
struct AppUiState;
struct AppWorkerState;
class Params;
struct UiLayout;
class OledPager;

enum DiagnosticsOverlayPage : uint8_t
{
    kDiagOverlayPageSys = 0,
    kDiagOverlayPageGain1,
    kDiagOverlayPageGain2,
    kDiagOverlayPageCount
};

struct UiOverlayState
{
    bool     visible = false;
    bool     modal_active = false;
    uint8_t  page = kDiagOverlayPageSys;
    uint32_t shown_since_ms = 0;
};

void UiOverlay_Update(UiOverlayState& o, uint32_t now_ms);
void UiOverlay_Render(const AppUiState& ui,
                      const AppDiagnosticsState& diag,
                      const AppWorkerState& worker_state,
                      const Params& params,
                      const UiLayout& layout,
                      OledPager& oled);
