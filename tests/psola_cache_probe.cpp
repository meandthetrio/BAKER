// Host probe for the PSOLA bake "reuse forward analysis" optimization (#1).
//
// Phase 1: establish the baseline and measure the real ROI before any library
// surgery. It:
//   (a) confirms blockSamples / fftSamples / bands (expect 5760 / 6144 / 3072)
//       and the latency, plus the resulting analysis-cache memory footprint;
//   (b) times a stock per-slice pitch-shift (the exact process() the bake runs)
//       with formant preservation OFF and ON -> baseline ms/slice + formant cost;
//   (c) times the *cacheable* forward FFT using the real library RealFFT, then
//       projects the #1 speedup = (N-1)/N * F/stock for typical/maximum slice
//       counts.
//
// This same file grows a stock-vs-cached bit-exact diff in Phase 3.
//
// Built/run natively, not flashed.
// Compile: c++ -std=gnu++14 -O2 -I src/dsp tests/psola_cache_probe.cpp -o /tmp/pcp && /tmp/pcp
#include "../src/dsp/signalsmith-stretch/signalsmith-stretch.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

using Clock   = std::chrono::high_resolution_clock;
using Stretch = signalsmith::stretch::SignalsmithStretch<float>;
using RFFT    = signalsmith::linear::RealFFT<float>; // matches presetDefault (no split)

static const float    SR        = 48000.0f;
static const int      kInterval = 1440;  // presetDefault interval @ 48k
static const int      kBlock    = 5760;  // presetDefault block @ 48k

static double Ms(Clock::duration d)
{
    return std::chrono::duration<double, std::milli>(d).count();
}

// A realistic, sustained, harmonic-rich source note (~2 s) so process() never
// trips its silence fast-path. Fundamental ~150 Hz with a decaying harmonic
// series and a slow amplitude envelope.
static void MakeSource(std::vector<float>& x, int frames)
{
    x.assign(frames, 0.0f);
    const double f0 = 150.0;
    for(int i = 0; i < frames; ++i)
    {
        double t   = i / (double)SR;
        double env = 0.6 * (0.5 - 0.5 * std::cos(2 * M_PI * std::fmin(1.0, t / 0.05))); // 50ms fade-in
        env *= std::exp(-t * 0.4);                                                       // gentle decay
        double s = 0.0;
        for(int h = 1; h <= 24; ++h)
            s += std::sin(2 * M_PI * f0 * h * t) / h;
        x[i] = (float)(env * s * 0.3);
    }
}

static double Rms(const std::vector<float>& x, int from, int n)
{
    double s = 0;
    for(int i = from; i < from + n && i < (int)x.size(); ++i)
        s += (double)x[i] * x[i];
    return std::sqrt(s / std::max(1, n));
}

int main()
{
    // ---- (a) dimensions ----------------------------------------------------
    Stretch st;
    st.presetDefault(1, SR);
    const int   block    = st.blockSamples();
    const size_t fftSamp = RFFT::fastSizeAbove((block + 1) / 2) * 2;
    const size_t bands   = fftSamp / 2;
    const int   latency  = st.inputLatency() + st.outputLatency();

    std::printf("=== PSOLA cache probe (Phase 1) ===\n");
    std::printf("blockSamples = %d (expect 5760)\n", block);
    std::printf("fftSamples   = %zu (expect 6144)\n", fftSamp);
    std::printf("bands        = %zu (expect 3072)\n", bands);
    std::printf("latency      = %d (in %d + out %d)\n", latency,
                st.inputLatency(), st.outputLatency());

    // ---- source + buffers (mirror RunPitchShiftChunked) --------------------
    const int frames = (int)(SR * 2.0); // 2 s note
    std::vector<float> src;
    MakeSource(src, frames);

    const int total = frames + latency;
    std::vector<float> in(total, 0.0f), out(total, 0.0f);

    // cache footprint for the real bake bounds (kMaxFrames + kMaxLatencyPad)
    const int    maxFrames = 240000, maxPad = 16384;
    const int    maxBlocks = (maxFrames + maxPad) / kInterval + 2;
    const double cacheMB    = (double)maxBlocks * bands * sizeof(std::complex<float>) / (1024.0 * 1024.0);
    std::printf("max blocks   = %d  -> cache = %.2f MB (192-block array = %.2f MB)\n",
                maxBlocks, cacheMB, 192.0 * bands * sizeof(std::complex<float>) / (1024.0 * 1024.0));

    // ---- (b) stock per-slice timing, formants OFF then ON ------------------
    const int semis[] = {-24, -12, -7, -5, 5, 7, 12, 24};
    const int nSemis  = (int)(sizeof(semis) / sizeof(semis[0]));

    double stockMs[2] = {0, 0};
    for(int fm = 0; fm < 2; ++fm)
    {
        st.setFormantFactor(1.0f, /*compensatePitch=*/fm == 1);
        double acc = 0;
        for(int k = 0; k < nSemis; ++k)
        {
            std::fill(in.begin(), in.end(), 0.0f);
            for(int i = 0; i < frames; ++i) in[i] = src[i];

            st.reset();
            st.setTransposeSemitones((float)semis[k]);

            float* ic[1] = {in.data()};
            float* oc[1] = {out.data()};
            auto t0 = Clock::now();
            st.process(ic, total, oc, total);
            acc += Ms(Clock::now() - t0);

            if(k == 0)
            {
                double r = Rms(out, latency, frames);
                if(r < 1e-6)
                    std::printf("WARNING: output RMS %.2e — silence path may be active!\n", r);
            }
        }
        stockMs[fm] = acc / nSemis;
    }
    std::printf("\nstock per-slice: formants OFF = %.1f ms | ON = %.1f ms  (formant cost +%.0f%%)\n",
                stockMs[0], stockMs[1], 100.0 * (stockMs[1] / stockMs[0] - 1.0));

    // ---- (c) cacheable forward-FFT cost (real library RealFFT) -------------
    const int nBlocks = total / kInterval + 1;
    RFFT fwd;
    fwd.resize(fftSamp);
    std::vector<float>                time(fftSamp, 0.0f);
    std::vector<std::complex<float>>  freq(bands);
    for(size_t i = 0; i < fftSamp; ++i)
        time[i] = src[i % frames];

    // warm + time enough reps to be stable
    const int reps = 20;
    auto tf0 = Clock::now();
    for(int r = 0; r < reps; ++r)
        for(int b = 0; b < nBlocks; ++b)
            fwd.fft(time.data(), freq.data());
    double fMs = Ms(Clock::now() - tf0) / reps; // forward-FFT cost for one slice's worth of blocks

    std::printf("forward FFT (cacheable) per slice = %.1f ms over %d blocks\n", fMs, nBlocks);

    // ---- ROI projection: total_after = F(prime) + N*(stock - F) ------------
    std::printf("\nProjected #1 speedup = (N-1)/N * F/stock:\n");
    for(int fm = 0; fm < 2; ++fm)
    {
        double FoverStock = fMs / stockMs[fm];
        std::printf("  formants %s (F/stock = %.0f%%):\n", fm ? "ON " : "OFF", 100.0 * FoverStock);
        for(int N : {12, 24, 72})
        {
            double roi = (double)(N - 1) / N * FoverStock;
            std::printf("    N=%2d slices -> %.0f%% faster\n", N, 100.0 * roi);
        }
    }
    return 0;
}
