#include "ui_screens_internal.h"

#include <cstdio>

#include "app_state_ui.h"
#include "app_state_engine.h"
#include "app_state_recording.h"
#include "app_state_project.h"
#include "app_state_diagnostics.h"
#include "app_state_shared.h"
#include "app_state_worker.h"
#include "oled_pager.h"
#include "ui_input.h"

static constexpr uint8_t kPerformLayerCount = 2;

static int ClampInt(int v, int lo, int hi)
{
    if(v < lo)
        return lo;
    if(v > hi)
        return hi;
    return v;
}

static void FormatMidiNoteName(uint8_t note, char* out, size_t out_n)
{
    if(!out || out_n == 0)
        return;

    static const char* kNames[12] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B",
    };
    const int midi_note = static_cast<int>(note);
    const int pitch = ((midi_note % 12) + 12) % 12;
    const int octave = (midi_note / 12) - 1;
    std::snprintf(out, out_n, "%s%d", kNames[pitch], octave);
}

static int KeyzonePageFromNote(uint8_t note)
{
    // Pages are C0-B0 .. C8-B8, clamped to the supported note range.
    const int octave = (static_cast<int>(note) / 12) - 1;
    return ClampInt(octave, 0, 8);
}

static void DrawKeyzoneUiRangeTemplate(OledPager& d,
                                       AppEngineState& engine,
                                       uint8_t layer,
                                       bool highlight_both,
                                       int x0,
                                       int y0)
{
    constexpr int kAreaW = 128;
    constexpr int kVisibleOctaves = 3;
    constexpr int kPageCount = 9; // C0-B0 .. C8-B8
    constexpr int kWhiteKeyCount = 7 * kVisibleOctaves;
    // Geometry matched to the provided reference image: wide 3-octave span,
    // shallow white keys, and ~60% black-key height.
    constexpr int kWhiteW = 6;
    constexpr int kWhiteH = 28;
    constexpr int kBlackW = 3;
    constexpr int kBlackH = 17;

    const int total_white_w = kWhiteKeyCount * kWhiteW;
    const int kb_x0 = x0 + ((kAreaW - total_white_w) / 2);
    // Use upper available space and preserve blank space below for future UI.
    const int kb_y0 = y0 + 2;
    const int kb_x1 = kb_x0 + total_white_w - 1;
    const int kb_y1 = kb_y0 + kWhiteH - 1;

    // Real-keyboard contrast on monochrome OLED: white bed + black key cutouts.
    d.DrawRect(kb_x0, kb_y0, kb_x1, kb_y1, true, true);
    for(int i = 1; i < kWhiteKeyCount; ++i)
    {
        const int x = kb_x0 + (i * kWhiteW);
        d.DrawLine(x, kb_y0 + 1, x, kb_y1 - 1, false);
    }

    // Black keys above C,D,F,G,A for each visible octave.
    const int black_after_white_idx[5] = {0, 1, 3, 4, 5};
    for(int oct = 0; oct < kVisibleOctaves; ++oct)
    {
        for(int i = 0; i < 5; ++i)
        {
            const int after_white = (oct * 7) + black_after_white_idx[i];
            const int center_x = kb_x0 + ((after_white + 1) * kWhiteW);
            const int bx0 = center_x - (kBlackW / 2);
            const int bx1 = bx0 + kBlackW - 1;
            const int by0 = kb_y0;
            const int by1 = kb_y0 + kBlackH - 1;
            d.DrawRect(bx0, by0, bx1, by1, false, true);
        }
    }

    const uint8_t lo_note = engine.perform_keyzone_lo_note[layer & 1u];
    const uint8_t hi_note = engine.perform_keyzone_hi_note[layer & 1u];
    const uint8_t focused_marker = engine.perform_keyzone_marker_focus & 1u; // 0=LO, 1=HI
    const uint8_t focus_note = (focused_marker == 0u) ? lo_note : hi_note;
    int window_octave = static_cast<int>(engine.perform_keyzone_window_octave[layer & 1u]);
    window_octave = ClampInt(window_octave, 0, kPageCount - kVisibleOctaves);
    int page_start = 12 * (window_octave + 1); // Cn MIDI note
    int page_end = page_start + (12 * kVisibleOctaves) - 1;
    const int focus_n = static_cast<int>(focus_note);
    if(focus_n < page_start)
    {
        window_octave = KeyzonePageFromNote(focus_note);
        window_octave = ClampInt(window_octave, 0, kPageCount - kVisibleOctaves);
        page_start = 12 * (window_octave + 1);
        page_end = page_start + (12 * kVisibleOctaves) - 1;
    }
    else if(focus_n > page_end)
    {
        window_octave = KeyzonePageFromNote(focus_note) - (kVisibleOctaves - 1);
        window_octave = ClampInt(window_octave, 0, kPageCount - kVisibleOctaves);
        page_start = 12 * (window_octave + 1);
        page_end = page_start + (12 * kVisibleOctaves) - 1;
    }
    engine.perform_keyzone_window_octave[layer & 1u] = static_cast<uint8_t>(window_octave);
    auto white_idx_from_semitone_in_oct = [](int semi_in_oct) -> int
    {
        switch(semi_in_oct)
        {
            case 0: return 0;
            case 2: return 1;
            case 4: return 2;
            case 5: return 3;
            case 7: return 4;
            case 9: return 5;
            case 11: return 6;
            default: return -1;
        }
    };
    auto black_after_white_from_semitone = [](int semi_in_oct) -> int
    {
        switch(semi_in_oct)
        {
            case 1: return 0;
            case 3: return 1;
            case 6: return 3;
            case 8: return 4;
            case 10: return 5;
            default: return -1;
        }
    };
    auto is_black = [](int semi_in_oct) -> bool
    {
        return semi_in_oct == 1 || semi_in_oct == 3 || semi_in_oct == 6 || semi_in_oct == 8
               || semi_in_oct == 10;
    };
    auto semitone_in_page = [&](uint8_t note) -> int
    {
        int semitone = static_cast<int>(note) - page_start;
        if(semitone < 0)
            semitone = 0;
        if(semitone > (12 * kVisibleOctaves) - 1)
            semitone = (12 * kVisibleOctaves) - 1;
        return semitone;
    };
    auto note_in_window = [&](uint8_t note) -> bool
    {
        const int n = static_cast<int>(note);
        return n >= page_start && n <= page_end;
    };
    auto key_rect_for_note = [&](uint8_t note, int& rx0, int& ry0, int& rx1, int& ry1)
    {
        const int semitone = semitone_in_page(note);
        const int oct = semitone / 12;
        const int semi_in_oct = semitone % 12;
        if(is_black(semi_in_oct))
        {
            const int after_white = black_after_white_from_semitone(semi_in_oct) + (oct * 7);
            const int center_x = kb_x0 + ((after_white + 1) * kWhiteW);
            rx0 = center_x - (kBlackW / 2);
            rx1 = rx0 + kBlackW - 1;
            ry0 = kb_y0;
            ry1 = kb_y0 + kBlackH - 1;
            return;
        }
        const int white_idx = white_idx_from_semitone_in_oct(semi_in_oct) + (oct * 7);
        rx0 = kb_x0 + (white_idx * kWhiteW);
        rx1 = rx0 + kWhiteW - 1;
        ry0 = kb_y0;
        ry1 = kb_y1;
    };
    auto draw_white_arrow_marker = [&](int white_idx, bool left_arrow)
    {
        const int x0 = kb_x0 + (white_idx * kWhiteW);
        const int x1 = x0 + kWhiteW - 1;
        const int y_cut = kb_y0 + kBlackH - 1;
        if(x1 - x0 + 1 < 5)
            return;

        const int marker_h = 6;
        const int marker_y0 = y_cut + 1 + ((kb_y1 - (y_cut + 1) + 1 - marker_h) / 2);
        const int marker_y1 = marker_y0 + marker_h - 1;
        if(marker_y0 < y_cut + 1 || marker_y1 > kb_y1)
            return;

        // Keep the arrow centered in the white key for both LO and HI markers.
        const int cx = (x0 + x1) / 2;
        constexpr int kArrowShiftRight = 1;
        if(left_arrow)
        {
            const int stem_x = ClampInt(cx + 1 + kArrowShiftRight, x0 + 3, x1 - 1);
            d.DrawRect(stem_x, marker_y0, stem_x, marker_y1, false, true); // 1x6 stem
            d.DrawRect(stem_x - 1, marker_y0 + 1, stem_x - 1, marker_y1 - 1, false, true); // 1x4
            d.DrawRect(stem_x - 2, marker_y0 + 2, stem_x - 2, marker_y1 - 2, false, true); // 1x2
        }
        else
        {
            const int stem_x = ClampInt(cx - 1 + kArrowShiftRight, x0 + 1, x1 - 3);
            d.DrawRect(stem_x, marker_y0, stem_x, marker_y1, false, true); // 1x6 stem
            d.DrawRect(stem_x + 1, marker_y0 + 1, stem_x + 1, marker_y1 - 1, false, true); // 1x4
            d.DrawRect(stem_x + 2, marker_y0 + 2, stem_x + 2, marker_y1 - 2, false, true); // 1x2
        }
    };

    auto highlight_note = [&](uint8_t note, bool is_lo_endpoint)
    {
        const int active_semitone = semitone_in_page(note);
        const int oct = active_semitone / 12;
        const int semi_in_oct = active_semitone % 12;
        if(is_black(semi_in_oct))
        {
            int arx0 = 0, ary0 = 0, arx1 = 0, ary1 = 0;
            key_rect_for_note(note, arx0, ary0, arx1, ary1);
            // Selected black key: inversion only (no dotted barrier).
            d.DrawRect(arx0, ary0, arx1, ary1, true, true);
            d.DrawRect(arx0, ary0, arx1, ary1, false, false);
        }
        else
        {
            const int white_idx = white_idx_from_semitone_in_oct(semi_in_oct) + (oct * 7);
            // White-key selection: directional marker only.
            draw_white_arrow_marker(white_idx, is_lo_endpoint);
        }
    };

    const bool show_lo = note_in_window(lo_note);
    const bool show_hi = note_in_window(hi_note);
    if(show_lo)
        highlight_note(lo_note, true);
    if(show_hi && (!show_lo || hi_note != lo_note))
        highlight_note(hi_note, false);

    if(highlight_both && !show_lo && !show_hi)
    {
        // In linked-shift mode, ensure some visual feedback by drawing focused endpoint if both are off-window.
        highlight_note(focus_note, focused_marker == 0u);
    }
}

bool PerformKeyzone_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.ui)
        return false;
    if(ctx.lshift)
        return false;

    AppUiState& ui = *ctx.ui;
    AppEngineState& engine = *ctx.engine;
    AppRecordingState& recording = *ctx.recording;
    AppProjectState& project = *ctx.project;
    AppDiagnosticsState& diag = *ctx.diag;
    AppSharedState& shared = *ctx.shared;
    AppWorkerState& worker = *ctx.worker;

    // POD2 toggles layer and keeps focus on the BACK (start) marker.
    if(e.type == UiInputType::BtnDown && e.id == kUiBtnPod2)
    {
        engine.perform_layer ^= 1u;
        const uint8_t layer = engine.perform_layer & 1u;
        engine.perform_keyzone_marker_focus = static_cast<uint8_t>(layer * 2u);
        shared.sample.sd_current_slot.store(layer, std::memory_order_release);
        engine.engine_header_invert_until_ms = e.t_ms + 250u;
        PublishEngineLayerParams(ctx);
        ui.ui_dirty = true;
        return true;
    }

    if(ctx.rshift && e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        const bool full_a = engine.perform_keyzone_lo_note[0] == kPerformKeyzoneMinNote
                            && engine.perform_keyzone_hi_note[0] == kPerformKeyzoneMaxNote;
        const bool full_b = engine.perform_keyzone_lo_note[1] == kPerformKeyzoneMinNote
                            && engine.perform_keyzone_hi_note[1] == kPerformKeyzoneMaxNote;
        if(full_a && full_b)
        {
            engine.perform_keyzone_lo_note[0] = kPerformKeyzoneMinNote;
            engine.perform_keyzone_hi_note[0] = kPerformKeyzoneDefaultSplitAHi;
            engine.perform_keyzone_lo_note[1] = kPerformKeyzoneDefaultSplitBLo;
            engine.perform_keyzone_hi_note[1] = kPerformKeyzoneMaxNote;
        }
        else
        {
            for(uint8_t i = 0; i < kPerformLayerCount; ++i)
            {
                engine.perform_keyzone_lo_note[i] = kPerformKeyzoneMinNote;
                engine.perform_keyzone_hi_note[i] = kPerformKeyzoneMaxNote;
            }
        }
        PublishEngineLayerParams(ctx);
        ui.ui_dirty = true;
        return true;
    }

    if(e.type == UiInputType::EncDelta && e.value != 0)
    {
        const uint8_t layer = engine.perform_layer & 1u;
        if(ctx.rshift)
        {
            if(e.id != kUiEncPod && e.id != kUiEncExt)
                return false;
            const int dir = (e.value < 0) ? -1 : 1;
            const int lo = static_cast<int>(engine.perform_keyzone_lo_note[layer]);
            const int hi = static_cast<int>(engine.perform_keyzone_hi_note[layer]);
            const int delta_min = static_cast<int>(kPerformKeyzoneMinNote) - lo;
            const int delta_max = static_cast<int>(kPerformKeyzoneMaxNote) - hi;
            const int applied = ClampInt(dir, delta_min, delta_max);
            if(applied == 0)
                return false;
            engine.perform_keyzone_lo_note[layer] = static_cast<uint8_t>(lo + applied);
            engine.perform_keyzone_hi_note[layer] = static_cast<uint8_t>(hi + applied);
            PublishEngineLayerParams(ctx);
            ui.ui_dirty = true;
            return true;
        }

        if(e.id == kUiEncPod)
        {
            const int next_lo = ClampInt(static_cast<int>(engine.perform_keyzone_lo_note[layer]) + e.value,
                                         static_cast<int>(kPerformKeyzoneMinNote),
                                         static_cast<int>(engine.perform_keyzone_hi_note[layer]));
            if(next_lo == static_cast<int>(engine.perform_keyzone_lo_note[layer]))
                return false;
            engine.perform_keyzone_lo_note[layer] = static_cast<uint8_t>(next_lo);
            engine.perform_keyzone_marker_focus = static_cast<uint8_t>(layer * 2u); // back marker
            ui.ui_dirty = true;
            PublishEngineLayerParams(ctx);
            return true;
        }

        if(e.id == kUiEncExt)
        {
            const int next_hi = ClampInt(static_cast<int>(engine.perform_keyzone_hi_note[layer]) + e.value,
                                         static_cast<int>(engine.perform_keyzone_lo_note[layer]),
                                         static_cast<int>(kPerformKeyzoneMaxNote));
            if(next_hi == static_cast<int>(engine.perform_keyzone_hi_note[layer]))
                return false;
            engine.perform_keyzone_hi_note[layer] = static_cast<uint8_t>(next_hi);
            engine.perform_keyzone_marker_focus = static_cast<uint8_t>((layer * 2u) + 1u); // forward marker
            PublishEngineLayerParams(ctx);
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

    AppUiState& ui = *ctx.ui;
    AppEngineState& engine = *ctx.engine;
    AppRecordingState& recording = *ctx.recording;
    AppProjectState& project = *ctx.project;
    AppDiagnosticsState& diag = *ctx.diag;
    AppSharedState& shared = *ctx.shared;
    AppWorkerState& worker = *ctx.worker;
    EngineRefreshLoadedMetadata(ui, engine, shared);

    OledPager& d = *ctx.display;
    d.Fill(false);

    const uint8_t layer = engine.perform_layer & 1u;

    char header_label[16] = {};
    std::snprintf(header_label, sizeof(header_label), "kyzn %c", layer == 0 ? 'a' : 'b');
    const int header_w = MicroStringWidth(header_label);
    const int box_w = header_w + 4;
    const int box_h = kMicroH + 4;
    int box_x = 128 - box_w;
    if(box_x < 0)
        box_x = 0;
    const bool header_invert_flash
        = static_cast<int32_t>(engine.engine_header_invert_until_ms - ctx.now_ms) > 0;
    if(header_invert_flash)
    {
        d.DrawRect(box_x, 0, box_x + box_w - 1, box_h - 1, false, true);
        d.DrawRect(box_x, 0, box_x + box_w - 1, box_h - 1, true, false);
        DrawMicroString(d, header_label, box_x + 2, 2, true);
    }
    else
    {
        d.DrawRect(box_x, 0, box_x + box_w - 1, box_h - 1, true, true);
        DrawMicroString(d, header_label, box_x + 2, 2, false);
    }

    constexpr int kSectionH = 16;
    char lo_note[8] = {};
    char hi_note[8] = {};
    char lo_text[16] = {};
    char hi_text[16] = {};
    FormatMidiNoteName(engine.perform_keyzone_lo_note[layer], lo_note, sizeof(lo_note));
    FormatMidiNoteName(engine.perform_keyzone_hi_note[layer], hi_note, sizeof(hi_note));
    std::snprintf(lo_text, sizeof(lo_text), "Lo:%s", lo_note);
    std::snprintf(hi_text, sizeof(hi_text), "Hi:%s", hi_note);

    auto draw_animated_dotted_box = [&](int x0, int y0, int x1, int y1)
    {
        const int phase = static_cast<int>((ctx.now_ms / 100u) & 1u);
        for(int x = x0; x <= x1; ++x)
        {
            if(((x + phase) & 1) == 0)
            {
                d.DrawPixel(x, y0, true);
                d.DrawPixel(x, y1, true);
            }
        }
        for(int y = y0; y <= y1; ++y)
        {
            if(((y + phase) & 1) == 0)
            {
                d.DrawPixel(x0, y, true);
                d.DrawPixel(x1, y, true);
            }
        }
    };

    const int status_y = (kSectionH - Font5x7::H) / 2;
    const int lo_w = TinyStringWidthCaseSensitiveTightColons(lo_text);
    const int hi_w = TinyStringWidthCaseSensitiveTightColons(hi_text);
    const int lo_x = 2;
    const int gap_w = 8;
    const int hi_x = lo_x + lo_w + gap_w;
    if(hi_x + hi_w <= box_x - 2)
    {
        const int box_pad_x = 2;
        const int box_top = status_y - 3;
        const int box_bottom = status_y + Font5x7::H + 1;
        const int lo_box_x0 = lo_x - box_pad_x;
        const int lo_box_x1 = lo_x + lo_w + box_pad_x;
        const int hi_box_x0 = hi_x - box_pad_x;
        const int hi_box_x1 = hi_x + hi_w + box_pad_x;
        if(ctx.rshift)
        {
            d.DrawRect(lo_box_x0 + 1, box_top + 1, lo_box_x1 - 1, box_bottom - 1, true, true);
            d.DrawRect(hi_box_x0 + 1, box_top + 1, hi_box_x1 - 1, box_bottom - 1, true, true);
            DrawTinyStringCaseSensitive(d, lo_text, lo_x, status_y, false);
            DrawTinyStringCaseSensitive(d, hi_text, hi_x, status_y, false);
        }
        else
        {
            DrawTinyStringCaseSensitive(d, lo_text, lo_x, status_y, true);
            DrawTinyStringCaseSensitive(d, hi_text, hi_x, status_y, true);
        }
        draw_animated_dotted_box(lo_box_x0, box_top, lo_box_x1, box_bottom);
        draw_animated_dotted_box(hi_box_x0, box_top, hi_box_x1, box_bottom);
    }

    DrawKeyzoneUiRangeTemplate(d, engine, layer, ctx.rshift, 0, kSectionH);
}
