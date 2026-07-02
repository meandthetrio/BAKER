// Host-side equivalence probe for the ADSR / no-seam forward-loop fast path in
// voice_engine_render_fetch.cpp (VoiceRenderFetch_VoiceStreamBatch).
//
// It runs two implementations over many randomized parameter combinations and
// asserts they produce BIT-IDENTICAL output buffers and identical end state
// (pos_frame, pos_frac, gate, return index):
//   REF  - the general per-sample path, transcribed verbatim from the shipping
//          helpers (SampleAtLinearRegion, SampleAtLoopSeamCrossfade seam==0
//          branch, AdvancePos, ApplyBoundaryFadeNoSeam, ComputeLoopBoundaryFade).
//   FAST - the inlined fast path added to VoiceRenderFetch_VoiceStreamBatch.
//
// Covers all ADSR sub-cases (sub-region sustain loop, whole-region loop, gate-off
// release play-out, no-sustain-loop through-play) plus plain no-seam loops.
//
// Compile/run:
//   c++ -std=gnu++14 -O2 tests/adsr_fetch_fastpath_probe.cpp -o /tmp/afp && /tmp/afp
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <vector>
#include <random>

static const float kLoopBoundaryFadeMs = 1.0f;

// ---- transcribed helpers (verbatim from the firmware sources) ----------------
static void AdvanceFrameFrac(uint32_t& pos_frame, float& pos_frac, float delta)
{
    const float total = pos_frac + delta;
    const int32_t whole = static_cast<int32_t>(std::floor(total));
    const float frac = total - static_cast<float>(whole);
    const int64_t frame = static_cast<int64_t>(pos_frame) + static_cast<int64_t>(whole);
    if(frame >= 0)
    {
        pos_frame = static_cast<uint32_t>(frame);
        pos_frac = frac;
        return;
    }
    pos_frame = 0u;
    pos_frac = static_cast<float>(frame) + frac;
}
static float PositionDeltaFromFrame(uint32_t pos_frame, float pos_frac, uint32_t boundary_frame)
{
    return static_cast<float>(pos_frame - boundary_frame) + pos_frac;
}
static void SetPositionFromBoundaryOffset(uint32_t boundary_frame, float offset,
                                          uint32_t& pos_frame, float& pos_frac)
{
    if(offset < 0.0f) offset = 0.0f;
    const uint32_t whole = static_cast<uint32_t>(offset);
    pos_frame = boundary_frame + whole;
    pos_frac  = offset - static_cast<float>(whole);
}
static float PosAsFloat(uint32_t frame, float frac) { return static_cast<float>(frame) + frac; }

// AdvancePos Forward-only (dir/pingpong unused for these voices), seam_offset=0.
static bool AdvancePos(uint32_t& pos_frame, float& pos_frac, float ratio, uint32_t len,
                       uint32_t ls, uint32_t le, bool loop_enabled, bool gate)
{
    if(!loop_enabled || !gate || le <= ls || le > len)
    {
        AdvanceFrameFrac(pos_frame, pos_frac, ratio);
        if(pos_frame >= len) return false;
        return true;
    }
    const uint32_t loop_span = le - ls;
    AdvanceFrameFrac(pos_frame, pos_frac, ratio);
    if(pos_frame >= le)
    {
        const uint32_t effective_span = loop_span; // seam_offset 0
        float overshoot = PositionDeltaFromFrame(pos_frame, pos_frac, le);
        if(effective_span == 0u) { pos_frame = ls; pos_frac = 0.0f; }
        else
        {
            const float span_f = static_cast<float>(effective_span);
            overshoot = std::fmod(overshoot, span_f);
            if(overshoot < 0.0f) overshoot += span_f;
            SetPositionFromBoundaryOffset(ls, overshoot, pos_frame, pos_frac);
        }
    }
    return true;
}
static float ComputeLoopBoundaryFade(float pos, uint32_t start, uint32_t end, float sr)
{
    if(end <= start) return 1.0f;
    float fade_frames = sr * 0.001f * kLoopBoundaryFadeMs;
    const float region_frames = static_cast<float>(end - start);
    if(fade_frames < 1.0f) fade_frames = 1.0f;
    if(fade_frames > region_frames * 0.5f) fade_frames = region_frames * 0.5f;
    if(fade_frames <= 0.0f) return 1.0f;
    const float start_f = static_cast<float>(start), end_f = static_cast<float>(end);
    float fade = 1.0f;
    if(pos < start_f + fade_frames) fade = (pos - start_f) / fade_frames;
    else if(pos > end_f - fade_frames) fade = (end_f - pos) / fade_frames;
    if(fade < 0.0f) fade = 0.0f;
    if(fade > 1.0f) fade = 1.0f;
    return fade;
}
static float ApplyBoundaryFadeNoSeam(float s, uint32_t pos_frame, float pos_frac,
                                     float fss, float fse, uint32_t start, uint32_t end, float sr)
{
    const float pos = PosAsFloat(pos_frame, pos_frac);
    if(pos >= fss && pos <= fse) return s;
    return s * ComputeLoopBoundaryFade(pos, start, end, sr);
}
// layer-loop read: SampleAtLoopSeamCrossfade seam==0 -> SampleAtLinearRegion
// (loop_enabled hardcoded true, loop_start=start, loop_end=end).
static float ReadRegion(const int16_t* pcm, uint32_t pf, float pfrac,
                        uint32_t start, uint32_t end, float gain)
{
    if(pf < start || pf >= end) return 0.0f;
    const int16_t a = pcm[pf];
    uint32_t next = pf + 1;
    if(next >= end) next = start; // loop_enabled true, start<end
    const int16_t b = pcm[next];
    const float fa = static_cast<float>(a) * (1.0f / 32768.0f);
    const float fb = static_cast<float>(b) * (1.0f / 32768.0f);
    return (fa + pfrac * (fb - fa)) * gain;
}

// ---- REF: general per-sample path -------------------------------------------
static size_t Ref(const int16_t* pcm, uint32_t len, uint32_t start, uint32_t end,
                  uint32_t ls, uint32_t le, bool loop_enabled, float ratio, float gain,
                  float sr, float fss, float fse,
                  uint32_t& pos_frame, float& pos_frac, bool& gate,
                  size_t count, float* out)
{
    size_t eos_idx = count;
    for(size_t i = 0; i < count; ++i)
    {
        float s = ReadRegion(pcm, pos_frame, pos_frac, start, end, gain);
        s = ApplyBoundaryFadeNoSeam(s, pos_frame, pos_frac, fss, fse, start, end, sr);
        out[i] = s;
        if(eos_idx == count)
        {
            if(!AdvancePos(pos_frame, pos_frac, ratio, end, ls, le, loop_enabled, gate))
            {
                eos_idx = i; gate = false;
                if(end > 0u) { pos_frame = end - 1u; pos_frac = 0.0f; }
            }
        }
    }
    return eos_idx;
}

// ---- FAST: transcription of the new fast path in the firmware ----------------
static size_t Fast(const int16_t* pcm, uint32_t len, uint32_t start, uint32_t end,
                   uint32_t ls, uint32_t le, bool loop_enabled, float ratio, float gain,
                   float sr, float fss, float fse,
                   uint32_t& pos_frame, float& pos_frac, bool& gate,
                   size_t count, float* out_buf)
{
    const float start_f = static_cast<float>(start);
    const float end_f   = static_cast<float>(end);
    const bool  loop_active = loop_enabled && gate && le > ls && le <= end;
    const float loop_span_f = loop_active ? static_cast<float>(le - ls) : 0.0f;
    float ff = sr * 0.001f * 1.0f;
    const float region = end_f - start_f;
    if(ff < 1.0f) ff = 1.0f;
    if(ff > region * 0.5f) ff = region * 0.5f;
    uint32_t pf = pos_frame;
    float pfrac = pos_frac;
    for(size_t i = 0; i < count; ++i)
    {
        const int16_t a = pcm[pf];
        uint32_t nxt = pf + 1u;
        if(nxt >= end) nxt = start;
        const int16_t b = pcm[nxt];
        const float fa = static_cast<float>(a) * (1.0f / 32768.0f);
        const float fb = static_cast<float>(b) * (1.0f / 32768.0f);
        float sv = (fa + pfrac * (fb - fa)) * gain;
        const float pos = static_cast<float>(pf) + pfrac;
        if(!(pos >= fss && pos <= fse))
        {
            float fade = 1.0f;
            if(pos < fss) fade = (pos - start_f) / ff;
            else if(pos > fse) fade = (end_f - pos) / ff;
            if(fade < 0.0f) fade = 0.0f;
            if(fade > 1.0f) fade = 1.0f;
            sv *= fade;
        }
        out_buf[i] = sv;
        const float total = pfrac + ratio;
        const uint32_t whole = static_cast<uint32_t>(total);
        pfrac = total - static_cast<float>(whole);
        pf += whole;
        if(loop_active)
        {
            if(pf >= le)
            {
                float overshoot = (static_cast<float>(pf - le) + pfrac);
                overshoot = std::fmod(overshoot, loop_span_f);
                if(overshoot < 0.0f) overshoot += loop_span_f;
                const uint32_t w = static_cast<uint32_t>(overshoot);
                pf = ls + w;
                pfrac = overshoot - static_cast<float>(w);
            }
        }
        else if(pf >= end)
        {
            pos_frame = end - 1u; pos_frac = 0.0f; gate = false;
            const int16_t ea = pcm[end - 1u];
            float eos_val = static_cast<float>(ea) * (1.0f / 32768.0f) * gain;
            const float epos = static_cast<float>(end - 1u);
            if(!(epos >= fss && epos <= fse))
            {
                float efade = 1.0f;
                if(epos < fss) efade = (epos - start_f) / ff;
                else if(epos > fse) efade = (end_f - epos) / ff;
                if(efade < 0.0f) efade = 0.0f;
                if(efade > 1.0f) efade = 1.0f;
                eos_val *= efade;
            }
            for(size_t k = i + 1u; k < count; ++k) out_buf[k] = eos_val;
            return i;
        }
    }
    pos_frame = pf; pos_frac = pfrac;
    return count;
}

int main()
{
    const float SR = 48000.0f;
    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> lenD(64, 4000);
    std::uniform_real_distribution<float> ratioD(0.25f, 4.0f);
    std::uniform_real_distribution<float> fracD(0.0f, 0.999f);
    std::uniform_real_distribution<float> gainD(0.1f, 1.5f);

    long cases = 0, mism = 0;
    double worst = 0.0;
    for(int t = 0; t < 200000; ++t)
    {
        uint32_t len = (uint32_t)lenD(rng);
        std::vector<int16_t> pcm(len);
        for(uint32_t i = 0; i < len; ++i) pcm[i] = (int16_t)((rng() & 0xFFFF) - 32768);

        // region [start,end) within the sample
        uint32_t start = rng() % (len / 2);
        uint32_t end = start + 2 + rng() % (len - start - 1);
        if(end > len) end = len;
        if(end <= start + 1) continue;
        // loop sub-region [ls,le) within [start,end) (ADSR: d_pos..r_pos)
        uint32_t ls = start + rng() % (end - start - 1);
        uint32_t le = ls + 1 + rng() % (end - ls);
        if(le > end) le = end;
        bool loop_enabled = (rng() & 1);
        bool gate = (rng() & 3) != 0; // mostly gated, sometimes released
        float ratio = ratioD(rng);
        float gain = gainD(rng);

        // thresholds exactly as render_voice computes them
        float ff = SR * 0.001f * kLoopBoundaryFadeMs;
        if(ff < 1.0f) ff = 1.0f;
        float regionf = (float)(end - start);
        if(ff > regionf * 0.5f) ff = regionf * 0.5f;
        float fss = (float)start + ff;
        float fse = (float)end - ff;

        // start position somewhere in [start,end)
        uint32_t p0f = start + (rng() % (end - start));
        float p0frac = fracD(rng);
        size_t count = 1 + rng() % 48;

        std::vector<float> a(count), b(count);
        uint32_t rf = p0f, ff2 = p0f; float rfr = p0frac, ffr = p0frac;
        bool rg = gate, fg = gate;
        size_t re = Ref(pcm.data(), len, start, end, ls, le, loop_enabled, ratio, gain,
                        SR, fss, fse, rf, rfr, rg, count, a.data());
        size_t fe = Fast(pcm.data(), len, start, end, ls, le, loop_enabled, ratio, gain,
                         SR, fss, fse, ff2, ffr, fg, count, b.data());
        ++cases;
        bool bad = (re != fe) || (rf != ff2) || (rg != fg) || (rfr != ffr);
        for(size_t i = 0; i < count; ++i)
        {
            double d = std::fabs((double)a[i] - (double)b[i]);
            if(d > worst) worst = d;
            if(a[i] != b[i]) bad = true;
        }
        if(bad)
        {
            ++mism;
            if(mism <= 5)
                printf("MISMATCH t=%d len=%u [%u,%u) loop[%u,%u) le=%d gate=%d ratio=%.3f "
                       "reIdx=%zu feIdx=%zu rf=%u ff=%u rfr=%.9g ffr=%.9g\n",
                       t, len, start, end, ls, le, (int)loop_enabled, (int)gate, ratio,
                       re, fe, rf, ff2, rfr, ffr);
        }
    }
    printf("cases=%ld mismatches=%ld worst_abs_diff=%.3e\n", cases, mism, worst);
    printf(mism == 0 ? "PASS: fast path is bit-identical to the general path\n"
                     : "FAIL: divergence detected\n");
    return mism == 0 ? 0 : 1;
}
