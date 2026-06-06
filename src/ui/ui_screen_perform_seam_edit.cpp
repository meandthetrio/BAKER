#include "ui_screens_internal.h"
#include "ui_screen_perform_adsr_internal.h"

#include "app_state_ui.h"
#include "app_state_engine.h"
#include "app_state_shared.h"
#include "oled_pager.h"
#include "ui_draw_text.h"
#include "ui_input.h"
#include "sample_edit.h"
#include "sd_sample_pool.h"
#include "sample_bake.h"

// Dedicated loop-seam edit screen. Reached via REnc click on the focused ADSR wav
// preview. It replicates the old in-place seam adjustment (REnc = length, rshift +
// REnc = shape) but in isolation: MIDI is forced monophonic to the edited layer and
// the played sample is temporarily switched back to the RAW buffer so the live
// runtime crossfade auditions the seam in real time. REnc click commits: the new
// seam is baked into the layer's baked buffer and the user returns to ADSR.

namespace
{
constexpr float kSeamCrossfadeMin = 0.0f;
constexpr float kSeamCrossfadeMax = 0.5f;
constexpr float kSeamCrossfadeStep = 1.0f / 128.0f;
constexpr float kSeamCrossfadeShapeMin = 0.0f;
constexpr float kSeamCrossfadeShapeMax = 1.0f;
constexpr float kSeamCrossfadeShapeStep = 1.0f / 64.0f;

uint8_t SeamLayer(const AppEngineState& engine)
{
    return engine.perform_nav.perform_layer & 1u;
}
} // namespace

void PerformSeamEdit_OnScreenEnter(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.engine || !ctx.shared)
        return;
    AppUiState& ui = *ctx.ui;
    AppEngineState& engine = *ctx.engine;
    AppSharedState& shared = *ctx.shared;
    const uint8_t layer = SeamLayer(engine);

    // Switch the played sample back to RAW + disable the runtime seam suppression so
    // the live crossfade responds to encoder changes in real time. Only swap the
    // pointer when this layer is currently playing its baked copy (the raw load lives
    // in SdSampleBuffer for SD-loaded samples); for any other source leave pcm as-is.
    if(engine.adsr.perform_adsr_loop_seam_baked[layer])
        shared.sample.publish.sd_slots[layer].pcm = SdSampleBuffer(layer);
    engine.adsr.perform_adsr_loop_seam_baked[layer] = false;

    // Force monophonic audition of this layer; silence anything currently sounding.
    ui.ui_seam_audition_active = true;
    ui.ui_seam_audition_layer = layer;
    ui.ui_seam_silence_pending = true;
    ui.ui_dirty = true;
}

bool PerformSeamEdit_OnEvent(UiScreenCtx& ctx, const UiInputEvent& e)
{
    if(!ctx.ui || !ctx.engine || !ctx.shared)
        return false;
    if(ctx.shift)
        return false;

    AppUiState& ui = *ctx.ui;
    AppEngineState& engine = *ctx.engine;
    AppSharedState& shared = *ctx.shared;
    const uint8_t layer = SeamLayer(engine);

    // REnc click = commit: re-bake the new seam into the baked buffer and return.
    if(e.type == UiInputType::BtnDown && e.id == kUiBtnExtEnc)
    {
        const Sample& s = shared.sample.publish.sd_slots[layer];
        // Only re-bake when this layer is backed by the SD raw buffer (the source
        // the bake reads from). Other sources just exit without re-baking.
        if(s.pcm != nullptr && s.length > 0u && s.pcm == SdSampleBuffer(layer))
        {
            SampleEdit edit = shared.sample.edit.sd_edit_slots[layer];
            SampleEdit_Clamp(edit, s.length);
            // Audio is currently playing the RAW buffer (set on enter), so writing
            // the baked buffer here on the main thread is safe.
            BakeLoopSeamToBuffer(SdSampleBuffer(layer),
                                 s.length,
                                 edit.start_frame,
                                 edit.end_frame,
                                 engine.adsr.perform_adsr_loop_crossfade[layer],
                                 engine.adsr.perform_adsr_loop_crossfade_shape[layer],
                                 48000.0f,
                                 SdBakedBuffer(layer));
            engine.adsr.perform_adsr_loop_seam_baked[layer] = true;
            shared.sample.publish.sd_slots[layer].pcm = SdBakedBuffer(layer);
        }
        ui.ui_seam_audition_active = false;
        ui.ui_seam_silence_pending = true;
        UiNav_Pop(ui.ui_nav);
        ui.ui_dirty = true;
        return true;
    }

    // REnc rotate = seam length; rshift + REnc rotate = curve shape. (Copied from
    // the old ADSR in-place seam adjustment.)
    if(e.type == UiInputType::EncDelta && e.id == kUiEncExt && e.value != 0)
    {
        float& target = ctx.rshift ? engine.adsr.perform_adsr_loop_crossfade_shape[layer]
                                   : engine.adsr.perform_adsr_loop_crossfade[layer];
        const float step = ctx.rshift ? kSeamCrossfadeShapeStep : kSeamCrossfadeStep;
        const float min_value = ctx.rshift ? kSeamCrossfadeShapeMin : kSeamCrossfadeMin;
        const float max_value = ctx.rshift ? kSeamCrossfadeShapeMax : kSeamCrossfadeMax;
        float next = target + (static_cast<float>(e.value) * step);
        if(next < min_value)
            next = min_value;
        if(next > max_value)
            next = max_value;
        if(next == target)
            return true;
        target = next;
        PublishEngineLayerParams(ctx);
        ui.ui_dirty = true;
        return true;
    }

    return false;
}

void PerformSeamEdit_Render(UiScreenCtx& ctx)
{
    if(!ctx.ui || !ctx.display || !ctx.engine || !ctx.shared)
        return;

    OledPager& d = *ctx.display;
    d.Fill(false);

    AppEngineState& engine = *ctx.engine;
    AppSharedState& shared = *ctx.shared;
    const uint8_t layer = SeamLayer(engine);
    const Sample& sample = shared.sample.publish.sd_slots[layer];
    const bool sample_loaded = (sample.pcm != nullptr && sample.length > 0);
    const SampleEdit* edit = sample_loaded ? &shared.sample.edit.sd_edit_slots[layer] : nullptr;

    // Inverted "seams" micro label, top-right (mirrors the ADSR header style).
    const char* kLabel = "seams";
    const int header_w = MicroStringWidth(kLabel);
    const int box_w = header_w + 4;
    const int box_h = kMicroH + 4;
    int box_x = 128 - box_w;
    if(box_x < 0)
        box_x = 0;
    d.DrawRect(box_x, 0, box_x + box_w - 1, box_h - 1, true, true);
    DrawMicroString(d, kLabel, box_x + 2, 2, false);

    // Wav preview box, inset 3 px on every side (matches the ADSR screen).
    constexpr int kWaveX = 3;
    constexpr int kWaveY = 13;
    constexpr int kWaveW = 122;
    int kWaveBottomY = static_cast<int>(d.Height()) - 11;
    if(kWaveBottomY < kWaveY)
        kWaveBottomY = kWaveY;
    const int kWaveH = kWaveBottomY - kWaveY + 1;

    // Inverse-video preview box: fill lit, draw waveform + border dark.
    d.DrawRect(kWaveX, kWaveY, kWaveX + kWaveW - 1, kWaveY + kWaveH - 1, true, true);
    DrawWaveformPreview(d, sample, edit, kWaveX, kWaveY, kWaveW, kWaveH, false, false, false, true);

    if(sample_loaded)
    {
        const int preview_x0 = kWaveX + 1;
        const int preview_x1 = kWaveX + kWaveW - 2;
        const int preview_y0 = kWaveY + 1;
        const int preview_y1 = kWaveBottomY - 1;
        PerformAdsr_DrawSeamOverlay(d,
                                    preview_x0,
                                    preview_y0,
                                    preview_x1,
                                    preview_y1,
                                    engine.adsr.perform_adsr_loop_crossfade[layer],
                                    engine.adsr.perform_adsr_loop_crossfade_shape[layer],
                                    /*invert=*/true,
                                    /*show_curve=*/ctx.rshift);
    }
}
