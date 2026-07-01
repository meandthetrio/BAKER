#include "ui_screens_internal.h"

#include <cstdio>

#include "app_state_ui.h"
#include "app_state_engine.h"
#include "app_state_shared.h"
#include "oled_pager.h"
#include "ui_draw_controls.h"
#include "ui_input.h"

// Keyboard background bitmap: 128 wide x 13 rows rendered, 16-byte stride.
// Copied verbatim from oled_ui_sim ui_ref/ui_screens.cpp.
static const uint8_t kPerformKeyzoneKeyboard128x16[16 * 16] = {
    0xC4, 0x6C, 0x44, 0x6C, 0x46, 0xC4, 0x46, 0xC4, 0x6C, 0x44, 0x6C, 0x46, 0xC4, 0x46, 0xC4, 0x6C,
    0xC4, 0x6C, 0x44, 0x6C, 0x46, 0xC4, 0x46, 0xC4, 0x6C, 0x44, 0x6C, 0x46, 0xC4, 0x46, 0xC4, 0x6C,
    0xC4, 0x6C, 0x44, 0x6C, 0x46, 0xC4, 0x46, 0xC4, 0x6C, 0x44, 0x6C, 0x46, 0xC4, 0x46, 0xC4, 0x6C,
    0xC4, 0x6C, 0x44, 0x6C, 0x46, 0xC4, 0x46, 0xC4, 0x6C, 0x44, 0x6C, 0x46, 0xC4, 0x46, 0xC4, 0x6C,
    0xC4, 0x6C, 0x44, 0x6C, 0x46, 0xC4, 0x46, 0xC4, 0x6C, 0x44, 0x6C, 0x46, 0xC4, 0x46, 0xC4, 0x6C,
    0xC4, 0x6C, 0x44, 0x6C, 0x46, 0xC4, 0x46, 0xC4, 0x6C, 0x44, 0x6C, 0x46, 0xC4, 0x46, 0xC4, 0x6C,
    0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE,
    0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE,
    0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE,
    0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE,
    0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE,
    0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE,
    0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE,
    0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE,
    0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE,
    0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE,
};

static int ClampInt(int v, int lo, int hi)
{
    if(v < lo) return lo;
    if(v > hi) return hi;
    return v;
}

// Focus slots (L encoder cycles): 0 = split note, 1 = lane A, 2 = lane B,
// 3 = keytrack bar. Slot 0 edits the split with the R encoder; 1/2/3 open a
// subscreen on click.
static constexpr uint8_t kKzFocusSplit    = 0u;
static constexpr uint8_t kKzFocusLaneA    = 1u;
static constexpr uint8_t kKzFocusLaneB    = 2u;
static constexpr uint8_t kKzFocusKeytrack = 3u;
static constexpr uint8_t kKzFocusCount    = 4u;

// A|B split boundary range. The split note S is the first note of lane B; lane
// A covers everything below it. S-1 (top of A) must stay >= C1, S <= C8.
static constexpr int kKzSplitLo      = 25;  // S-1 == C1 (24)
static constexpr int kKzSplitHi      = 108; // C8
static constexpr int kKzSplitDefault = 60;  // C4

// The two lanes are automatic pitch keyzones: lane A = "<note" (below the
// split), lane B = ">note" (at/above the split, threshold = S-1 so note==S
// counts). Both gate (uniform amount across the zone). The split note lives in
// lane A's threshold, so it persists with the velmod state (no new field).
static void KeyzoneApplySplit(AppEngineState& e, int split)
{
    split = ClampInt(split, kKzSplitLo, kKzSplitHi);
    e.velmod.source[0]    = 3u;                              // <note
    e.velmod.threshold[0] = static_cast<uint8_t>(split);     // A covers note < S
    e.velmod.source[1]    = 2u;                              // >note
    e.velmod.threshold[1] = static_cast<uint8_t>(split - 1); // B covers note >= S
    e.velmod.shape[0]     = 1u;                              // gate
    e.velmod.shape[1]     = 1u;
}

static int KeyzoneSplitNote(const AppEngineState& e)
{
    return static_cast<int>(e.velmod.threshold[0]);
}

static bool PerformKeyzone_TryPushSubscreen(UiScreenCtx& ctx)
{
    if(!ctx.ui)
        return false;
    AppUiState& ui = *ctx.ui;
    if(ui.perform_keyzone_focus == kKzFocusLaneA)
        return UiNav_Push(ui.ui_nav, UiScreenId::VelocityMod);
    if(ui.perform_keyzone_focus == kKzFocusLaneB)
        return UiNav_Push(ui.ui_nav, UiScreenId::VelocityMod2);
    if(ui.perform_keyzone_focus == kKzFocusKeytrack)
        return UiNav_Push(ui.ui_nav, UiScreenId::PerformKeytrack);
    return false; // split slot has no subscreen (R encoder edits it)
}

// Definition is intentionally non-static: declared in ui_screens_internal.h so
// other screens (e.g. the Bake screen) can call it without duplicating the
// note-name table.
void FormatMidiNoteName(uint8_t note, char* out, size_t out_n)
{
    if(!out || out_n == 0)
        return;
    static const char* kNames[12] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B",
    };
    const int midi_note = static_cast<int>(note);
    const int pitch     = ((midi_note % 12) + 12) % 12;
    const int octave    = (midi_note / 12) - 1;
    std::snprintf(out, out_n, "%s%d", kNames[pitch], octave);
}

// Screen-enter: make lanes A/B behave as automatic pitch keyzones. Migrate any
// legacy velocity-domain lanes to a C4 split on first entry, and keep the focus
// slot in range.
void PerformKeyzone_OnScreenEnter(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.engine)
        return;
    AppEngineState& engine = *ctx.engine;
    int split = KeyzoneSplitNote(engine);
    if(engine.velmod.source[0] < 2u || split < kKzSplitLo || split > kKzSplitHi)
        split = kKzSplitDefault; // fresh / legacy state -> default C4 split
    KeyzoneApplySplit(engine, split);
    PublishEngineLayerParams(ctx);
    if(ctx.ui->perform_keyzone_focus >= kKzFocusCount)
        ctx.ui->perform_keyzone_focus = kKzFocusSplit;
}

// External-encoder click (on_enter slot): enter the focused subscreen.
bool PerformKeyzone_OnEnter(UiScreenCtx& ctx)
{
    if(PerformKeyzone_TryPushSubscreen(ctx))
    {
        ctx.ui->ui_dirty = true;
        return true;
    }
    return false;
}

bool PerformKeyzone_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.ui || !ctx.engine)
        return false;
    if(ctx.lshift)
        return false;

    AppUiState&     ui     = *ctx.ui;
    AppEngineState& engine = *ctx.engine;

    // L encoder cycles focus: split -> lane A -> lane B -> keytrack.
    if(e.type == UiInputType::EncDelta && e.id == kUiEncPod && e.value != 0)
    {
        if(e.value > 0)
            ui.perform_keyzone_focus = (ui.perform_keyzone_focus + 1u) % kKzFocusCount;
        else
            ui.perform_keyzone_focus =
                (ui.perform_keyzone_focus + kKzFocusCount - 1u) % kKzFocusCount;
        ui.ui_dirty = true;
        return true;
    }

    // R encoder moves the A|B split whenever the split slot OR either lane (a/b)
    // is focused — lanes still open their subscreen on the encoder *click*.
    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt && e.value != 0
       && ui.perform_keyzone_focus <= kKzFocusLaneB)
    {
        KeyzoneApplySplit(engine, KeyzoneSplitNote(engine) + (e.value > 0 ? 1 : -1));
        PublishEngineLayerParams(ctx);
        ui.ui_dirty = true;
        return true;
    }

    // External-encoder click enters the focused slot's subscreen.
    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        if(PerformKeyzone_TryPushSubscreen(ctx))
        {
            ui.ui_dirty = true;
            return true;
        }
    }

    return false;
}

void PerformKeyzone_Render(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.display)
        return;

    AppUiState&     ui     = *ctx.ui;
    AppEngineState& engine = *ctx.engine;
    EngineRefreshLoadedMetadata(*ctx.ui, *ctx.engine, *ctx.shared);

    OledPager& d = *ctx.display;
    d.Fill(false);

    static const char header_label[] = "keyzone";
    const int header_w = MicroStringWidth(header_label);
    const int box_w    = header_w + 4;
    const int box_h    = kMicroH + 4;
    int box_x = 128 - box_w;
    if(box_x < 0)
        box_x = 0;
    const bool header_invert_flash
        = static_cast<int32_t>(engine.layer.engine_header_invert_until_ms - ctx.now_ms) > 0;
    if(header_invert_flash)
    {
        d.DrawRect(box_x, 0, box_x + box_w - 1, box_h - 1, false, true);
        d.DrawRect(box_x, 0, box_x + box_w - 1, box_h - 1, true,  false);
        DrawMicroString(d, header_label, box_x + 2, 2, true);
    }
    else
    {
        d.DrawRect(box_x, 0, box_x + box_w - 1, box_h - 1, true, true);
        DrawMicroString(d, header_label, box_x + 2, 2, false);
    }

    // Layout (top→bottom): split note control, A|B band split at the key, keybed
    // icon, then the full-width keytrack bar pinned to the bottom.
    constexpr int kBandY0  = 16;
    constexpr int kBandY1  = 29;            // ~14 px tall (shortened lanes)
    constexpr int kKbdH    = 13;
    constexpr int kKbdY0   = 32;            // keybed just below the A|B band
    constexpr int kKtY0    = 48;            // keytrack bar
    constexpr int kKtY1    = 62;

    // Map a MIDI note to its x on the keybed (shared by the split divider and the
    // played-note flash so they line up).
    auto keyzone_left_x = [&](int midi_note)
    {
        static constexpr int kKeyzoneLoMidi = 36; // C2
        static constexpr int kKeyzoneHiMidi = 90; // F#6
        const int note   = ClampInt(midi_note, kKeyzoneLoMidi, kKeyzoneHiMidi);
        const int span   = kKeyzoneHiMidi - kKeyzoneLoMidi;
        const int offset = note - kKeyzoneLoMidi;
        return (offset * 127 + (span / 2)) / span;
    };

    // 3-char tag per velmod target index (0 = "----" → blank → lane letter).
    static const char* const kTargetTag[8]
        = {"", "vol", "att", "sus", "rel", "rev", "dly", "sat"};

    const int split = KeyzoneSplitNote(engine);

    // ── Split note control (focus 0): "B3;C4" with a single focus border. ──
    {
        char a_name[6] = {}, b_name[6] = {}, split_str[14] = {};
        FormatMidiNoteName(static_cast<uint8_t>(split - 1), a_name, sizeof(a_name));
        FormatMidiNoteName(static_cast<uint8_t>(split), b_name, sizeof(b_name));
        std::snprintf(split_str, sizeof(split_str), "%s;%s", a_name, b_name);
        const int sx = 3;
        const int sy = 2;
        const int sw = TinyStringWidth(split_str);
        DrawTinyStringCaseSensitive(d, split_str, sx, sy, true);
        if(ui.perform_keyzone_focus == kKzFocusSplit)
            d.DrawRect(sx - 2, sy - 2, sx + sw + 1, sy + Font5x7::H + 1, true, false);
    }

    // ── A|B band split at the key (focus 1 = a on the left, 2 = b on the right). ──
    const int split_x = keyzone_left_x(split);
    auto draw_zone = [&](int x0, int x1, int lane, bool focused)
    {
        if(x1 < x0)
            x1 = x0;
        d.DrawRect(x0, kBandY0, x1, kBandY1, true, false); // hollow zone box

        const uint8_t target = engine.velmod.target_idx[lane];
        const char*   tag    = kTargetTag[target & 7u];
        const char*   label  = (target != 0u && tag[0] != '\0') ? tag
                                                                : (lane == 0 ? "a" : "b");
        const int lw = MicroStringWidth(label);
        const int tx = ClampInt(x0 + 3, 1, 127 - lw);
        const int ty = (kBandY0 + kBandY1) / 2 - (kMicroH / 2);
        if(focused)
        {
            DrawRencFocusFrame(d, tx, ty, lw, kMicroH);
            DrawMicroString(d, label, tx, ty, false);
        }
        else
        {
            d.DrawRect(tx - 1, ty - 1, tx + lw, ty + kMicroH, false, true);
            DrawMicroString(d, label, tx, ty, true);
        }
    };
    draw_zone(0, split_x - 1, 0, ui.perform_keyzone_focus == kKzFocusLaneA);
    draw_zone(split_x, 127, 1, ui.perform_keyzone_focus == kKzFocusLaneB);

    // ── Keybed icon + played-note flash (no divider drawn over the keybed). ──
    DrawBitmap1bpp(d, 0, kKbdY0, 128, kKbdH, 16, kPerformKeyzoneKeyboard128x16, true);

    if(static_cast<int32_t>(ui.keyzone_flash_until_ms - ctx.now_ms) > 0
       && ((ctx.now_ms / 167u) & 1u) == 0u)
    {
        const int nx  = keyzone_left_x(ui.keyzone_flash_note);
        const int mx0 = ClampInt(nx - 1, 0, 127);
        const int mx1 = ClampInt(nx + 1, 0, 127);
        d.DrawRect(mx0, kKbdY0, mx1, kKbdY0 + kKbdH - 1, true, true);
    }

    // ── Full-width keytrack bar (focus 3): opens the keytrack subscreen. ──
    {
        const bool  focused = (ui.perform_keyzone_focus == kKzFocusKeytrack);
        const char* label   = "keytrack";
        const int   lw      = MicroStringWidth(label);
        const int   lx      = (128 - lw) / 2;
        const int   ly      = (kKtY0 + kKtY1) / 2 - (kMicroH / 2);
        d.DrawRect(0, kKtY0, 127, kKtY1, focused, true);   // filled when focused
        d.DrawRect(0, kKtY0, 127, kKtY1, true, false);      // border always
        DrawMicroString(d, label, lx, ly, !focused);        // invert text when filled
    }
}
