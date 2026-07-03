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
    LoadWavIndexSdManage,
    LoadWavToBakePreview,
    // Async .bk multisample load into layer B. Streamed one DMA read per worker
    // tick (like the WAV loaders) so it never issues back-to-back SDMMC DMA in a
    // tight blocking loop — the synchronous read did, and hard-failed (FR_DISK_ERR)
    // after a project load. `a` = sd.paths[] index of the focused .bk.
    LoadBkIndex,
    DeleteWavIndex,
    NormalizeCurrent,
    LoopFindCurrent,
    SaveRenderedWavCurrent,
    SaveRenderedWavNamed,
    SaveSdManageTrimNamed,
    ReplaceSdManageTrimCurrent,
    SaveProject,
    LoadProject,
    ScanProjectSlots,
    DeleteProject,
    RenameProject,
    RenameWavIndex,
    UpdateWavStyleIndex,
    UpdateProjectStyle,
    CraftRenderToWav,
    CraftRenderToPreview,
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
