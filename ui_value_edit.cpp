#include "ui_value_edit.h"

#include "oled_pager.h"

#include <cstdio>

using namespace daisy;

static int16_t ClampValue(const UiValueSpec& spec, int32_t v)
{
    if(v < spec.min_i)
        v = spec.min_i;
    if(v > spec.max_i)
        v = spec.max_i;
    return static_cast<int16_t>(v);
}

void UiValueEdit_Begin(UiValueEdit& edit,
                       const char* label,
                       const UiValueSpec& spec,
                       int16_t start_i)
{
    edit.active = true;
    edit.label = label;
    edit.spec = spec;
    edit.value_i = ClampValue(spec, start_i);
    edit.original_i = edit.value_i;
}

bool UiValueEdit_OnEnc(UiValueEdit& edit, int delta)
{
    if(!edit.active || delta == 0)
        return false;

    const int32_t step = (edit.spec.step == 0) ? 1 : edit.spec.step;
    const int32_t v = static_cast<int32_t>(edit.value_i) + (static_cast<int32_t>(delta) * step);
    const int16_t clamped = ClampValue(edit.spec, v);
    if(clamped == edit.value_i)
        return false;
    edit.value_i = clamped;
    return true;
}

void UiValueEdit_Cancel(UiValueEdit& edit)
{
    edit.value_i = edit.original_i;
    edit.active = false;
}

void UiValueEdit_Commit(UiValueEdit& edit)
{
    edit.active = false;
}

void UiValueEdit_Render(const UiValueEdit& edit, OledPager& oled, int x, int y)
{
    if(!edit.active)
        return;

    char buf[24];
    oled.SetCursor(x, y);
    std::snprintf(buf, sizeof(buf), "EDIT:%s", edit.label ? edit.label : "");
    oled.WriteString(buf, Font_6x8, true);

    const char* text = nullptr;
    char val_buf[12];
    switch(edit.spec.type)
    {
        case UiValueType::Bool:
            text = (edit.value_i != 0) ? "ON" : "OFF";
            break;
        case UiValueType::Enum:
            if(edit.spec.enum_labels && edit.spec.enum_count > 0)
            {
                const int idx = (edit.value_i < 0) ? 0
                              : (edit.value_i >= edit.spec.enum_count)
                                    ? (edit.spec.enum_count - 1)
                                    : edit.value_i;
                text = edit.spec.enum_labels[idx];
            }
            break;
        case UiValueType::Bipolar1:
            std::snprintf(val_buf, sizeof(val_buf), "%+04d", (int)edit.value_i);
            text = val_buf;
            break;
        case UiValueType::Norm01:
        default:
            std::snprintf(val_buf, sizeof(val_buf), "%03d", (int)edit.value_i);
            text = val_buf;
            break;
    }

    oled.SetCursor(x, y + 8);
    std::snprintf(buf, sizeof(buf), "VAL:%s", text ? text : "");
    oled.WriteString(buf, Font_6x8, true);
}
