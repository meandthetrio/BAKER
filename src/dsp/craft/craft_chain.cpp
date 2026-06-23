#include "craft/craft_chain.h"

namespace craft {

void CraftChain::ApplyConfig(const CraftChainConfig& cfg, float sample_rate)
{
    cfg_         = cfg;
    sample_rate_ = (sample_rate > 1.0f) ? sample_rate : 48000.0f;

    for(uint8_t s = 0; s < kCraftSlotCount; ++s)
    {
        const CraftSlotConfig& sc = cfg_.slots[s];
        switch(sc.plugin)
        {
            case kCraftPluginCopy:
                slots_[s].copy.Reset(sample_rate_);
                slots_[s].copy.SetParams(sc.param, sample_rate_);
                break;
            default: break; // None / not-yet-implemented: nothing to init
        }
    }
}

void CraftChain::UpdateParams(const CraftChainConfig& cfg)
{
    for(uint8_t s = 0; s < kCraftSlotCount; ++s)
    {
        const bool plugin_changed = (cfg.slots[s].plugin != cfg_.slots[s].plugin);
        cfg_.slots[s]             = cfg.slots[s];
        switch(cfg_.slots[s].plugin)
        {
            case kCraftPluginCopy:
                if(plugin_changed)
                    slots_[s].copy.Reset(sample_rate_);
                slots_[s].copy.SetParams(cfg_.slots[s].param, sample_rate_);
                break;
            default: break;
        }
    }
}

void CraftChain::ProcessSlot_(uint8_t slot, float* buf, uint32_t n)
{
    switch(cfg_.slots[slot].plugin)
    {
        case kCraftPluginCopy: slots_[slot].copy.Process(buf, n); break;
        default: break; // None / not-yet-implemented: pass through
    }
}

void CraftChain::Process(float* buf, uint32_t n)
{
    for(uint8_t s = 0; s < kCraftSlotCount; ++s)
        ProcessSlot_(s, buf, n);
}

bool CraftChain::HasActiveEffect() const
{
    for(uint8_t s = 0; s < kCraftSlotCount; ++s)
        if(CraftPluginParamCount(cfg_.slots[s].plugin) > 0u)
            return true;
    return false;
}

} // namespace craft
