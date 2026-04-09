#pragma once

#include "ui_screens.h"

#include <cstddef>

class OledPager;
struct UiInputEvent;

void DrawScaledText6x8(OledPager& d, const char* text, int x, int y, int scale);
void DrawTinyString(OledPager& d, const char* str, int x, int y, bool on);
int TinyStringWidth(const char* str);
void ExtractBaseName(const char* path, char* out, size_t out_n);

bool ProjectStatus_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e);
void ProjectStatus_Render(UiScreenCtx& ctx);

void SdBrowse_OnEnter(UiScreenCtx& ctx);
bool SdBrowse_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e);
void SdBrowse_Render(UiScreenCtx& ctx);

bool SdDeleteConfirm_OnEnter(UiScreenCtx& ctx);
void SdDeleteConfirm_Render(UiScreenCtx& ctx);

bool SampleEdit_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e);
void SampleEdit_Render(UiScreenCtx& ctx);
