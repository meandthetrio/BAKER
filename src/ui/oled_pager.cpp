#include "oled_pager.h"

#include <cstring>
#include "daisy_pod.h"

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

    Fill(false);
    initialized_  = true;
    transferring_ = false;
    page_idx_     = 0;
    last_page_ms_ = 0;
}

void OledPager::BeginFrameTransfer()
{
    if(!initialized_)
        return;
    std::memcpy(front_, back_, kBufferSize);
    page_idx_     = 0;
    transferring_ = true;
}

void OledPager::TickTransferOnePage(uint32_t now_ms, bool midi_busy)
{
    if(!initialized_ || !transferring_ || midi_busy)
        return;

    if(last_page_ms_ != 0 && (now_ms - last_page_ms_) < 2)
        return;

    SendCommand(0xB0 + page_idx_);
    SendCommand(0x00);
    SendCommand(0x10);
    SendPageData_(&front_[kWidth * page_idx_]);

    page_idx_++;
    last_page_ms_ = now_ms;
    if(page_idx_ >= kPages)
        transferring_ = false;
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

void OledPager::SendPageData_(const uint8_t* page)
{
    uint8_t tx[kWidth + 1];
    tx[0] = 0x40;
    std::memcpy(&tx[1], page, kWidth);
    i2c_.TransmitBlocking(i2c_address_, tx, kWidth + 1, 1000);
}
