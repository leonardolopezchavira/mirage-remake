// Motorola 6850 ACIA used for MIDI. Bytes are paced at MIDI speed (31250 baud, 10 bits)
// in CPU cycles. MIT licence.
#pragma once
#include <cstdint>
#include <deque>
#include <functional>

namespace mirage {

class Acia6850 {
public:
    static constexpr int kByteCycles = 640; // 320 us at 2 MHz

    std::function<void(uint8_t)> onTx;      // MIDI out

    void reset() { ctrl_ = 0x03; status_ = 0x02; rxq_.clear(); rxWait_ = 0; txBusy_ = 0; }

    uint8_t read(int reg)
    {
        if (!(reg & 1)) return uint8_t(status_ | (irq() ? 0x80 : 0));
        status_ &= ~0x21; // RDRF, OVRN
        return rdr_;
    }

    void write(int reg, uint8_t v)
    {
        if (!(reg & 1)) {
            ctrl_ = v;
            if ((v & 3) == 3) { status_ = 0x02; txBusy_ = 0; }
            return;
        }
        if (onTx) onTx(v);
        status_ &= ~0x02;
        txBusy_ = kByteCycles;
    }

    void tick(int cycles)
    {
        if (txBusy_ > 0 && (txBusy_ -= cycles) <= 0) status_ |= 0x02;
        if (rxWait_ > 0) rxWait_ -= cycles;
        if (rxWait_ <= 0 && !rxq_.empty() && !(status_ & 0x01) && (ctrl_ & 3) != 3) {
            rdr_ = rxq_.front();
            rxq_.pop_front();
            status_ |= 0x01;
            rxWait_ = kByteCycles;
        }
    }

    void receive(uint8_t b) { rxq_.push_back(b); }
    size_t pending() const { return rxq_.size(); }

    bool irq() const
    {
        if ((ctrl_ & 3) == 3) return false;
        if ((ctrl_ & 0x80) && (status_ & 0x01)) return true;
        if ((ctrl_ & 0x60) == 0x20 && (status_ & 0x02)) return true;
        return false;
    }

private:
    uint8_t ctrl_ = 0x03, status_ = 0x02, rdr_ = 0;
    int rxWait_ = 0, txBusy_ = 0;
    std::deque<uint8_t> rxq_;
};

} // namespace mirage
