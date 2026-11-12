// MOS 6522 VIA — the subset the Mirage uses (ports, T1 free-run, T2 one-shot, shift register,
// CA1/CB1 edges, interrupt flags). Clocked at the CPU's E clock. MIT licence.
#pragma once
#include <cstdint>
#include <functional>

namespace mirage {

class Via6522 {
public:
    enum : uint8_t { IFR_CA2 = 0x01, IFR_CA1 = 0x02, IFR_SR = 0x04, IFR_CB2 = 0x08,
                     IFR_CB1 = 0x10, IFR_T2 = 0x20, IFR_T1 = 0x40, IFR_ANY = 0x80 };

    std::function<void(uint8_t)> onPortA;   // output values (ORA with DDRA applied, inputs as 1)
    std::function<void(uint8_t)> onPortB;
    std::function<uint8_t()> readPortA;     // external levels on PA pins
    std::function<uint8_t()> readPortB;

    void reset();
    uint8_t read(int reg);
    void write(int reg, uint8_t v);
    void tick(int cycles);
    bool irq() const { return (ifr_ & ier_ & 0x7F) != 0; }

    // External events
    void ca1(bool level);
    void shiftInByte(uint8_t v);            // a complete byte clocked in by CB1 (keyboard)

    uint8_t portBOut() const { return uint8_t((orb_ & ddrb_) | ~ddrb_); }
    uint8_t portAOut() const { return uint8_t((ora_ & ddra_) | ~ddra_); }
    bool pb7() const { return pb7_; }

private:
    uint8_t ora_ = 0, orb_ = 0, ddra_ = 0, ddrb_ = 0;
    uint8_t acr_ = 0, pcr_ = 0, ifr_ = 0, ier_ = 0, sr_ = 0;
    uint16_t t1latch_ = 0xFFFF, t2latchLo_ = 0xFF;
    int32_t t1_ = 0xFFFF, t2_ = 0xFFFF;
    bool t1armed_ = false, t2armed_ = false, pb7_ = true, ca1_ = true;

    void outA() { if (onPortA) onPortA(portAOut()); }
    void outB();
};

} // namespace mirage
