// Ensoniq ES5503 "DOC" digital oscillator chip, stepped one frame (all enabled oscillators
// once) at a time. Behaviour follows the ES5503 datasheet and MAME's es5503.cpp (BSD-3-Clause,
// R. Belmont) for the halt / swap / sync details. MIT licence.
#pragma once
#include <cstdint>
#include <functional>

namespace mirage {

class Es5503 {
public:
    static constexpr int kChannels = 8;

    explicit Es5503(const uint8_t* ram) : ram_(ram) {}

    std::function<uint8_t()> readAdc;

    void reset();
    uint8_t read(int reg);
    void write(int reg, uint8_t v);

    // One frame: every enabled oscillator steps once. Fills channel() with the frame's
    // per-channel output (signed sum of data*volume, full scale of one osc = +-32767).
    void frame();
    int oscillatorsEnabled() const { return oscCount_; }
    int frameClocks() const { return (oscCount_ + 2) * 8; } // in DOC master clocks (8 MHz)
    int32_t channel(int c) const { return chan_[c]; }
    bool irq() const { return irqLine_; }

private:
    struct Osc {
        uint16_t freq = 0, wtsize = 256;
        uint8_t control = 1, vol = 0, data = 0x80;
        uint32_t wtptr = 0;   // 17-bit
        uint8_t wtsizeSel = 0, res = 0;
        uint32_t acc = 0;
        bool irqpend = false;
    };
    const uint8_t* ram_;
    Osc osc_[32];
    int oscCount_ = 1;
    uint8_t regE0_ = 0xFF;
    bool irqLine_ = false;
    int32_t chan_[kChannels] = {};

    void haltOsc(int n, bool zeroByte, uint32_t& acc, int resshift, uint8_t newCtrl);
    void updateIrq();
};

} // namespace mirage
