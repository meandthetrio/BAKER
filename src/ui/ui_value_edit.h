#pragma once

#include <cstdint>

class OledPager;

enum class UiValueType : uint8_t
{
    Bool = 0,
    Norm01,
    Bipolar1,
    Enum,
    COUNT
};

struct UiValueSpec
{
    UiValueType  type;
    int16_t      step;
    int16_t      min_i;
    int16_t      max_i;
    const char** enum_labels;
    uint8_t      enum_count;
};

struct UiValueEdit
{
    bool        active = false;
    const char* label = nullptr;
    UiValueSpec spec{};
    int16_t     value_i = 0;
    int16_t     original_i = 0;
};

void UiValueEdit_Begin(UiValueEdit& edit,
                       const char* label,
                       const UiValueSpec& spec,
                       int16_t start_i);

bool UiValueEdit_OnEnc(UiValueEdit& edit, int delta);
void UiValueEdit_Cancel(UiValueEdit& edit);
void UiValueEdit_Commit(UiValueEdit& edit);
void UiValueEdit_Render(const UiValueEdit& edit, OledPager& oled, int x, int y);
