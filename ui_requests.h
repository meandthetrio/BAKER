#pragma once

#include <cstdint>

struct AppUiState;
struct AppWorkerState;
struct AppState;

enum class UiReqType : uint8_t
{
    None = 0,
    RebuildCache,
    LoadSample,
    SavePreset,
    ScanSdWavs,
    LoadWavIndex,
    DeleteWavIndex,
    NormalizeCurrent,
    LoopFindCurrent,
    SaveRenderedWavCurrent,
    SaveRenderedWavNamed,
    SaveProject,
    LoadProject,
    ScanProjectSlots,
    DeleteProject,
    RenameProject,
    RenameWavIndex,
    UpdateWavStyleIndex,
    UpdateProjectStyle,
    COUNT
};

struct UiReq
{
    UiReqType type;
    uint16_t  a;
    uint16_t  b;
};

static constexpr uint32_t kUiReqQueueSize = 16;

struct UiReqQueue
{
    static constexpr uint32_t kCapacity = kUiReqQueueSize;

    UiReq    buffer[kCapacity];
    uint32_t head = 0;
    uint32_t tail = 0;
    uint32_t overflows = 0;
};

bool UiReq_Push(AppUiState& ui, AppWorkerState& worker, const UiReq& r);
bool UiReq_Push(AppState& app, const UiReq& r);
bool UiReq_Pop(AppWorkerState& worker, UiReq& out);
bool UiReq_Pop(AppState& app, UiReq& out);
uint32_t UiReq_Dropped(const AppState& app);
uint32_t UiReq_Fill(const AppState& app);
