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
    bool IsDisplayOn() const { return display_on_; }
    bool IsTransferSuppressed() const { return transfer_suppressed_; }
    void SetDisplayOn(bool on);
    void SetTransferSuppressed(bool suppressed);
    void TickTransferOnePage(uint32_t now_ms, bool midi_busy);
    // Synchronous full-frame drain for already-blocking contexts (e.g. the SD
    // save/load splash). The normal 60 Hz path is async via TickTransferOnePage.
    void FlushFrameBlocking();

    uint16_t Width() const override { return kWidth; }
    uint16_t Height() const override { return kHeight; }
    void Fill(bool on) override;
    void DrawPixel(uint_fast8_t x, uint_fast8_t y, bool on) override;
    // Read a pixel back from the working (back) buffer — the same buffer DrawPixel
    // writes to. Enables XOR/invert drawing against already-composed content.
    bool GetPixel(uint_fast8_t x, uint_fast8_t y) const;
    void Update() override {}
    bool UpdateFinished() override { return !transferring_; }

  private:
    static constexpr uint16_t kWidth      = 128;
    static constexpr uint16_t kHeight     = 64;
    static constexpr uint8_t  kPages      = kHeight / 8;
    static constexpr size_t   kBufferSize = (kWidth * kHeight) / 8;

    void SendCommand(uint8_t cmd);
    // Kicks the page-data DMA. Returns true if the transfer was queued (the
    // completion callback will clear dma_in_flight_); false on immediate error.
    bool SendPageData_(const uint8_t* page);
    // Spin until the in-flight page DMA completes. Used only by the synchronous
    // flush + the cancel paths — never on the normal async tick.
    void WaitDmaIdle_();
    // I2C DMA completion callback (runs in DMA1_Stream6 ISR context). Minimal:
    // clears dma_in_flight_ and records the result. context = the OledPager.
    static void DmaCompleteCallback_(void* context, daisy::I2CHandle::Result result);

    daisy::I2CHandle i2c_;
    uint8_t          i2c_address_ = 0x3C;
    bool             initialized_ = false;
    bool             display_on_ = true;
    bool             transfer_suppressed_ = false;
    bool             transferring_ = false;
    // Set by the main loop just before kicking a page DMA, cleared by the ISR
    // callback. volatile: written in ISR, read in the main loop.
    volatile bool    dma_in_flight_ = false;
    volatile bool    dma_last_error_ = false;
    uint8_t          page_idx_ = 0;
    uint8_t          front_[kBufferSize];
    uint8_t          back_[kBufferSize];
    // Page-data DMA source buffer (0x40 control prefix + one 128-byte page).
    // Lives in the D2 DMA-safe section (defined in the .cpp). Single buffer is
    // safe because the dma_in_flight_ gate never lets it be rewritten mid-flight.
    static uint8_t   s_dma_buf_[kWidth + 1];
};
