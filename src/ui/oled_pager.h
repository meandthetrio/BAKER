#pragma once

#include <cstddef>
#include <cstdint>

#include "hid/disp/display.h"
#include "per/i2c.h"

namespace daisy
{
class DaisyPod;
}

class OledPager
    : public daisy::OneBitGraphicsDisplayImpl<OledPager>
{
  public:
    void Init(daisy::DaisyPod& hw);
    void BeginFrameTransfer();
    bool IsTransferring() const { return transferring_; }
    bool IsReady() const { return initialized_; }
    void TickTransferOnePage(uint32_t now_ms, bool midi_busy);

    uint16_t Width() const override { return kWidth; }
    uint16_t Height() const override { return kHeight; }
    void Fill(bool on) override;
    void DrawPixel(uint_fast8_t x, uint_fast8_t y, bool on) override;
    void Update() override {}

  private:
    static constexpr uint16_t kWidth      = 128;
    static constexpr uint16_t kHeight     = 64;
    static constexpr uint8_t  kPages      = kHeight / 8;
    static constexpr size_t   kBufferSize = (kWidth * kHeight) / 8;

    void SendCommand(uint8_t cmd);
    void SendPageData_(const uint8_t* page);

    daisy::I2CHandle i2c_;
    uint8_t          i2c_address_ = 0x3C;
    uint32_t         last_page_ms_ = 0;
    bool             initialized_  = false;
    bool        transferring_ = false;
    uint8_t     page_idx_     = 0;
    uint8_t     front_[kBufferSize];
    uint8_t     back_[kBufferSize];
};
