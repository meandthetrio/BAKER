#include "ui_screens_internal.h"

#include <cmath>

#include "oled_pager.h"
#include "params.h"
#include "ui_draw_controls.h"

// Circle + centre dot + indicator hand. Shared by every process knob.
static void DrawProcessKnobBody(OledPager& d, int cx, int cy, int radius, float angle_rad)
{
    DrawCirclePixels(d, cx, cy, radius, true);
    d.DrawPixel(cx, cy, true);
    const int hand_r = radius - 2;
    const int hx = cx + static_cast<int>(std::cos(angle_rad) * static_cast<float>(hand_r));
    const int hy = cy + static_cast<int>(std::sin(angle_rad) * static_cast<float>(hand_r));
    d.DrawLine(cx, cy, hx, hy, true);
}

// Knob name label in micro font (matches the page header), focused or plain.
static void DrawProcessKnobLabel(OledPager& d, const char* label, int x, int y, bool focused)
{
    if(focused)
        DrawRencFocusMicroString(d, label, x, y);
    else
        DrawMicroString(d, label, x, y, true);
}

// Gap (px) between a knob's circle and its label.
static constexpr int kKnobLabelGap = 4;

// Volume knob (top of the process left-pane triangle): micro label centred
// ABOVE the knob, no value readout.
void DrawProcessVolumeKnob(OledPager& d,
                           int cx,
                           int cy,
                           int radius,
                           const char* label,
                           float angle_rad,
                           bool focused)
{
    DrawProcessKnobBody(d, cx, cy, radius, angle_rad);
    const int lw = MicroStringWidth(label);
    DrawProcessKnobLabel(d, label, cx - (lw / 2), cy - radius - kKnobLabelGap - kMicroH, focused);
}

// Filter knob (cutoff / resonance): micro label centred BELOW the knob, no value.
void DrawProcessFilterKnob(OledPager& d,
                           int cx,
                           int cy,
                           int radius,
                           const char* label,
                           float angle_rad,
                           bool focused)
{
    DrawProcessKnobBody(d, cx, cy, radius, angle_rad);
    const int lw = MicroStringWidth(label);
    DrawProcessKnobLabel(d, label, cx - (lw / 2), cy + radius + kKnobLabelGap, focused);
}

static void DrawProcessReorderLeftArrow(OledPager& d, int tip_x, int cy)
{
    d.DrawPixel(tip_x, cy, true);
    d.DrawPixel(tip_x + 1, cy - 1, true);
    d.DrawPixel(tip_x + 1, cy, true);
    d.DrawPixel(tip_x + 1, cy + 1, true);
    d.DrawPixel(tip_x + 2, cy - 2, true);
    d.DrawPixel(tip_x + 2, cy - 1, true);
    d.DrawPixel(tip_x + 2, cy, true);
    d.DrawPixel(tip_x + 2, cy + 1, true);
    d.DrawPixel(tip_x + 2, cy + 2, true);
}

static void DrawProcessReorderRightArrow(OledPager& d, int tip_x, int cy)
{
    d.DrawPixel(tip_x, cy, true);
    d.DrawPixel(tip_x - 1, cy - 1, true);
    d.DrawPixel(tip_x - 1, cy, true);
    d.DrawPixel(tip_x - 1, cy + 1, true);
    d.DrawPixel(tip_x - 2, cy - 2, true);
    d.DrawPixel(tip_x - 2, cy - 1, true);
    d.DrawPixel(tip_x - 2, cy, true);
    d.DrawPixel(tip_x - 2, cy + 1, true);
    d.DrawPixel(tip_x - 2, cy + 2, true);
}

void DrawProcessFxReorderOverlay(OledPager& d,
                                 int fader_x,
                                 int fader_y,
                                 int fader_w,
                                 int fader_h,
                                 int32_t selected_index,
                                 bool rshift_held)
{
    if(selected_index < 0 || selected_index >= 3 || !rshift_held)
        return;

    const int line_top = fader_y + 2;
    const int label_y = fader_y + fader_h - Font5x7::H - 1;
    const int line_bottom = label_y - 2;
    if(line_bottom <= line_top)
        return;

    const int fader_left = fader_x + 4;
    const int fader_right = fader_x + fader_w - 5;
    const int span_x = fader_right - fader_left;
    const int line_x = (span_x > 0) ? (fader_left + (span_x * selected_index) / 3) : fader_left;

    const int cy = line_top + ((line_bottom - line_top) / 2);
    if(selected_index > 0)
        DrawProcessReorderLeftArrow(d, line_x - 7, cy);
    if(selected_index < 2)
        DrawProcessReorderRightArrow(d, line_x + 7, cy);
}

static void DrawProcessSatDetail(OledPager& d,
                                 const PerformParamsTargets& t,
                                 uint8_t selected_param)
{
    constexpr int kDisplayW = 128;
    constexpr int kDisplayH = 64;
    constexpr int kBitResoStepCount = 3;
    static const char* kBitResoLabels[kBitResoStepCount] = {"CRUSH", "STATIC", "HISS"};
    auto bit_reso_index = [](float value) -> int
    {
        if(value < 0.0f) value = 0.0f;
        if(value > 1.0f) value = 1.0f;
        int idx = static_cast<int>(value * static_cast<float>(kBitResoStepCount - 1) + 0.5f);
        if(idx < 0) idx = 0;
        if(idx >= kBitResoStepCount) idx = kBitResoStepCount - 1;
        return idx;
    };

    constexpr int kMargin = 2;
    constexpr int kGap = 2;
    const int block_x = kMargin;
    const int block_w = kDisplayW / 4;
    const int block_y = Font5x7::H + 4;
    int block_h = kDisplayH - block_y - kMargin;
    if(block_h < 3) block_h = 3;
    const int box_h = (block_h - kGap) / 2;
    const bool tape_selected = (t.sat_mode == 0);
    const bool bit_selected = (t.sat_mode == 1);
    // Mode toggle is the last detail param: index 4 in TAPE (4 faders), 2 in BIT
    // (2 faders). Must match ProcessEditSatDetail / ProcessDetailParamCount.
    const int  mode_pidx = bit_selected ? 2 : 4;
    const bool mode_select_active = (selected_param == mode_pidx);
    d.DrawRect(block_x, block_y, block_x + block_w - 1, block_y + box_h - 1, true, false);
    d.DrawRect(block_x,
               block_y + box_h + kGap,
               block_x + block_w - 1,
               block_y + (box_h * 2) + kGap - 1,
               true,
               false);
    const int label_w1 = TinyStringWidth("TAPE");
    const int label_w2 = TinyStringWidth("BIT");
    const int label_y1 = block_y + (box_h - Font5x7::H) / 2;
    const int label_y2 = block_y + box_h + kGap + (box_h - Font5x7::H) / 2;
    int label_x1 = block_x + (block_w - label_w1) / 2;
    int label_x2 = block_x + (block_w - label_w2) / 2;
    if(label_x1 < block_x + 1) label_x1 = block_x + 1;
    if(label_x2 < block_x + 1) label_x2 = block_x + 1;
    if(mode_select_active && tape_selected)
    {
        d.DrawRect(label_x1 - 2, label_y1 - 2, label_x1 + label_w1 + 1, label_y1 + Font5x7::H + 1, true, false);
        DrawTinyString(d, "TAPE", label_x1, label_y1, true);
    }
    else
        DrawTinyString(d, "TAPE", label_x1, label_y1, true);
    if(mode_select_active && bit_selected)
    {
        d.DrawRect(label_x2 - 2, label_y2 - 2, label_x2 + label_w2 + 1, label_y2 + Font5x7::H + 1, true, false);
        DrawTinyString(d, "BIT", label_x2, label_y2, true);
    }
    else
        DrawTinyString(d, "BIT", label_x2, label_y2, true);

    const int fader_offset = 8;
    const int fader_x = block_x + block_w + kGap + fader_offset;
    const int fader_w = kDisplayW - fader_x - kMargin;
    if(fader_w <= 4)
        return;

    constexpr int kSatFaderMax = 4;
    // BIT: RESO SMPL (RESO drawn as discrete labels, fader rail/handle hidden).
    // TAPE: SAT BUMP TONE BIAS. BIAS is bipolar (-1..1) shown as 0..1.
    const int   sat_fader_count = (t.sat_mode == 1) ? 2 : 4;
    const char* fader_labels[kSatFaderMax];
    float       fader_values[kSatFaderMax];
    int         fader_offsets[kSatFaderMax]  = {0, 0, 0, 0};
    bool        circle_handles[kSatFaderMax] = {false, false, false, false};
    bool        hide_rails[kSatFaderMax]     = {false, false, false, false};
    bool        hide_handles[kSatFaderMax]   = {false, false, false, false};
    if(t.sat_mode == 1)
    {
        fader_labels[0] = "RESO";
        fader_labels[1] = "SMPL";
        fader_values[0] = t.sat_bit_reso;
        fader_values[1] = t.sat_bit_smpl;
        hide_rails[0]   = true;
        hide_handles[0] = true;
    }
    else
    {
        fader_labels[0] = "SAT";
        fader_labels[1] = "BUMP";
        fader_labels[2] = "TONE";
        fader_labels[3] = "BIAS";
        fader_values[0] = t.sat_drive;
        fader_values[1] = t.sat_bump;
        fader_values[2] = t.sat_tone;
        fader_values[3] = 0.5f + 0.5f * t.sat_bias;
    }
    int param_index = selected_param;
    const bool fader_select_active = (param_index >= 0 && param_index < sat_fader_count);
    if(!fader_select_active && !mode_select_active) param_index = 0;
    const int selected_label_style = 3;
    DrawVerticalFadersInRect(d,
                             fader_x,
                             block_y,
                             fader_w,
                             block_h,
                             fader_labels,
                             fader_values,
                             sat_fader_count,
                             fader_select_active,
                             param_index,
                             fader_offsets,
                             circle_handles,
                             hide_rails,
                             hide_handles,
                             0,
                             0,
                             0,
                             selected_label_style);
    if(t.sat_mode != 1)
        return;

    const int label_y = block_y + block_h - Font5x7::H - 1;
    int line_top = block_y + 2;
    int line_bottom = label_y - 2;
    if(line_bottom <= line_top)
        return;

    int fader_left = fader_x + 2;
    int line_x = fader_left;
    const char* lbl = "RESO";
    const int lbl_w = TinyStringWidth(lbl);
    int lbl_x = line_x - (lbl_w / 2);
    if(lbl_x < fader_x + 1) lbl_x = fader_x + 1;
    if(lbl_x + lbl_w > fader_x + fader_w - 2)
        lbl_x = fader_x + fader_w - 2 - lbl_w;
    line_x = lbl_x + (lbl_w / 2);
    const int cur_idx = bit_reso_index(t.sat_bit_reso);
    const int label_top = line_top + 1;
    const int label_gap = 3;
    int label_y0 = label_top;
    for(int i = 0; i < kBitResoStepCount; ++i)
    {
        const char* bits_label = kBitResoLabels[i];
        const int bits_w = TinyStringWidth(bits_label);
        const int bits_x = line_x - (bits_w / 2);
        const int bits_y = label_y0 + (i * (Font5x7::H + label_gap));
        if(bits_y >= line_top && bits_y <= line_bottom - Font5x7::H)
        {
            const bool is_selected = (i == cur_idx);
            if(is_selected)
            {
                d.DrawRect(bits_x - 2, bits_y - 2, bits_x + bits_w + 1, bits_y + Font5x7::H + 1, true, false);
                DrawTinyString(d, bits_label, bits_x, bits_y, true);
            }
            else
            {
                DrawTinyString(d, bits_label, bits_x, bits_y, true);
            }
        }
    }
}

static void DrawProcessEqDetailPlaceholder(OledPager& d)
{
    constexpr int kDisplayW = 128;
    constexpr int kDisplayH = 64;

    const char* msg = "EXT: graph";
    const int mw = TinyStringWidth(msg);
    int mx = (kDisplayW - mw) / 2;
    if(mx < 0)
        mx = 0;
    DrawTinyString(d, msg, mx, kDisplayH / 2 - Font5x7::H, true);
}

static void DrawProcessDelayDetail(OledPager& d,
                                   const PerformParamsTargets& t,
                                   uint8_t selected_param,
                                   uint32_t now_ms,
                                   bool rshift_held)
{
    constexpr int kDisplayW = 128;
    constexpr int kDisplayH = 64;
    constexpr int kDelayFaderCount = 3;
    constexpr int kMargin = 2;
    constexpr int kModeGap = 3;

    const int block_y = Font5x7::H + 4;
    int block_h = kDisplayH - block_y - kMargin;
    if(block_h < 3)
        block_h = 3;
    const int fader_x = kMargin;
    const int mode_w = TinyStringWidth("SEND") + 6;
    const int mode_h = Font5x7::H + 4;
    const int mode_x = kDisplayW - kMargin - mode_w;
    const int fader_w = mode_x - fader_x - kModeGap;
    if(fader_w <= 4)
        return;

    const char* fader_labels[kDelayFaderCount] = {"LTM", "RTM", "FBK"};
    const float fader_values[kDelayFaderCount]
        = {t.delay_time_l, t.delay_time_r, t.delay_feedback};
    int param_index = selected_param;
    const bool fader_select_active = (param_index >= 0 && param_index < kDelayFaderCount);
    if(!fader_select_active)
        param_index = 0;

    // Use the exact same shared renderer as reverb for identical rail/handle/label behavior.
    DrawVerticalFadersInRect(d,
                             fader_x,
                             block_y,
                             fader_w,
                             block_h,
                             fader_labels,
                             fader_values,
                             kDelayFaderCount,
                             fader_select_active,
                             param_index,
                             nullptr,
                             nullptr,
                             nullptr,
                             nullptr,
                             0,
                             0,
                             0,
                             3);

    // Compute label bounds for LTM/RTM dotted shift indicators using shared layout math.
    int lb_x0[4] = {};
    int lb_y0[4] = {};
    int lb_x1[4] = {};
    int lb_y1[4] = {};
    const int label_y = block_y + block_h - Font5x7::H - 1;
    const int fader_left = fader_x + 4;
    const int fader_right = fader_x + fader_w - 5;
    const int span_x = fader_right - fader_left;
    for(int i = 0; i < kDelayFaderCount; ++i)
    {
        int line_x = fader_left;
        if(kDelayFaderCount > 1 && span_x > 0)
            line_x = fader_left + (span_x * i) / (kDelayFaderCount - 1);
        const int lab_w = TinyStringWidth(fader_labels[i]);
        int lab_x = line_x - (lab_w / 2);
        if(lab_x < fader_x + 1)
            lab_x = fader_x + 1;
        if(lab_x + lab_w > fader_x + fader_w - 2)
            lab_x = fader_x + fader_w - 2 - lab_w;
        lb_x0[i] = lab_x - 1;
        lb_y0[i] = label_y - 1;
        lb_x1[i] = lab_x + lab_w;
        lb_y1[i] = label_y + Font5x7::H;
    }

    if(rshift_held)
    {
        // LTM/RTM must be dotted-only (no solid border): clear any shared solid focus box first.
        d.DrawRect(lb_x0[0] - 1, lb_y0[0] - 1, lb_x1[0] + 1, lb_y1[0] + 1, false, true);
        d.DrawRect(lb_x0[1] - 1, lb_y0[1] - 1, lb_x1[1] + 1, lb_y1[1] + 1, false, true);
        DrawTinyString(d, fader_labels[0], lb_x0[0] + 1, lb_y0[0] + 1, true);
        DrawTinyString(d, fader_labels[1], lb_x0[1] + 1, lb_y0[1] + 1, true);
        DrawClockwiseMarchingDottedRect(
            d, lb_x0[0] - 1, lb_y0[0] - 1, lb_x1[0] + 1, lb_y1[0] + 1, now_ms);
        DrawClockwiseMarchingDottedRect(
            d, lb_x0[1] - 1, lb_y0[1] - 1, lb_x1[1] + 1, lb_y1[1] + 1, now_ms);
        return;
    }

    if(fader_select_active && param_index == 0)
    {
        d.DrawRect(lb_x0[0] - 1, lb_y0[0] - 1, lb_x1[0] + 1, lb_y1[0] + 1, false, true);
        DrawTinyString(d, fader_labels[0], lb_x0[0] + 1, lb_y0[0] + 1, true);
        DrawDottedRect(d, lb_x0[0] - 1, lb_y0[0] - 1, lb_x1[0] + 1, lb_y1[0] + 1, true);
    }
    if(fader_select_active && param_index == 1)
    {
        d.DrawRect(lb_x0[1] - 1, lb_y0[1] - 1, lb_x1[1] + 1, lb_y1[1] + 1, false, true);
        DrawTinyString(d, fader_labels[1], lb_x0[1] + 1, lb_y0[1] + 1, true);
        DrawDottedRect(d, lb_x0[1] - 1, lb_y0[1] - 1, lb_x1[1] + 1, lb_y1[1] + 1, true);
    }

    const uint8_t active_mode = (t.delay_fader_mode == kDelayFaderModeMix)
                                    ? kDelayFaderModeMix
                                    : kDelayFaderModeSend;
    const bool mode_focused = (selected_param == 3u);
    const char* label = (active_mode == kDelayFaderModeMix) ? "MIX" : "SEND";
    const int label_w = TinyStringWidth(label);
    const int box_y = block_y + ((block_h - mode_h) / 2);
    int label_x = mode_x + ((mode_w - label_w) / 2);
    if(label_x < mode_x + 1)
        label_x = mode_x + 1;
    const int mode_label_y = box_y + ((mode_h - Font5x7::H) / 2);

    if(mode_focused)
        d.DrawRect(mode_x, box_y, mode_x + mode_w - 1, box_y + mode_h - 1, true, false);

    DrawTinyString(d, label, label_x, mode_label_y, true);
}

static void DrawProcessReverbDetail(OledPager& d,
                                    const PerformParamsTargets& t,
                                    uint8_t selected_param)
{
    constexpr int kDisplayW = 128;
    constexpr int kDisplayH = 64;
    constexpr int kReverbFaderCount = 4;
    constexpr int kMargin = 2;
    constexpr int kModeGap = 3;

    const int block_y = Font5x7::H + 4;
    int block_h = kDisplayH - block_y - kMargin;
    if(block_h < 3) block_h = 3;
    const int fader_x = kMargin;
    const int mode_w = TinyStringWidth("SEND") + 6;
    const int mode_h = Font5x7::H + 4;
    const int mode_x = kDisplayW - kMargin - mode_w;
    const int fader_w = mode_x - fader_x - kModeGap;
    if(fader_w <= 4)
        return;

    const char* fader_labels[kReverbFaderCount]
        = {"Pre", "Dmp", "Dcy", "Mod"};
    const float fader_values[kReverbFaderCount]
        = {t.reverb_pre, t.reverb_damp, t.reverb_decay, t.reverb_mod};
    int param_index = selected_param;
    const bool fader_select_active = (param_index >= 0 && param_index < kReverbFaderCount);
    if(!fader_select_active) param_index = 0;
    const int fader_offsets[kReverbFaderCount] = {0, 1, -1, 0};
    DrawVerticalFadersInRect(d,
                             fader_x,
                             block_y,
                             fader_w,
                             block_h,
                             fader_labels,
                             fader_values,
                             kReverbFaderCount,
                             fader_select_active,
                             param_index,
                             fader_offsets,
                             nullptr,
                             nullptr,
                             nullptr,
                             0,
                             0,
                             0,
                             3);

    const uint8_t active_mode = (t.reverb_fader_mode == kReverbFaderModeMix)
                                    ? kReverbFaderModeMix
                                    : kReverbFaderModeSend;
    const bool mode_focused = (selected_param == 4u);
    const char* label = (active_mode == kReverbFaderModeMix) ? "MIX" : "SEND";
    const int label_w = TinyStringWidth(label);
    const int box_y = block_y + ((block_h - mode_h) / 2);
    int label_x = mode_x + ((mode_w - label_w) / 2);
    if(label_x < mode_x + 1)
        label_x = mode_x + 1;
    const int label_y = box_y + ((mode_h - Font5x7::H) / 2);

    if(mode_focused)
        d.DrawRect(mode_x, box_y, mode_x + mode_w - 1, box_y + mode_h - 1, true, false);

    DrawTinyString(d, label, label_x, label_y, true);
}

void DrawFxDetailScreen(OledPager& d,
                        const PerformParamsTargets& t,
                        uint8_t fx_id,
                        uint8_t selected_param,
                        uint32_t now_ms,
                        bool rshift_held)
{
    constexpr int kDisplayW = 128;
    constexpr int kPerformFaderCount = 4;
    const char* labels[kPerformFaderCount] = {"SATURATION", "EQ", "DELAY", "REVERB"};
    int index = static_cast<int>(fx_id & 0x03u);
    if(index < 0 || index >= kPerformFaderCount)
        index = 0;

    {
        const char* label  = labels[index];
        const int   text_w = TinyStringWidth(label);
        int         text_x = (kDisplayW - text_w) / 2;
        if(text_x < 0)
            text_x = 0;
        DrawTinyString(d, label, text_x, 1, true);
    }

    switch(index)
    {
        case 0: DrawProcessSatDetail(d, t, selected_param); break;
        case 1: DrawProcessEqDetailPlaceholder(d); break;
        case 2: DrawProcessDelayDetail(d, t, selected_param, now_ms, rshift_held); break;
        case 3: DrawProcessReverbDetail(d, t, selected_param); break;
        default: break;
    }
}
