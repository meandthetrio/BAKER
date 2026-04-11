#pragma once

#include <cstddef>

class OledPager;
struct AppSharedState;

struct UiLayout
{
    int x = 0;
    int y_header = 0;
    int y_body = 0;
    int y_footer = 0;
    int line_h = 8;
    int rows_body = 0;
};

UiLayout UiLayout_Default();
void UiDraw_Header(OledPager& oled, const UiLayout& l, const char* title, const char* status);
void UiDraw_Footer(OledPager& oled, const UiLayout& l, const char* hint);
void BuildStatus(const AppSharedState& shared, char* out, size_t n);
