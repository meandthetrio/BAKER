// Host-side probe for the tape saturator: feeds known test tones and measures
// how much TONE and BIAS actually change the output. Built/run natively, not
// flashed. Compile: c++ -std=gnu++14 -O2 tests/tape_sat_probe.cpp -o /tmp/tsp
#include "../Effects/Saturator/TapeSaturator.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <functional>

static const float SR = 48000.0f;
static const int   BS = 48;
static const int   N  = 48000;     // 1 second
static const int   SKIP = 4800;    // discard filter settling

// Goertzel magnitude of one frequency over a buffer (amplitude, not power).
static double Mag(const std::vector<float>& x, double f)
{
    double w = 2.0 * M_PI * f / SR;
    double cw = cos(w), sw = sin(w), c = 2.0 * cw;
    double s1 = 0, s2 = 0;
    for(size_t i = SKIP; i < x.size(); ++i)
    {
        double s0 = x[i] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    double re = s1 - s2 * cw, im = s2 * sw;
    double n = (double)(x.size() - SKIP) / 2.0;
    return sqrt(re * re + im * im) / n;
}

static double Rms(const std::vector<float>& x)
{
    double s = 0;
    for(size_t i = SKIP; i < x.size(); ++i) s += (double)x[i] * x[i];
    return sqrt(s / (x.size() - SKIP));
}

static double Db(double a) { return 20.0 * log10(a + 1e-12); }

static void Run(float drive, float bump, float tone, float bias,
                std::vector<float>& out, std::function<float(int)> gen)
{
    TapeSaturator s;
    s.Init(SR, BS);
    for(int b = 0; b < 400; ++b) s.PrepareBlock(drive, bump, tone, bias); // settle
    out.assign(N, 0.0f);
    int n = 0;
    for(int blk = 0; blk * BS < N; ++blk)
    {
        s.PrepareBlock(drive, bump, tone, bias);
        for(int i = 0; i < BS && n < N; ++i, ++n) out[n] = s.Process(gen(n));
    }
}

int main()
{
    // ---- TONE: feed 200 Hz + 8 kHz equal level, see the tilt -------------
    auto twoTone = [](int n) {
        float t = (float)n / SR;
        return 0.25f * sinf(2 * M_PI * 200 * t) + 0.25f * sinf(2 * M_PI * 8000 * t);
    };
    printf("=== TONE (200 Hz + 8 kHz in, bump=0, bias=center) ===\n");
    printf("%-22s %10s %10s %10s\n", "setting", "200Hz dB", "8kHz dB", "tilt(hi-lo)");
    for(float drive : {0.0f, 0.3f, 0.8f})
        for(float tone : {0.0f, 0.5f, 1.0f})
        {
            std::vector<float> o;
            Run(drive, 0.0f, tone, 0.0f, o, twoTone);
            double lo = Db(Mag(o, 200)), hi = Db(Mag(o, 8000));
            char lbl[40];
            snprintf(lbl, sizeof lbl, "drive=%.1f tone=%.1f", drive, tone);
            printf("%-22s %10.2f %10.2f %10.2f\n", lbl, lo, hi, hi - lo);
        }

    // ---- BUMP: feed 50 Hz + 500 Hz + 5 kHz, see which bands it lifts ------
    auto threeTone = [](int n) {
        float t = (float)n / SR;
        return 0.18f * sinf(2 * M_PI * 50 * t) + 0.18f * sinf(2 * M_PI * 500 * t)
               + 0.18f * sinf(2 * M_PI * 5000 * t);
    };
    printf("\n=== BUMP (50 + 500 + 5k Hz in, drive=0.3, tone=center) ===\n");
    printf("%-12s %10s %10s %10s\n", "setting", "50Hz dB", "500Hz dB", "5kHz dB");
    {
        std::vector<float> ref;
        Run(0.3f, 0.0f, 0.5f, 0.0f, ref, threeTone);
        double r50 = Db(Mag(ref, 50)), r500 = Db(Mag(ref, 500)), r5k = Db(Mag(ref, 5000));
        for(float bump : {0.0f, 0.5f, 1.0f})
        {
            std::vector<float> o;
            Run(0.3f, bump, 0.5f, 0.0f, o, threeTone);
            char lbl[20];
            snprintf(lbl, sizeof lbl, "bump=%.1f", bump);
            printf("%-12s %10.2f %10.2f %10.2f\n", lbl,
                   Db(Mag(o, 50)) - r50, Db(Mag(o, 500)) - r500, Db(Mag(o, 5000)) - r5k);
        }
        printf("(values are dB change vs bump=0)\n");
    }

    // ---- BIAS: feed 1 kHz sine, measure even (2k) vs odd (3k) harmonics ---
    auto sine1k = [](int n) { return 0.5f * sinf(2 * M_PI * 1000 * (float)n / SR); };
    printf("\n=== BIAS (1 kHz sine in, drive=0.5, bump=0) ===\n");
    printf("%-26s %9s %9s %9s %9s\n", "setting (fader pos)", "1k dB", "2k(even)", "3k(odd)", "even-odd");
    // fader min = bias -1, center = 0, max = +1
    struct { const char* name; float b; } pts[] = {
        {"min (-1)", -1.0f}, {"quarter", -0.5f}, {"center (0)", 0.0f},
        {"three-qtr", 0.5f}, {"max (+1)", 1.0f}};
    for(auto p : pts)
    {
        std::vector<float> o;
        Run(0.5f, 0.0f, 0.5f, p.b, o, sine1k);
        double e = Db(Mag(o, 2000)), od = Db(Mag(o, 3000));
        printf("%-26s %9.2f %9.2f %9.2f %9.2f\n", p.name, Db(Mag(o, 1000)), e, od, e - od);
    }
    // ---- HARMONICS: 1 kHz sine, spectrum vs drive (relative to fundamental) -
    auto sineA = [](int n) { return 0.4f * sinf(2 * M_PI * 1000 * (float)n / SR); };
    printf("\n=== HARMONICS (1 kHz sine, bump=0, tone=center, bias=center) ===\n");
    printf("%-10s", "harmonic");
    for(int h = 1; h <= 8; ++h) printf(" %6dk", h);
    printf("\n");
    for(float drive : {0.0f, 0.5f, 1.0f})
    {
        std::vector<float> o;
        Run(drive, 0.0f, 0.5f, 0.0f, o, sineA);
        double fund = Mag(o, 1000);
        char lbl[16];
        snprintf(lbl, sizeof lbl, "drive=%.1f", drive);
        printf("%-10s", lbl);
        printf(" %6.1f", 0.0); // fundamental = 0 dB ref
        for(int h = 2; h <= 8; ++h) printf(" %6.1f", Db(Mag(o, 1000.0 * h)) - Db(fund));
        printf("  (dB vs fundamental)\n");
    }
    return 0;
}
