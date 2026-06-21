#include "oled_pager.h"

#include <cstring>
#include "daisy_pod.h"

// Page-data DMA source buffer in the D2 DMA-safe, cacheless section
// (DMA_BUFFER_MEM_SECTION -> .sram1_bss -> RAM_D2_DMA). Cacheless, so the CPU's
// memcpy into it is directly visible to the DMA with no manual cache flush.
uint8_t DMA_BUFFER_MEM_SECTION OledPager::s_dma_buf_[OledPager::kWidth + 1];

void OledPager::Init(daisy::DaisyPod& hw)
{
    daisy::I2CHandle::Config cfg;
    cfg.periph         = daisy::I2CHandle::Config::Peripheral::I2C_1;
    cfg.mode           = daisy::I2CHandle::Config::Mode::I2C_MASTER;
    cfg.speed          = daisy::I2CHandle::Config::Speed::I2C_400KHZ;
    cfg.pin_config.scl = hw.seed.GetPin(11);
    cfg.pin_config.sda = hw.seed.GetPin(12);

    i2c_.Init(cfg);

    SendCommand(0x20); // Set Memory Addressing Mode
    SendCommand(0x02); // Page Addressing Mode

    std::memset(front_, 0x00, kBufferSize);
    Fill(false);
    s_dma_buf_[0] = 0x40; // data-mode control prefix (page bytes filled per kick)
    initialized_  = true;
    display_on_ = true;
    transfer_suppressed_ = false;
    transferring_ = false;
    dma_in_flight_ = false;
    dma_last_error_ = false;
    page_idx_ = 0;
}

void OledPager::BeginFrameTransfer()
{
    if(!initialized_ || transfer_suppressed_)
        return;
    std::memcpy(front_, back_, kBufferSize);
    page_idx_ = 0;
    transferring_ = true;
}

void OledPager::SetDisplayOn(bool on)
{
    if(!initialized_ || display_on_ == on)
        return;

    // Bus must be free before the blocking on/off command (a page DMA may be in
    // flight). Bounded by <3 ms; only hit on this rare control path.
    WaitDmaIdle_();
    SendCommand(on ? 0xAF : 0xAE);
    display_on_ = on;
    if(!on)
    {
        transferring_ = false;
        page_idx_ = 0;
    }
}

void OledPager::SetTransferSuppressed(bool suppressed)
{
    transfer_suppressed_ = suppressed;
    if(suppressed)
    {
        // Safe to cancel mid-DMA: the DMA reads s_dma_buf_ (untouched) and its
        // callback still clears the gate; !transferring_ blocks any new page.
        transferring_ = false;
        page_idx_ = 0;
    }
}

void OledPager::TickTransferOnePage(uint32_t now_ms, bool midi_busy)
{
    (void)now_ms; // throttle dropped: the dma_in_flight_ gate paces pages
    if(!initialized_ || !display_on_ || transfer_suppressed_ || !transferring_)
        return;
    // Previous page still clocking out over DMA -> return and free the main loop
    // (this is the whole point: MIDI gets drained while the page transfers).
    if(dma_in_flight_)
        return;
    // Don't START a new page mid-MIDI burst (the ~150 us of address commands
    // below are still blocking). An already-in-flight DMA keeps draining though.
    if(midi_busy)
        return;
    if(page_idx_ >= kPages)
    {
        transferring_ = false;
        return;
    }

    // Set this page's address (blocking, ~150 us total), then kick its data DMA.
    SendCommand(0xB0 + page_idx_);
    SendCommand(0x00);
    SendCommand(0x10);
    SendPageData_(&front_[kWidth * page_idx_]); // sets dma_in_flight_ (or clears on err)

    page_idx_++;
    if(page_idx_ >= kPages)
        transferring_ = false; // cleared when the LAST page is kicked (DMA reads s_dma_buf_)
}

void OledPager::Fill(bool on)
{
    std::memset(back_, on ? 0xFF : 0x00, kBufferSize);
}

void OledPager::DrawPixel(uint_fast8_t x, uint_fast8_t y, bool on)
{
    if(x >= kWidth || y >= kHeight)
        return;

    const size_t index = x + (y / 8) * kWidth;
    const uint8_t mask = 1 << (y % 8);
    if(on)
        back_[index] |= mask;
    else
        back_[index] &= ~mask;
}

void OledPager::SendCommand(uint8_t cmd)
{
    uint8_t buf[2] = {0x00, cmd};
    i2c_.TransmitBlocking(i2c_address_, buf, 2, 1000);
}

bool OledPager::SendPageData_(const uint8_t* page)
{
    // s_dma_buf_[0] is the 0x40 data-mode prefix (set in Init).
    std::memcpy(&s_dma_buf_[1], page, kWidth);
    dma_last_error_ = false;
    dma_in_flight_  = true; // set BEFORE the kick so the ISR callback can't race-clear
    const auto res = i2c_.TransmitDma(
        i2c_address_, s_dma_buf_, kWidth + 1, &OledPager::DmaCompleteCallback_, this);
    if(res != daisy::I2CHandle::Result::OK)
    {
        // No callback will fire on a failed kick -> clear the gate so the tick
        // (and any WaitDmaIdle_) can't deadlock. The page is effectively dropped;
        // the next frame redraws it.
        dma_in_flight_  = false;
        dma_last_error_ = true;
        return false;
    }
    return true;
}

void OledPager::DmaCompleteCallback_(void* context, daisy::I2CHandle::Result result)
{
    // ISR context (DMA1_Stream6), IRQs blocked. Minimal: record + clear the gate.
    auto* self = static_cast<OledPager*>(context);
    self->dma_last_error_ = (result != daisy::I2CHandle::Result::OK);
    self->dma_in_flight_  = false;
}

void OledPager::WaitDmaIdle_()
{
    // The gate is cleared either by the completion ISR or synchronously by a
    // failed kick, so this can't spin forever.
    while(dma_in_flight_)
    {
    }
}

void OledPager::FlushFrameBlocking()
{
    // Synchronous full-frame drain for already-blocking contexts (SD splash).
    // Keeps the normal 60 Hz path async; here we intentionally block per page.
    if(!initialized_ || !display_on_ || transfer_suppressed_)
        return;
    WaitDmaIdle_(); // don't collide with an async page already in flight
    while(transferring_)
    {
        if(page_idx_ >= kPages)
        {
            transferring_ = false;
            break;
        }
        SendCommand(0xB0 + page_idx_);
        SendCommand(0x00);
        SendCommand(0x10);
        SendPageData_(&front_[kWidth * page_idx_]);
        WaitDmaIdle_(); // wait this page out -> synchronous semantics
        page_idx_++;
        if(page_idx_ >= kPages)
            transferring_ = false;
    }
}
