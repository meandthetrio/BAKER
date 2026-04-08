#pragma once

#include <cstdint>

struct AppState;
class Params;
class OledPager;
struct UiInputEvent;

enum class UiScreenId : uint8_t
{
    Hud = 0,
    Fx,
    Mod,
    Macro,
    SdBrowse,
    SdDeleteConfirm,
    SampleEdit,
    ShiftMenu,
    Presets,      // HOME -> PRESETS
    Record,
    Start,
    PerformMenu,
    PerformEngine,
    PerformWaveEdit,
    PerformKeyzone,
    PerformAdsr,
    PerformEmphasis,
    PerformProcess,
    ProjectStatus,
    COUNT
};

struct UiScreenCtx
{
    AppState*  app = nullptr;
    Params*    params = nullptr;
    OledPager* display = nullptr;
    uint32_t   now_ms = 0;
    bool       shift = false;
    bool       lshift = false;
    bool       rshift = false;
};

struct UiScreen
{
    UiScreenId id;
    void (*OnEnter)(UiScreenCtx&);
    void (*OnExit)(UiScreenCtx&);
    bool (*OnEvent)(UiScreenCtx&, const UiInputEvent&);
    void (*Render)(UiScreenCtx&);
    using UiOnEnterFn = bool(*)(UiScreenCtx&);
    UiOnEnterFn on_enter = nullptr;
};

static constexpr uint8_t kUiStackMax = 8;

struct UiNav
{
    UiScreenId stack[kUiStackMax]{};
    uint8_t    top = 0;
};

UiScreenId UiNav_Active(const UiNav& nav);
bool UiNav_Push(UiNav& nav, UiScreenId next);
bool UiNav_Pop(UiNav& nav);

const UiScreen& GetScreen(UiScreenId id);
void UiRouter_DispatchEvent(UiScreenCtx& ctx, const UiInputEvent& e);
void UiRouter_Render(UiScreenCtx& ctx);
