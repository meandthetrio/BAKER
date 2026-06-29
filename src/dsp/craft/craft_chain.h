#pragma once

#include <cstdint>

#include "craft/craft_copy.h"
#include "craft/craft_dial.h"
#include "craft/craft_howl.h"
#include "craft/craft_params.h"
#include "craft/craft_rand.h"
#include "craft/craft_refresh.h"
#include "craft/craft_snap.h"
#include "craft/craft_thru.h"
#include "craft/craft_warm.h"
#include "craft/craft_warp.h"
#include "craft/craft_zero.h"

namespace craft {

// CraftSlotConfig / CraftChainConfig live in craft_params.h (POD, shareable).

// Runs up to 3 CRAFT effects in series over a mono float block. Used verbatim
// by the audio thread (live audition) and the worker thread (offline render);
// each thread owns its own CraftChain instance (no shared state).
//
// Each slot holds its own effect state (so the same plugin can appear in more
// than one slot without aliasing). New effects: add a member to CraftSlot and a
// case to ProcessSlot_.
class CraftChain
{
  public:
    // Binds each slot's CraftRefresh to its per-slot SDRAM working set.
    CraftChain();

    // Apply config + (re)initialise each active slot's effect at sample_rate.
    void ApplyConfig(const CraftChainConfig& cfg, float sample_rate);
    // Live param update: recompute coeffs WITHOUT resetting running state, so a
    // knob move mid-playthrough is heard smoothly. A slot whose plugin changed
    // is reset; unchanged slots keep their filter/decimator state.
    void UpdateParams(const CraftChainConfig& cfg);
    // Process a mono block in place through the active slots, in order.
    void Process(float* buf, uint32_t n);
    // True if at least one slot has a real (implemented) effect selected.
    bool HasActiveEffect() const;

    // Total algorithmic latency in samples introduced by the active slots (they
    // add in series). Only STFT effects (Refresh) are non-zero. The offline
    // render flushes this many extra samples and trims the leading delay so the
    // rendered sample is time-aligned with the source.
    uint32_t Latency() const;

  private:
    struct CraftSlot
    {
        CraftCopy copy;
        CraftDial dial;
        CraftSnap snap;
        CraftWarm warm;
        CraftWarp warp;
        CraftHowl howl;
        // Audio Refresh (STFT). Holds only pointers/scalars here; its large
        // working set lives in a per-slot RAM_D2 SpectralState + FreshScratch bound
        // in the CraftChain ctor (CraftRefresh::BindState).
        CraftRefresh refresh;
        // Identity STFT passthrough (Spectral Toolkit CPU baseline). Shares the
        // per-slot SpectralState with refresh — only one plugin runs per slot.
        CraftThru thru;
        // Phase Zeroing (Spectral Toolkit). Shares the per-slot SpectralState; adds a
        // per-slot PolarScratch (mag/phase) bound in the CraftChain ctor.
        CraftZero zero;
        // Phase Randomization (Spectral Toolkit). Shares the per-slot SpectralState AND
        // the PolarScratch with zero (one plugin runs per slot) — no extra memory.
        CraftRand rnd;
    };

    void ProcessSlot_(uint8_t slot, float* buf, uint32_t n);

    CraftChainConfig cfg_{};
    float            sample_rate_ = 48000.0f;
    CraftSlot        slots_[kCraftSlotCount];
};

} // namespace craft
