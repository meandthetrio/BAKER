#pragma once

struct UiScreenCtx;
struct AppUiState;
struct AppRecordingState;
struct AppSharedState;

void Record_StopPreview(AppRecordingState& recording, AppSharedState& shared);
void Record_RestoreArmedSource(AppUiState& ui,
                               AppRecordingState& recording,
                               AppSharedState& shared);
void Record_ApplyMonitorState(const AppUiState& ui,
                              const AppRecordingState& recording,
                              AppSharedState& shared);
void Record_RenderLiveCaptureStyle(UiScreenCtx& ctx, const char* title, float level_boost);
