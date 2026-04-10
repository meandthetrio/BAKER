#pragma once

#include <cstdint>

// `ui_screens.cpp` is the intentionally small shared UI facade:
// active-screen helpers plus cross-screen support that does not belong to one screen owner.
struct AppState;
struct AppUiState;
struct AppEngineState;
struct AppRecordingState;
struct AppProjectState;
struct AppDiagnosticsState;
struct AppSharedState;
struct AppWorkerState;
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

struct UiAppAccess
{
    AppState* root = nullptr;
    AppUiState& ui;
    AppEngineState& engine;
    AppRecordingState& recording;
    AppProjectState& project;
    AppDiagnosticsState& diag;
    AppSharedState& shared;
    AppWorkerState& worker;
    Params& params;

    operator AppState&() const { return *root; }
    AppState* operator->() const { return root; }
};

struct UiSessionState
{
    AppUiState* ui = nullptr;
    AppEngineState* engine = nullptr;
    AppRecordingState* recording = nullptr;
    AppProjectState* project = nullptr;
};

struct UiScreenCtx
{
    UiAppAccess* app = nullptr;
    UiSessionState* session = nullptr;
    AppUiState* ui = nullptr;
    AppEngineState* engine = nullptr;
    AppRecordingState* recording = nullptr;
    AppProjectState* project = nullptr;
    AppDiagnosticsState* diag = nullptr;
    AppSharedState* shared = nullptr;
    AppWorkerState* worker = nullptr;
    Params* params = nullptr;
    OledPager* display = nullptr;
    uint32_t now_ms = 0;
    bool shift = false;
    bool lshift = false;
    bool rshift = false;
};

inline void UiScreenCtx_BindSession(UiScreenCtx& ctx, UiSessionState& session)
{
    ctx.session = &session;
    ctx.ui = session.ui;
    ctx.engine = session.engine;
    ctx.recording = session.recording;
    ctx.project = session.project;
}

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
