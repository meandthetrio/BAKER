#pragma once

#define LFO_SWEEP_TEST 0

// Master switch for the modulation system in the audio path: global LFO,
// macros, mod-matrix routing, and the per-voice mod-envelope. When 0, none of
// this runs in RenderBlock (no sinf/LFO tick, no macro smoothing/apply, no
// route snapshot/scan, no per-voice mod-env tick) and voices render with a
// neutral pitch (pitch_scale = 1.0). Engine tuning, gain, and EQ are
// unaffected. Flip to 1 to restore modulation.
#define MOD_SYSTEM_ENABLED 0
