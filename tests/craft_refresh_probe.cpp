// Host-side probe for CRAFT "refresh" (Audio Refresh) Phase 1: verifies the
// STFT skeleton reconstructs the input near-bit-transparently (identity pass).
// Built/run natively, not flashed.
//   c++ -std=gnu++14 -O2 -Isrc/dsp tests/craft_refresh_probe.cpp \
//       src/dsp/craft/craft_refresh.cpp -o /tmp/crp && /tmp/crp
#include "craft/craft_refresh.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using craft::CraftRefresh;
using craft::RefreshState;

static const int N  = 48000; // 1 s
static const int BS = 256;   // render block size (matches the worker)

// Best integer lag (0..maxLag) maximizing normalized cross-correlation of out
// vs in, then the residual error at that lag over the steady-state region.
static void report(const char* name, const std::vector<float>& in, const std::vector<float>& out, int maxLag)
{
    int    bestLag = 0;
    double bestCorr = -1e30;
    const int skip = 6000; // discard STFT warm-up
    for(int lag = 0; lag <= maxLag; ++lag)
    {
        double num = 0, den = 0;
        for(int i = skip; i + lag < N; ++i)
        {
            num += (double)in[i] * out[i + lag];
            den += (double)out[i + lag] * out[i + lag];
        }
        double corr = (den > 1e-12) ? (num / std::sqrt(den)) : 0.0;
        if(corr > bestCorr) { bestCorr = corr; bestLag = lag; }
    }

    // Residual at best lag (with an optimal scalar gain removed).
    double sxy = 0, syy = 0, sxx = 0;
    int    cnt = 0;
    for(int i = skip; i + bestLag < N; ++i)
    {
        double x = in[i], y = out[i + bestLag];
        sxy += x * y; syy += y * y; sxx += x * x; ++cnt;
    }
    double g    = (syy > 1e-12) ? (sxy / syy) : 0.0; // gain that best maps out->in
    double err  = 0;
    for(int i = skip; i + bestLag < N; ++i)
    {
        double e = in[i] - g * out[i + bestLag];
        err += e * e;
    }
    double rmsErr = std::sqrt(err / cnt);
    double rmsIn  = std::sqrt(sxx / cnt);
    double errDb  = 20.0 * std::log10(rmsErr / (rmsIn + 1e-12) + 1e-12);
    double gainDb = 20.0 * std::log10(g + 1e-12);
    printf("  %-8s lag=%-5d gain=%+.3f dB  residual=%.1f dB %s\n",
           name, bestLag, gainDb, errDb, (errDb < -60.0 ? "PASS" : "  <-- check"));
}

static void run(const char* name, const std::vector<float>& in)
{
    static RefreshState st; // 64 KB — keep off the stack
    CraftRefresh        r;
    r.BindState(&st);
    r.Reset(48000.0f);
    uint8_t p[6] = {50, 0, 0, 0, 0, 0}; // Phase 1 ignores params
    r.SetParams(p, 48000.0f);

    std::vector<float> out = in;
    for(int pos = 0; pos < N; pos += BS)
    {
        int n = (N - pos < BS) ? (N - pos) : BS;
        r.Process(out.data() + pos, (uint32_t)n);
    }
    report(name, in, out, 4096);
}

// Replicates the worker render's latency-trim (StepCraftRenderPreview): feed
// length+latency samples (source then zeros) and write dst[k-latency]. Verifies
// the trimmed output is TIME-ALIGNED with the source (impulse stays put) and the
// tail is preserved.
static void renderTrim()
{
    static RefreshState st;
    CraftRefresh        r;
    r.BindState(&st);
    r.Reset(48000.0f);
    uint8_t p[6] = {50, 0, 0, 0, 0, 0};
    r.SetParams(p, 48000.0f);

    const int   length  = N;
    const int   latency = (int)r.Latency();
    const int   total   = length + latency;
    std::vector<float> src(length, 0.0f), dst(length, 0.0f);
    src[8000]          = 0.8f;          // alignment marker
    src[length - 1]    = 0.5f;          // tail marker (last sample)

    std::vector<float> stream;
    stream.reserve(total);
    for(int k = 0; k < total; ++k) stream.push_back(k < length ? src[k] : 0.0f);
    for(int pos = 0; pos < total; pos += BS)
    {
        int n = (total - pos < BS) ? (total - pos) : BS;
        r.Process(stream.data() + pos, (uint32_t)n);
        for(int i = 0; i < n; ++i)
        {
            int k = pos + i;
            if(k >= latency && (k - latency) < length) dst[k - latency] = stream[k];
        }
    }

    // Peak location of dst should match the input impulse (8000), not 8000+latency.
    int    peak = 0; float pv = 0;
    for(int i = 0; i < length; ++i) if(std::fabs(dst[i]) > pv) { pv = std::fabs(dst[i]); peak = i; }
    double tail = std::fabs(dst[length - 1]);
    printf("  trim     impulse@%d (src 8000, %s) | tail[last]=%.3f (src 0.5, %s)\n",
           peak, (std::abs(peak - 8000) <= 1 ? "ALIGNED" : "MISALIGNED"),
           tail, (tail > 0.3 ? "PRESERVED" : "LOST"));
}

// File mode (calibration): read raw float32 mono from inPath, process through
// CraftRefresh at the given Intensity (70..130) / linear, write raw float32 to
// outPath. Latency is irrelevant for magnitude-spectrum comparison, so no trim.
//   crp <in.f32> <out.f32> <tilt> [linear] [skirt] [roll]   (bipolar -20..+20)
static int fileMode(const char* inPath, const char* outPath, float intensity, int linear, int skirt, int roll)
{
    FILE* fi = std::fopen(inPath, "rb");
    if(!fi) { std::fprintf(stderr, "open %s failed\n", inPath); return 1; }
    std::fseek(fi, 0, SEEK_END);
    long bytes = std::ftell(fi);
    std::fseek(fi, 0, SEEK_SET);
    std::vector<float> buf(bytes / sizeof(float));
    if(std::fread(buf.data(), sizeof(float), buf.size(), fi) != buf.size()) { std::fclose(fi); return 1; }
    std::fclose(fi);

    static RefreshState st;
    CraftRefresh        r;
    r.BindState(&st);
    r.Reset(44100.0f);
    int     dv = (int)std::lround(intensity); // tilt, bipolar -20..+20
    if(dv < -20) dv = -20; if(dv > 20) dv = 20;
    int     sv = skirt;                       // skirt, bipolar -20..+20
    if(sv < -20) sv = -20; if(sv > 20) sv = 20;
    int     rv = roll;                        // roll, bipolar -20..+20
    if(rv < -20) rv = -20; if(rv > 20) rv = 20;
    uint8_t p[6] = {(uint8_t)(dv + 20), (uint8_t)linear, (uint8_t)(sv + 20), (uint8_t)(rv + 20), 0, 0};

    r.SetParams(p, 44100.0f);

    for(size_t pos = 0; pos < buf.size(); pos += BS)
    {
        int n = (int)std::min((size_t)BS, buf.size() - pos);
        r.Process(buf.data() + pos, (uint32_t)n);
    }

    FILE* fo = std::fopen(outPath, "wb");
    if(!fo) return 1;
    std::fwrite(buf.data(), sizeof(float), buf.size(), fo);
    std::fclose(fo);
    return 0;
}

int main(int argc, char** argv)
{
    if(argc >= 4)
        return fileMode(argv[1], argv[2], (float)std::atof(argv[3]),
                        argc >= 5 ? std::atoi(argv[4]) : 0,
                        argc >= 6 ? std::atoi(argv[5]) : 0,
                        argc >= 7 ? std::atoi(argv[6]) : 0);

    printf("CRAFT refresh Phase 1 — STFT identity reconstruction\n");
    printf("(residual is RMS of (in - gain*out) after lag align; PASS < -60 dB)\n");

    std::vector<float> sine(N), noise(N), imp(N, 0.0f);
    unsigned seed = 12345;
    for(int i = 0; i < N; ++i)
    {
        sine[i]  = 0.5f * std::sin(2.0 * M_PI * 1000.0 * i / 48000.0);
        seed     = seed * 1664525u + 1013904223u;
        noise[i] = ((float)(seed >> 9) / 8388608.0f - 1.0f) * 0.3f;
    }
    imp[8000] = 0.8f;

    run("sine1k", sine);
    run("white", noise);
    run("impulse", imp);
    printf("\nRender latency-trim (time-alignment):\n");
    renderTrim();
    return 0;
}
