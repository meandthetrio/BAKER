#pragma once

#include "ui_screens.h"
#include "ui_draw_controls.h"
#include "ui_draw_shapes.h"
#include "ui_draw_text.h"

#include <cstddef>

struct UiInputEvent;
struct AppState;
struct Sample;
struct SampleEdit;

// Internal contract for screen implementation units and the central registry.
// Keep this limited to cross-file screen entry points and the small shared
// helpers already consumed by extracted screen files.

// Shared non-text helpers used across extracted screen files.
void ExtractBaseName(const char* path, char* out, size_t out_n);
void DrawWaveformPreview(OledPager& d,
                         const Sample& sample,
                         const SampleEdit* edit,
                         int x,
                         int y,
                         int w,
                         int h,
                         bool on = true,
                         bool outline_only = false,
                         bool dotted_border = false);
void PublishEngineLayerParams(UiScreenCtx& ctx);
void EngineRefreshLoadedMetadata(AppUiState& ui, AppEngineState& engine, AppSharedState& shared);
bool ExpressUiEnabled(const AppSharedState& shared);
bool ExpressUiFlashLocked(uint32_t now_ms);
bool ExpressUiTargetLocked(const AppSharedState& shared,
                           const AppEngineState& engine,
                           uint8_t layer,
                           uint8_t target);

// Already-extracted screens.
bool ProjectStatus_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e);
void ProjectStatus_Render(UiScreenCtx& ctx);

void SdBrowse_OnEnter(UiScreenCtx& ctx);
bool SdBrowse_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e);
void SdBrowse_Render(UiScreenCtx& ctx);

bool SdDeleteConfirm_OnEnter(UiScreenCtx& ctx);
void SdDeleteConfirm_Render(UiScreenCtx& ctx);

bool SampleEdit_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e);
void SampleEdit_Render(UiScreenCtx& ctx);

// Upcoming extracted screen entry points.
// Keep `UiScreen::OnEnter` vs `UiScreen::on_enter` bindings unchanged when
// these move out of `ui_screens.cpp`.
bool MainMenu_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e);
bool MainMenu_OnEnter(UiScreenCtx& ctx);
void MainMenu_Render(UiScreenCtx& ctx);

bool Presets_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e);
void Presets_Render(UiScreenCtx& ctx);

void Record_OnEnter(UiScreenCtx& ctx);
void Record_OnExit(UiScreenCtx& ctx);
bool Record_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e);
void Record_Render(UiScreenCtx& ctx);

bool PerformMenu_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e);
bool PerformMenu_OnEnter(UiScreenCtx& ctx);
void PerformMenu_Render(UiScreenCtx& ctx);

void PerformEngine_OnScreenEnter(UiScreenCtx& ctx);
bool PerformEngine_OnEnter(UiScreenCtx& ctx);
bool PerformEngine_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e);
void PerformEngine_Render(UiScreenCtx& ctx);

void PerformWaveEdit_OnScreenEnter(UiScreenCtx& ctx);
bool PerformWaveEdit_OnEnter(UiScreenCtx& ctx);
bool PerformWaveEdit_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e);
void PerformWaveEdit_Render(UiScreenCtx& ctx);

bool PerformKeyzone_OnEnter(UiScreenCtx& ctx);
bool PerformKeyzone_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e);
void PerformKeyzone_Render(UiScreenCtx& ctx);

bool VelocityMod_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e);
void VelocityMod_Render(UiScreenCtx& ctx);
bool VelocityMod2_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e);
void VelocityMod2_Render(UiScreenCtx& ctx);
bool ModBlockA_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e);
void ModBlockA_Render(UiScreenCtx& ctx);
bool ModBlockB_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e);
void ModBlockB_Render(UiScreenCtx& ctx);

void PerformAdsr_OnScreenEnter(UiScreenCtx& ctx);
bool PerformAdsr_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e);
void PerformAdsr_Render(UiScreenCtx& ctx);

void PerformEmphasis_OnScreenEnter(UiScreenCtx& ctx);
bool PerformEmphasis_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e);
void PerformEmphasis_Render(UiScreenCtx& ctx);

void PerformExpress_OnScreenEnter(UiScreenCtx& ctx);
bool PerformExpress_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e);
void PerformExpress_Render(UiScreenCtx& ctx);

bool PerformProcess_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e);
void PerformProcess_Render(UiScreenCtx& ctx);

bool Hud_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e);
void Hud_Render(UiScreenCtx& ctx);

bool Fx_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e);
void Fx_Render(UiScreenCtx& ctx);

bool Mod_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e);
void Mod_Render(UiScreenCtx& ctx);

bool Macro_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e);
void Macro_Render(UiScreenCtx& ctx);

void ShiftMenu_OnScreenEnter(UiScreenCtx& ctx);
bool ShiftMenu_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e);
void ShiftMenu_Render(UiScreenCtx& ctx);
