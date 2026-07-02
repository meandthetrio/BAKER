#include "voice_engine_render_internal.h"

#include <cmath>

static float s_sqrt_lut[256] = {};

void VoiceRenderFetch_InitSqrtLut()
{
    for(int i = 0; i < 256; ++i)
        s_sqrt_lut[i] = std::sqrt(static_cast<float>(i) / 255.0f);
}

float SampleAtLinear(const Sample* s, uint32_t pos_frame, float pos_frac, bool wrap_end)
{
    if(s == nullptr || s->pcm == nullptr || s->length == 0)
        return 0.0f;
    if(pos_frame >= s->length)
        return 0.0f;
    const int16_t a = s->pcm[pos_frame];
    const int16_t b = (pos_frame + 1 < s->length) ? s->pcm[pos_frame + 1]
                                                  : (wrap_end ? s->pcm[0] : a);
    const float fa = static_cast<float>(a) * (1.0f / 32768.0f);
    const float fb = static_cast<float>(b) * (1.0f / 32768.0f);
    return fa + pos_frac * (fb - fa);
}

float SampleAtLinearRegion(const Sample* s,
                           uint32_t pos_frame,
                           float pos_frac,
                           uint32_t start,
                           uint32_t end,
                           bool loop_enabled,
                           uint32_t loop_start,
                           uint32_t loop_end)
{
    if(s == nullptr || s->pcm == nullptr || s->length == 0)
        return 0.0f;
    if(end <= start || end > s->length)
        end = s->length;
    if(pos_frame < start || pos_frame >= end)
        return 0.0f;
    const int16_t a = s->pcm[pos_frame];
    uint32_t next = pos_frame + 1;
    if(next >= end)
    {
        if(loop_enabled && loop_start < loop_end)
            next = loop_start;
        else
            next = pos_frame;
    }
    const int16_t b = s->pcm[next];
    const float fa = static_cast<float>(a) * (1.0f / 32768.0f);
    const float fb = static_cast<float>(b) * (1.0f / 32768.0f);
    return fa + pos_frac * (fb - fa);
}

uint32_t ComputeLoopSeamCrossfadeFrames(uint32_t start, uint32_t end, float amount)
{
    if(end <= start + 1)
        return 0;
    if(amount <= 0.0f)
        return 0;
    if(amount > 0.5f)
        amount = 0.5f;

    const uint32_t region_frames = end - start;
    const uint32_t max_frames = region_frames / 2;
    if(max_frames == 0)
        return 0;

    uint32_t frames = static_cast<uint32_t>((static_cast<float>(region_frames) * amount) + 0.5f);
    if(frames == 0)
        frames = 1;
    if(frames > max_frames)
        frames = max_frames;
    return frames;
}

float ComputeLoopSeamCrossfadeWeight(float mix, float shape, bool fade_in)
{
    if(mix < 0.0f)
        mix = 0.0f;
    if(mix > 1.0f)
        mix = 1.0f;
    if(shape < 0.0f)
        shape = 0.0f;
    if(shape > 1.0f)
        shape = 1.0f;

    const float linear = fade_in ? mix : (1.0f - mix);
    const float lut_in = fade_in ? mix : (1.0f - mix);
    const int lut_idx  = static_cast<int>(lut_in * 255.0f + 0.5f);
    const float equal_power = s_sqrt_lut[lut_idx < 255 ? lut_idx : 255];
    return linear + (equal_power - linear) * shape;
}

float SampleAtLoopSeamCrossfade(const Sample* s,
                                uint32_t pos_frame,
                                float pos_frac,
                                uint32_t start,
                                uint32_t end,
                                uint32_t seam_frames,
                                float shape,
                                float sample_rate,
                                bool& used_xfade)
{
    (void)sample_rate;
    used_xfade = false;
    if(seam_frames == 0 || end <= start + seam_frames)
        return SampleAtLinearRegion(s, pos_frame, pos_frac, start, end, true, start, end);

    const uint32_t seam_start = end - seam_frames;
    if(pos_frame < seam_start)
        return SampleAtLinearRegion(s, pos_frame, pos_frac, start, end, true, start, end);

    float mix
        = (static_cast<float>(pos_frame - seam_start) + pos_frac) / static_cast<float>(seam_frames);
    if(mix < 0.0f)
        mix = 0.0f;
    if(mix > 1.0f)
        mix = 1.0f;

    const uint32_t seam_pos_frame = start + (pos_frame - seam_start);
    const float tail = SampleAtLinearRegion(s, pos_frame, pos_frac, start, end, true, start, end);
    const float head
        = SampleAtLinearRegion(s, seam_pos_frame, pos_frac, start, end, true, start, end);
    const float tail_weight = ComputeLoopSeamCrossfadeWeight(mix, shape, false);
    const float head_weight = ComputeLoopSeamCrossfadeWeight(mix, shape, true);
    used_xfade = true;
    return tail * tail_weight + head * head_weight;
}

float VoiceRenderFetch_VoiceStream(const Sample* sample,
                                   uint32_t pos_frame,
                                   float pos_frac,
                                   float gain,
                                   bool layer_loop_voice,
                                   uint32_t start,
                                   uint32_t end,
                                   uint32_t seam_frames,
                                   float loop_shape,
                                   float sample_rate,
                                   bool& used_seam_xfade,
                                   bool use_edit,
                                   bool region_loop_enabled,
                                   uint32_t ls_i,
                                   uint32_t le_i,
                                   bool gate_for_wrap)
{
    if(layer_loop_voice)
        return SampleAtLoopSeamCrossfade(sample,
                                         pos_frame,
                                         pos_frac,
                                         start,
                                         end,
                                         seam_frames,
                                         loop_shape,
                                         sample_rate,
                                         used_seam_xfade)
            * gain;
    if(use_edit)
        return SampleAtLinearRegion(sample,
                                    pos_frame,
                                    pos_frac,
                                    start,
                                    end,
                                    region_loop_enabled,
                                    ls_i,
                                    le_i) * gain;

    const bool wrap_end
        = VoiceRenderLoop_FullSampleWrapGate(sample, region_loop_enabled, gate_for_wrap);
    return SampleAtLinear(sample, pos_frame, pos_frac, wrap_end) * gain;
}

size_t VoiceRenderFetch_VoiceStreamBatch(const VoiceBatchFetchParams& p,
                                         uint32_t& pos_frame,
                                         float& pos_frac,
                                         int8_t& dir,
                                         bool& gate,
                                         size_t count,
                                         float* out_buf,
                                         uint32_t* seam_cycles,
                                         uint32_t* seam_count)
{
    // ------------------------------------------------------------------------
    // Fast path: forward loop (live seam crossfade OR none). The general path
    // calls AdvancePos and ApplyBoundaryFadeNoSeam per sample; both live in a
    // different TU (voice_engine_render_loop.cpp) so without LTO they are real
    // per-sample calls whose overhead dominates fetch CPU. Here the read still
    // goes through VoiceRenderFetch_VoiceStream (same TU, inlines — it handles the
    // seam crossfade), but the forward-loop advance is inlined and the boundary
    // fade is skipped because in these conditions it is always a no-op
    // (seam_frames>0 -> ApplyBoundaryFadeNoSeam returns s; else thresholds span
    // the whole region). Semantics match the general path exactly.
    // ------------------------------------------------------------------------
    const Sample* fs = p.sample;
    const bool boundary_noop = (p.seam_frames > 0u)
                               || (p.fade_start_threshold <= static_cast<float>(p.start)
                                   && p.fade_end_threshold >= static_cast<float>(p.end));
    if(fs != nullptr && fs->pcm != nullptr && fs->length > 0u && p.layer_loop_voice
       && p.voice_loop_mode == LoopMode::Forward && p.loop_enabled && gate && p.ls_i == p.start
       && p.le_i == p.end && p.end > p.start && p.end <= fs->length && boundary_noop)
    {
        const uint32_t start = p.start;
        const uint32_t end = p.end;
        const uint32_t seam = p.seam_frames;                       // forward wrap offset
        // Live crossfade width: 0 when the sample is baked (seam region already
        // contains the pre-blend), so the per-sample seam branch never fires and
        // playback reads a single tap. seam (the wrap offset) stays non-zero so
        // the wrap still skips the baked head region.
        const uint32_t crossfade_seam = p.crossfade_seam_frames;
        const float eff_span = static_cast<float>(end - start - seam); // loop_span - seam_offset
        const float ratio = p.ratio;
        // Hoisted invariants for the inlined steady-state read below. The
        // fast-path entry conditions guarantee fs/pcm are valid, end>start,
        // end<=length, and pos stays in [start,end); the per-sample read is the
        // SampleAtLoopSeamCrossfade non-seam branch (a single SampleAtLinearRegion
        // with loop_start==start, loop_end==end) which we inline here to remove
        // the two cross-call frames (SampleAtLoopSeamCrossfade + SampleAtLinearRegion)
        // that otherwise dominate fetch CPU. Seam-region samples (rare) still
        // defer to the shared helper for a bit-exact equal-power crossfade.
        const int16_t* const pcm = fs->pcm;
        const float gain = p.gain;
        const uint32_t seam_start = (seam > 0u) ? (end - seam) : end;
        const float inv_seam = (seam > 0u) ? (1.0f / static_cast<float>(seam)) : 0.0f;
        float shape_c = p.loop_shape;
        if(shape_c < 0.0f)
            shape_c = 0.0f;
        else if(shape_c > 1.0f)
            shape_c = 1.0f;
        uint32_t pf = pos_frame;
        float pfrac = pos_frac;
        for(size_t i = 0; i < count; ++i)
        {
            float sv;
            if(crossfade_seam > 0u && pf >= seam_start)
            {
                uint32_t seam_t0 = 0u;
                if(seam_cycles)
                    seam_t0 = DWT->CYCCNT;
                if(seam_count)
                    ++(*seam_count);

                // Inlined SampleAtLoopSeamCrossfade seam branch: two region
                // reads (tail at pf, head at start+offset) blended by an
                // equal-power weight, no cross-call frames. Each read is an
                // inlined SampleAtLinearRegion (loop_start==start, loop_end==end)
                // wrapping its +1 tap to start at the region edge.
                float mix = (static_cast<float>(pf - seam_start) + pfrac) * inv_seam;
                if(mix < 0.0f)
                    mix = 0.0f;
                else if(mix > 1.0f)
                    mix = 1.0f;

                const uint32_t hf = start + (pf - seam_start);
                uint32_t tn = pf + 1u;
                if(tn >= end)
                    tn = start;
                uint32_t hn = hf + 1u;
                if(hn >= end)
                    hn = start;
                const float ta = static_cast<float>(pcm[pf]) * (1.0f / 32768.0f);
                const float tb = static_cast<float>(pcm[tn]) * (1.0f / 32768.0f);
                const float ha = static_cast<float>(pcm[hf]) * (1.0f / 32768.0f);
                const float hb = static_cast<float>(pcm[hn]) * (1.0f / 32768.0f);
                const float tail = ta + pfrac * (tb - ta);
                const float head = ha + pfrac * (hb - ha);

                const float lin_h = mix;         // head fades in
                const float lin_t = 1.0f - mix;  // tail fades out
                int idx_h = static_cast<int>(lin_h * 255.0f + 0.5f);
                int idx_t = static_cast<int>(lin_t * 255.0f + 0.5f);
                if(idx_h > 255)
                    idx_h = 255;
                if(idx_t > 255)
                    idx_t = 255;
                const float ep_h = s_sqrt_lut[idx_h];
                const float ep_t = s_sqrt_lut[idx_t];
                const float hw = lin_h + (ep_h - lin_h) * shape_c;
                const float tw = lin_t + (ep_t - lin_t) * shape_c;
                sv = (tail * tw + head * hw) * gain;

                if(seam_cycles)
                    *seam_cycles += DWT->CYCCNT - seam_t0;
            }
            else
            {
                const int16_t a = pcm[pf];
                uint32_t nxt = pf + 1u;
                if(nxt >= end)
                    nxt = start + seam; // wrap target matches the position wrap
                                        // below (start+seam); using `start` here
                                        // left a per-wrap interpolation step that
                                        // stacked across detuned voices into a click.
                const int16_t b = pcm[nxt];
                const float fa = static_cast<float>(a) * (1.0f / 32768.0f);
                const float fb = static_cast<float>(b) * (1.0f / 32768.0f);
                sv = (fa + pfrac * (fb - fa)) * gain;
            }
            out_buf[i] = sv;

            // Forward advance + wrap to (start + seam); a live forward loop never ends here.
            const float total = pfrac + ratio;
            const uint32_t whole = static_cast<uint32_t>(total); // floor (total >= 0)
            pfrac = total - static_cast<float>(whole);
            pf += whole;
            if(pf >= end)
            {
                if(eff_span <= 0.0f)
                {
                    pf = start + seam;
                    pfrac = 0.0f;
                }
                else
                {
                    float overshoot = static_cast<float>(pf - end) + pfrac;
                    // overshoot is always >= 0 and, for any normal-length loop,
                    // smaller than eff_span after a single subtract (the
                    // per-sample advance << eff_span). This is an exact
                    // replacement for std::fmod on a non-negative value, minus
                    // the libcall whose per-wrap cost scaled fetch CPU with
                    // playback pitch. The loop only iterates more than once for
                    // pathologically short loops (eff_span <= ratio).
                    while(overshoot >= eff_span)
                        overshoot -= eff_span;
                    const uint32_t w = static_cast<uint32_t>(overshoot);
                    pf = start + seam + w;
                    pfrac = overshoot - static_cast<float>(w);
                }
            }
        }
        pos_frame = pf;
        pos_frac = pfrac;
        return count; // gate stays true; no end-of-stream in a live forward loop
    }

    // ------------------------------------------------------------------------
    // Fast path: forward layer-loop voice with NO seam crossfade (seam_frames==0).
    // This is the path ADSR-playback voices take (RenderNormalVoice_ forces
    // seam_frames = crossfade_seam_frames = 0 and Forward), and it also covers a
    // plain forward loop whose crossfade amount is 0. The general path below runs
    // three cross-TU calls per sample (SampleAtLoopSeamCrossfade + the boundary
    // fade + AdvancePos), which is why these voices — including every ADSR
    // sub-region sustain loop and every ADSR release play-out — cost MORE than a
    // seam'd loop (which takes the inlined fast path above). Here the region read,
    // the 1 ms edge boundary fade, and the forward advance/wrap are all inlined.
    //
    // The read is identical for the whole batch regardless of loop state: a
    // layer-loop voice always fetches through SampleAtLoopSeamCrossfade, which with
    // seam_frames==0 collapses to SampleAtLinearRegion(loop_start=start,
    // loop_end=end) — a single two-tap blend whose +1 tap wraps end->start (NOT
    // le_i->ls_i; this matches the general path exactly, sub-region seam nuance
    // included). Only the position advance differs, and it is fixed for the block:
    //   - loop_active (sustain held): forward wrap le_i->ls_i, never ends.
    //   - otherwise (attack/decay/sustain with no sustain-loop, or the gate-off
    //     release play-out): plain forward advance, one-shot end-of-stream at end.
    // Semantics match the general path exactly, including the post-eos clamp to
    // (end-1) and the boundary-faded re-read for the rest of the batch.
    // ------------------------------------------------------------------------
    if(fs != nullptr && fs->pcm != nullptr && fs->length > 0u && p.layer_loop_voice
       && p.voice_loop_mode == LoopMode::Forward && p.seam_frames == 0u
       && p.crossfade_seam_frames == 0u && p.end > p.start && p.end <= fs->length)
    {
        const int16_t* const pcm     = fs->pcm;
        const uint32_t       start   = p.start;
        const uint32_t       end     = p.end;
        const uint32_t       ls      = p.ls_i;
        const uint32_t       le      = p.le_i;
        const float          ratio   = p.ratio;
        const float          gain    = p.gain;
        const float          start_f = static_cast<float>(start);
        const float          end_f   = static_cast<float>(end);
        // AdvancePos loop-branch guard, constant for the block: a live forward loop
        // wraps le->ls only while loop_enabled && gate && le>ls && le<=len. When
        // false, AdvancePos falls through to plain forward advance ending at `end`.
        const bool  loop_active = p.loop_enabled && gate && le > ls && le <= end;
        const float loop_span_f = loop_active ? static_cast<float>(le - ls) : 0.0f;
        // Boundary-fade window: recompute fade_frames exactly as
        // ComputeLoopBoundaryFade does (sr * 1 ms, clamped to region*0.5) so the
        // edge fade is bit-identical. The interior skip uses the same thresholds
        // ApplyBoundaryFadeNoSeam does (start+ff .. end-ff), passed through p.
        float ff = p.sample_rate * 0.001f * 1.0f; // kLoopBoundaryFadeMs
        const float region = end_f - start_f;
        if(ff < 1.0f)
            ff = 1.0f;
        if(ff > region * 0.5f)
            ff = region * 0.5f;
        const float fss = p.fade_start_threshold; // start + ff
        const float fse = p.fade_end_threshold;   // end   - ff
        uint32_t pf    = pos_frame;
        float    pfrac = pos_frac;
        for(size_t i = 0; i < count; ++i)
        {
            // Region read: two-tap blend, +1 tap wraps end->start (loop_enabled
            // is hardcoded true inside SampleAtLoopSeamCrossfade for a layer-loop
            // voice, so the wrap fires regardless of the actual loop state).
            const int16_t a   = pcm[pf];
            uint32_t      nxt = pf + 1u;
            if(nxt >= end)
                nxt = start;
            const int16_t b  = pcm[nxt];
            const float   fa = static_cast<float>(a) * (1.0f / 32768.0f);
            const float   fb = static_cast<float>(b) * (1.0f / 32768.0f);
            // Apply gain first, exactly as the general path does (the region read
            // returns read*gain, then the boundary fade multiplies that) — float
            // multiply is not associative, so the (read*gain)*fade order must match
            // to stay bit-identical.
            float sv = (fa + pfrac * (fb - fa)) * gain;

            // Boundary fade: no-op in the interior (skip), 1 ms linear ramp at each
            // region edge. Matches ComputeLoopBoundaryFade term-for-term.
            const float pos = static_cast<float>(pf) + pfrac;
            if(!(pos >= fss && pos <= fse))
            {
                float fade = 1.0f;
                if(pos < fss)
                    fade = (pos - start_f) / ff;
                else if(pos > fse)
                    fade = (end_f - pos) / ff;
                if(fade < 0.0f)
                    fade = 0.0f;
                if(fade > 1.0f)
                    fade = 1.0f;
                sv *= fade;
            }
            out_buf[i] = sv;

            // Forward advance (inlined AdvanceFrameFrac; ratio > 0 so the frame
            // index only increases).
            const float    total = pfrac + ratio;
            const uint32_t whole = static_cast<uint32_t>(total); // floor (total >= 0)
            pfrac = total - static_cast<float>(whole);
            pf += whole;
            if(loop_active)
            {
                // Wrap le->ls (seam offset is 0). Mirrors AdvancePos: fmod the
                // non-negative overshoot by the loop span, then re-seat from ls.
                if(pf >= le)
                {
                    float overshoot
                        = (static_cast<float>(pf - le) + pfrac);
                    overshoot = std::fmod(overshoot, loop_span_f);
                    if(overshoot < 0.0f)
                        overshoot += loop_span_f;
                    const uint32_t w = static_cast<uint32_t>(overshoot);
                    pf    = ls + w;
                    pfrac = overshoot - static_cast<float>(w);
                }
            }
            else if(pf >= end)
            {
                // One-shot end-of-stream. Clamp pos to end-1, drop the gate, and
                // refill the rest of the batch from the boundary frame with the
                // same edge boundary fade the general path applies at (end-1).
                pos_frame = end - 1u;
                pos_frac  = 0.0f;
                gate      = false;
                const int16_t ea  = pcm[end - 1u];
                // (read*gain) first, then fade — same order as the general path.
                float         eos_val = static_cast<float>(ea) * (1.0f / 32768.0f) * gain;
                const float   epos = static_cast<float>(end - 1u);
                if(!(epos >= fss && epos <= fse))
                {
                    float efade = 1.0f;
                    if(epos < fss)
                        efade = (epos - start_f) / ff;
                    else if(epos > fse)
                        efade = (end_f - epos) / ff;
                    if(efade < 0.0f)
                        efade = 0.0f;
                    if(efade > 1.0f)
                        efade = 1.0f;
                    eos_val *= efade;
                }
                for(size_t k = i + 1u; k < count; ++k)
                    out_buf[k] = eos_val;
                return i;
            }
        }
        pos_frame = pf;
        pos_frac  = pfrac;
        return count;
    }

    // ------------------------------------------------------------------------
    // Fast path: one-shot forward (non-loop) playback. The general path below
    // calls AdvancePos and ApplyBoundaryFadeNoSeam per sample; both live in
    // voice_engine_render_loop.cpp, so without LTO they are real per-sample
    // calls whose overhead makes one-shot voices COST MORE than looped ones
    // (which take the inlined fast path above). For a one-shot voice the
    // boundary fade is a no-op (ApplyBoundaryFadeNoSeam returns s when
    // loop_voice is false) and the advance is a plain forward step that ends
    // the stream at p.end. With the loop disabled both the full-sample read
    // (SampleAtLinear, wrap_end false) and the trimmed-region read
    // (SampleAtLinearRegion, no region loop) collapse to the SAME two-tap blend
    // clamped at p.end — for the full-sample case p.end == sample length, so one
    // path covers both use_edit and non-edit one-shots. Semantics match the
    // general path exactly, including the post-eos clamp-to-(end-1) and the
    // clamped re-read for the rest of the batch.
    // ------------------------------------------------------------------------
    if(fs != nullptr && fs->pcm != nullptr && fs->length > 0u && !p.layer_loop_voice
       && !p.loop_enabled && gate && p.end > p.start && p.end <= fs->length)
    {
        const int16_t* const pcm   = fs->pcm;
        const uint32_t       end   = p.end;
        const float          ratio = p.ratio;
        const float          gain  = p.gain;
        uint32_t             pf    = pos_frame;
        float                pfrac = pos_frac;
        for(size_t i = 0; i < count; ++i)
        {
            // Inlined two-tap linear blend; the +1 tap is clamped to the current
            // frame at the region/sample end (end == length for full-sample).
            const int16_t a = pcm[pf];
            const int16_t b = (pf + 1u < end) ? pcm[pf + 1u] : a;
            const float fa = static_cast<float>(a) * (1.0f / 32768.0f);
            const float fb = static_cast<float>(b) * (1.0f / 32768.0f);
            out_buf[i] = (fa + pfrac * (fb - fa)) * gain;
            // Boundary fade: no-op for a non-loop voice — skipped.

            // Forward advance (inlined AdvanceFrameFrac; ratio > 0 so the frame
            // index only increases) + one-shot end-of-stream at p.end.
            const float    total = pfrac + ratio;
            const uint32_t whole = static_cast<uint32_t>(total); // floor (total >= 0)
            pfrac = total - static_cast<float>(whole);
            pf += whole;
            if(pf >= end)
            {
                // Advance after out_buf[i] crossed the end: stream ends here.
                // Matches the general path — clamp pos to end-1, drop gate, and
                // refill the rest of the batch from the clamped boundary frame
                // (pfrac 0 → just that frame * gain).
                pos_frame = end - 1u;
                pos_frac  = 0.0f;
                gate      = false;
                const float eos_val
                    = (static_cast<float>(pcm[end - 1u]) * (1.0f / 32768.0f)) * gain;
                for(size_t k = i + 1u; k < count; ++k)
                    out_buf[k] = eos_val;
                return i;
            }
        }
        pos_frame = pf;
        pos_frac  = pfrac;
        return count;
    }

    size_t eos_idx = count;
    const uint32_t seam_offset = p.layer_loop_voice ? p.seam_frames : 0u;

    for(size_t i = 0; i < count; ++i)
    {
        bool used_seam_xfade = false;
        float s = VoiceRenderFetch_VoiceStream(p.sample,
                                               pos_frame,
                                               pos_frac,
                                               p.gain,
                                               p.layer_loop_voice,
                                               p.start,
                                               p.end,
                                               p.crossfade_seam_frames,
                                               p.loop_shape,
                                               p.sample_rate,
                                               used_seam_xfade,
                                               p.use_edit,
                                               p.region_loop_enabled,
                                               p.ls_i,
                                               p.le_i,
                                               gate);
        s = VoiceRenderLoop_ApplyBoundaryFadeNoSeam(s,
                                                    p.layer_loop_voice,
                                                    p.seam_frames,
                                                    used_seam_xfade,
                                                    pos_frame,
                                                    pos_frac,
                                                    p.fade_start_threshold,
                                                    p.fade_end_threshold,
                                                    p.start,
                                                    p.end,
                                                    p.sample_rate);
        out_buf[i] = s;

        // Pos advance only happens while the stream is still live. Once eos
        // has been hit earlier in this batch, pos stays clamped at length-1
        // and subsequent fetches re-read the boundary frame, which matches
        // the per-sample ClampPosToLastFrameIfValid behaviour that repeats
        // for each post-eos sample.
        if(eos_idx == count)
        {
            if(!AdvancePos(pos_frame,
                           pos_frac,
                           dir,
                           p.ratio,
                           p.end,
                           p.ls_i,
                           p.le_i,
                           p.loop_enabled,
                           gate,
                           p.voice_loop_mode,
                           seam_offset))
            {
                eos_idx = i;
                gate    = false;
                if(p.end > 0u)
                {
                    pos_frame = p.end - 1u;
                    pos_frac  = 0.0f;
                }
            }
        }
    }

    return eos_idx;
}
