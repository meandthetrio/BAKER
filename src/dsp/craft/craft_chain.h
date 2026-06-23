#pragma once

#include <cstdint>

#include "craft/craft_copy.h"
#include "craft/craft_dial.h"
#include "craft/craft_params.h"
#include "craft/craft_snap.h"
#include "craft/craft_warm.h"

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

  private:
    struct CraftSlot
    {
        CraftCopy copy;
        CraftDial dial;
        CraftSnap snap;
        CraftWarm warm;
        // future: CraftHowl howl; CraftWarp warp;
    };

    void ProcessSlot_(uint8_t slot, float* buf, uint32_t n);

    CraftChainConfig cfg_{};
    float            sample_rate_ = 48000.0f;
    CraftSlot        slots_[kCraftSlotCount];
};

} // namespace craft
