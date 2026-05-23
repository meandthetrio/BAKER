#pragma once

struct UiScreenCtx;
struct AppRecordingState;
struct AppSharedState;

void Record_StopPreview(AppRecordingState& recording, AppSharedState& shared);
void Record_RenderLiveCaptureStyle(UiScreenCtx& ctx, const char* title, float level_boost);
