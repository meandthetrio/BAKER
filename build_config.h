#pragma once

#define LFO_SWEEP_TEST 0

// Master switch for the modulation system in the audio path: global LFO,
// macros, mod-matrix routing, and the per-voice mod-envelope. When 0, none of
// this runs in RenderBlock (no sinf/LFO tick, no macro smoothing/apply, no
// route snapshot/scan, no per-voice mod-env tick) and voices render with a
// neutral pitch (pitch_scale = 1.0). Engine tuning, gain, and EQ are
// unaffected. Flip to 1 to restore modulation.
#define MOD_SYSTEM_ENABLED 0

// Run the whole Dattorro reverb at half the audio sample rate (24 kHz): the
// input is decimated 2:1, the reverb engine runs at half rate (delay lengths,
// filters, predelay, control rate all auto-scale from sample_rate_), and the
// wet output is interpolated back to 48 kHz before adding the pristine
// full-rate dry. Roughly halves the reverb's per-block math. HF rolls off near
// ~12 kHz across the reverb — inaudible on dark settings. Build-time A/B:
// 0 = full-rate (current), 1 = half-rate.
#define REVERB_HALF_RATE 1
