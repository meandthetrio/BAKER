#pragma once

class OledPager;

void DrawRencFocusFrame(OledPager& d, int x, int y, int w, int h);
void DrawRencFocusTinyString(OledPager& d, const char* str, int x, int y);
void DrawRencFocusMicroString(OledPager& d, const char* str, int x, int y);
void DrawRencFocusString6x8(OledPager& d, const char* str, int x, int y);

void DrawVerticalFadersInRect(OledPager& d,
                              int x,
                              int y,
                              int w,
                              int h,
                              const char* const* labels,
                              const float* values,
                              int count,
                              bool select_active,
                              int selected_index,
                              const int* x_offsets = nullptr,
                              const bool* circle_handles = nullptr,
                              const bool* hide_rails = nullptr,
                              const bool* hide_handles = nullptr,
                              int selected_label_box_y_offset = 0,
                              int selected_label_box_extra_bottom = 0,
                              int selected_label_box_bottom_clip_extra = 0);

void DrawDelayDetailFaders(OledPager& d,
                           int x,
                           int y,
                           int w,
                           int h,
                           const char* const labels[4],
                           const float values[4],
                           bool select_active,
                           int selected_index,
                           int out_lbl_x0[4],
                           int out_lbl_y0[4],
                           int out_lbl_x1[4],
                           int out_lbl_y1[4]);
