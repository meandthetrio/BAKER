#include "craft/craft_params.h"

namespace craft {

// ---- copy (generation loss / resample) value-label arrays ----
static const char* const kCopyRateLabels[6]  = {"48k", "32k", "27k", "22k", "16k", "12k"};
static const char* const kCopyBitsLabels[5]  = {"16", "12", "10", "8", "6"};
static const char* const kCopyToneLabels[5]  = {"clean", "soft", "ring", "leak", "bad"};
static const char* const kCopyCurveLabels[4] = {"lin", "comp", "warp", "noisy"};

// ---- fresh (Audio Refresh) value-label arrays ----
// Frequency Linearization ladder: No / Quarter / Half / Full.
static const char* const kFreshLinLabels[4] = {"no", "qtr", "half", "full"};

// Plugin descriptor table, indexed by CraftPlugin id. Plugins with param_count
// == 0 are name-only (not yet implemented) and the UI shows no param grid.
static const CraftPluginDesc kPluginDescs[kCraftPluginCount] = {
    // 0: ---- (none)
    {0u, {}},
    // 1: copy — rate, bits, drive, tone, curve, wear
    {6u,
     {{"rate", CraftParamKind::Enum, 6u, kCopyRateLabels},
      {"bits", CraftParamKind::Enum, 5u, kCopyBitsLabels},
      {"drive", CraftParamKind::Scalar, 100u, nullptr},
      {"tone", CraftParamKind::Enum, 5u, kCopyToneLabels},
      {"curve", CraftParamKind::Enum, 4u, kCopyCurveLabels},
      {"wear", CraftParamKind::Scalar, 100u, nullptr}}},
    // 2: dial — ring-mod / between stations. All scalars (0..100).
    //   tune=carrier freq, beat=2nd-carrier detune (between-stations warble),
    //   shape=sine->square, depth=ring amount, tone=post LPF, mix=dry/wet.
    {6u,
     {{"tune", CraftParamKind::Scalar, 100u, nullptr},
      {"beat", CraftParamKind::Scalar, 100u, nullptr},
      {"shape", CraftParamKind::Scalar, 100u, nullptr},
      {"depth", CraftParamKind::Scalar, 100u, nullptr},
      {"tone", CraftParamKind::Scalar, 100u, nullptr},
      {"mix", CraftParamKind::Scalar, 100u, nullptr}}},
    // 3: snap — transient burn. All scalars (0..100). sense=transient detect,
    //   burn=attack drive, edge=high-freq bite, decay=burn length (short=click,
    //   long=punch), clip=soft->hard overload, mix=dry/wet.
    {6u,
     {{"sense", CraftParamKind::Scalar, 100u, nullptr},
      {"burn", CraftParamKind::Scalar, 100u, nullptr},
      {"edge", CraftParamKind::Scalar, 100u, nullptr},
      {"decay", CraftParamKind::Scalar, 100u, nullptr},
      {"clip", CraftParamKind::Scalar, 100u, nullptr},
      {"mix", CraftParamKind::Scalar, 100u, nullptr}}},
    // 4: warm — body saturation. All scalars (0..100). drive=sat amount,
    //   tone=dark<->bright tilt, body=low-mid mass, glue=compression,
    //   grit=asymmetry (even-harmonic warmth), mix=dry/wet.
    {6u,
     {{"drive", CraftParamKind::Scalar, 100u, nullptr},
      {"tone", CraftParamKind::Scalar, 100u, nullptr},
      {"body", CraftParamKind::Scalar, 100u, nullptr},
      {"glue", CraftParamKind::Scalar, 100u, nullptr},
      {"grit", CraftParamKind::Scalar, 100u, nullptr},
      {"mix", CraftParamKind::Scalar, 100u, nullptr}}},
    // 5: howl — feedback fold. All scalars (0..100). gain=input drive,
    //   feed=feedback amount (bounded), time=comb time/pitch, fold=wavefold,
    //   damp=feedback-path darkening (stability), mix=dry/wet.
    {6u,
     {{"gain", CraftParamKind::Scalar, 100u, nullptr},
      {"feed", CraftParamKind::Scalar, 100u, nullptr},
      {"time", CraftParamKind::Scalar, 100u, nullptr},
      {"fold", CraftParamKind::Scalar, 100u, nullptr},
      {"damp", CraftParamKind::Scalar, 100u, nullptr},
      {"mix", CraftParamKind::Scalar, 100u, nullptr}}},
    // 6: warp — tape wow & flutter. All scalars (0..100). wow=slow drift depth,
    //   flut=fast flutter depth, rate=modulation speed, drop=dropout amount,
    //   tone=head-bump/darkening, mix=dry/wet.
    {6u,
     {{"wow", CraftParamKind::Scalar, 100u, nullptr},
      {"flut", CraftParamKind::Scalar, 100u, nullptr},
      {"rate", CraftParamKind::Scalar, 100u, nullptr},
      {"drop", CraftParamKind::Scalar, 100u, nullptr},
      {"tone", CraftParamKind::Scalar, 100u, nullptr},
      {"mix", CraftParamKind::Scalar, 100u, nullptr}}},
    // 7: fresh — Audio Refresh, STFT spectral-whitening exciter. Two real
    //   controls (the decode): intn = Refresh Intensity (raw 0..100 maps to
    //   Intensity 70..130%), linr = Frequency Linearization ladder. Remaining
    //   slots reserved for Phase 2 tuning knobs (envelope width / whitening).
    {4u,
     {{"tilt", CraftParamKind::Scalar, 40u, nullptr, 0u, 20u, 20u},  // -20..+20: dark<->bright whitening
      {"linr", CraftParamKind::Enum, 4u, kFreshLinLabels},
      {"skirt", CraftParamKind::Scalar, 40u, nullptr, 0u, 20u, 20u}, // -20..+20: de-skirt<->add-skirt
      {"roll", CraftParamKind::Scalar, 40u, nullptr, 0u, 20u, 20u}}}, // -20..+20: skirt HF<->LF rolloff
    // 8: thru — identity STFT passthrough (Spectral Toolkit CPU baseline). One inert
    //   param so the effect registers as active (param_count > 0) and the chain
    //   engages it; the engine is always full-wet for now. Reserved for a future
    //   dry/wet without another wiring pass.
    {1u,
     {{"mix", CraftParamKind::Scalar, 100u, nullptr, 0u, 100u, 0u}}},
    // 9: zero — Phase Zeroing (keep magnitude, align all phases). One inert param for
    //   now (always full-wet), same rationale as thru. Reserved for a future dry/wet.
    {1u,
     {{"mix", CraftParamKind::Scalar, 100u, nullptr, 0u, 100u, 0u}}},
    // 10: rand — Phase Randomization (keep magnitude, scatter phase). One inert param
    //   for now (always full-wet); reserved for a future dry/wet or scatter depth.
    {1u,
     {{"mix", CraftParamKind::Scalar, 100u, nullptr, 0u, 100u, 0u}}},
};

const CraftPluginDesc& CraftGetPluginDesc(uint8_t plugin)
{
    if(plugin >= kCraftPluginCount)
        return kPluginDescs[0];
    return kPluginDescs[plugin];
}

uint8_t CraftPluginParamCount(uint8_t plugin)
{
    return CraftGetPluginDesc(plugin).param_count;
}

const CraftParamDesc* CraftGetParamDesc(uint8_t plugin, uint8_t param)
{
    const CraftPluginDesc& d = CraftGetPluginDesc(plugin);
    if(param >= d.param_count)
        return nullptr;
    return &d.params[param];
}

} // namespace craft
