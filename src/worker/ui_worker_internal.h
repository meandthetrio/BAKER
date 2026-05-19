#pragma once

#include <cstddef>

#include "app_state_project.h"
#include "project_manifest.h"
#include "storage_limits.h"

#include "fatfs.h"
#include "ff.h"
#include "per/sdmmc.h"

struct AppEngineState;
struct AppProjectState;
struct AppSharedState;
struct AppUiState;
struct AppWorkerState;
struct SdBrowserState;
class Params;

enum class LoaderState : uint8_t
{
    Idle,
    Scan,
    Load,
};

struct SdWorkerState
{
    daisy::SdmmcHandler sdmmc;
    daisy::FatFSInterface fsi;
    bool inited = false;
    bool mounted = false;
    DIR dir{};
    bool dir_open = false;
    FIL file{};
    bool file_open = false;
    char scan_path[kSdPathMax] = {};
    uint32_t data_size = 0;
    uint32_t bytes_loaded = 0;
    uint32_t sample_frames = 0;
    uint16_t load_index = 0;
    uint8_t loading_slot = 0;
    LoaderState state = LoaderState::Idle;
    bool norm_active = false;
    uint8_t norm_slot = 0;
    uint32_t norm_pos = 0;
    uint32_t norm_start = 0;
    uint32_t norm_end = 0;
    int32_t norm_peak = 0;
    bool save_active = false;
    uint8_t save_slot = 0;
    const int16_t* save_pcm = nullptr;
    uint32_t save_start = 0;
    uint32_t save_end = 0;
    uint32_t save_total = 0;
    uint32_t save_written = 0;
    float save_gain = 1.0f;
    char save_dir[kSdPathMax] = {};
    char save_path[kSdPathMax] = {};
    char save_name[kSdNameMax] = {};
    uint8_t project_restore_pending_mask = 0;
    char project_restore_path[kSdSampleSlots][kProjectPathMax] = {};
};

struct WavInfo
{
    uint16_t audio_format = 0;
    uint16_t channels = 0;
    uint32_t sample_rate = 0;
    uint16_t bits_per_sample = 0;
    uint32_t data_offset = 0;
    uint32_t data_size = 0;
};

extern SdWorkerState s_sd;

bool ParseWavHeader(FIL& file, WavInfo& info);
bool WriteWavHeader(FIL& file, uint32_t frames);
bool IsWavName(const char* name);
bool MakePath(char* out, size_t n, const char* base, const char* name);
void WorkerExtractBaseName(const char* path, char* out, size_t out_n);

uint8_t RequestedProjectSlot(const AppWorkerState& worker);
ProjectStyleId RequestedProjectStyle(const AppWorkerState& worker);
void SetProjectSlotStatus(AppProjectState& project, uint8_t slot, const char* msg);

bool EnsureSdMounted(AppUiState& ui);
bool EnsureSdMountedInternal(SdBrowserState& sd);
void CancelLoad(AppUiState& ui, AppWorkerState& worker, AppSharedState& shared);

// SD WAV scan (implemented in ui_worker_sd_scan.cpp; uses `s_sd`)
bool StartScan(SdBrowserState& sd, AppSharedState& shared);
bool ScanStep(SdBrowserState& sd);
void CancelScan(SdBrowserState& sd);
void ClearProjectRestoreState(AppWorkerState& worker);
bool StartNextProjectRestoreLoad(AppUiState& ui,
                                 AppEngineState& engine,
                                 AppWorkerState& worker,
                                 AppSharedState& shared);
bool StartLoadPath(AppUiState& ui, AppSharedState& shared, const char* path, uint8_t target_slot);

bool ReadProjectManifestFromFile(ProjectManifestV11& manifest);
bool ScanProjectSlots(AppUiState& ui, AppProjectState& project);
bool RenameProject(AppUiState& ui, AppProjectState& project, AppWorkerState& worker);
bool UpdateProjectStyle(AppUiState& ui, AppProjectState& project, AppWorkerState& worker);
bool SaveProject(AppUiState& ui,
                 AppProjectState& project,
                 AppEngineState& engine,
                 AppSharedState& shared,
                 AppWorkerState& worker,
                 const Params& params);
bool LoadProject(AppUiState& ui,
                 AppProjectState& project,
                 AppEngineState& engine,
                 AppSharedState& shared,
                 AppWorkerState& worker,
                 Params& params);
