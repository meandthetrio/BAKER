#pragma once

#include "project_manifest.h"

#include <cstddef>
#include <cstdint>

struct AppUiState;
struct AppProjectState;
struct AppEngineState;
struct AppSharedState;
struct AppWorkerState;
struct ProjectSatState;
struct ProjectEqState;
class Params;

// Shared helpers implemented in ui_worker_project.cpp (used by save collect + load apply).
float ClampProjectFloat(float value, float lo, float hi);
int8_t ClampProjectTune(int value);
int8_t ClampProjectTuneCents(int value);
uint8_t ClampProjectMidiNote(int value);
void ClampProjectKeyzoneRange(uint8_t& lo, uint8_t& hi);
uint8_t ClampProjectAdsrRow(int value);
uint8_t ClampProjectPlayMode(int value);
int16_t ClampProjectEngineGainDb(int value);
uint8_t ClampProjectDriveMode(int value);
uint8_t ClampProjectSatMode(int value);
void ClampProjectAdsrGraph(uint8_t& a_x, uint8_t& d_x, uint8_t& r_x, uint8_t& s_level);
float ClampProjectFilterCutoffHz(float value);
void SanitizeProjectFxOrder(uint8_t* fx_order);
void ClampProjectSatState(ProjectSatState& sat);
void ClampProjectEqState(ProjectEqState& eq);
void ClampProjectDelayState(ProjectDelayState& delay);
void ClampProjectReverbState(ProjectReverbState& reverb);
bool MakeProjectSlotPath(char* out, size_t n, const char* base, uint8_t slot, const char* ext);
bool ProjectManifestHasLayer(const ProjectManifestV11& m, uint8_t layer);

// Project load subflow (ui_worker_project_load.cpp).
bool ReadProjectLoadManifest(AppUiState& ui,
                             AppProjectState& project,
                             uint8_t project_slot,
                             ProjectManifestV11& manifest);
void PrepareProjectLoadManifest(ProjectManifestV11& manifest);
void ApplyProjectLoadState(AppEngineState& engine,
                           AppSharedState& shared,
                           Params& params,
                           const ProjectManifestV11& manifest);
void SetupProjectRestoreState(AppWorkerState& worker,
                              AppEngineState& engine,
                              const ProjectManifestV11& manifest);
bool BeginProjectRestoreLoad(AppUiState& ui,
                             AppProjectState& project,
                             AppEngineState& engine,
                             AppSharedState& shared,
                             AppWorkerState& worker,
                             uint8_t project_slot);
