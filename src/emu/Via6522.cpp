#include "Via6522.h"

namespace mirage {

void Via6522::reset()
{
    ora_ = orb_ = ddra_ = ddrb_ = 0;
    acr_ = pcr_ = ifr_ = ier_ = sr_ = 0;
    t1armed_ = t2armed_ = false;
    pb7_ = true;
}

void Via6522::outB()
{
    uint8_t v = portBOut();
    if (acr_ & 0x80) v = uint8_t((v & 0x7F) | (pb7_ ? 0x80 : 0));
    if (onPortB) onPortB(v);
}

uint8_t Via6522::read(int reg)
{
    switch (reg & 0xF) {
    case 0x0: {
        ifr_ &= ~(IFR_CB1 | IFR_CB2);
        uint8_t in = readPortB ? readPortB() : 0xFF;
        uint8_t v = uint8_t((orb_ & ddrb_) | (in & ~ddrb_));
        if (acr_ & 0x80) v = uint8_t((v & 0x7F) | (pb7_ ? 0x80 : 0));
        return v;
    }
    case 0x1: ifr_ &= ~(IFR_CA1 | IFR_CA2); [[fallthrough]];
    case 0xF: {
        uint8_t in = readPortA ? readPortA() : 0xFF;
        return uint8_t((ora_ & ddra_) | (in & ~ddra_));
    }
    case 0x2: return ddrb_;
    case 0x3: return ddra_;
    case 0x4: ifr_ &= ~IFR_T1; return uint8_t(t1_);
    case 0x5: return uint8_t(t1_ >> 8);
    case 0x6: return uint8_t(t1latch_);
    case 0x7: return uint8_t(t1latch_ >> 8);
    case 0x8: ifr_ &= ~IFR_T2; return uint8_t(t2_);
    case 0x9: return uint8_t(t2_ >> 8);
    case 0xA: ifr_ &= ~IFR_SR; return sr_;
    case 0xB: return acr_;
    case 0xC: return pcr_;
    case 0xD: return uint8_t((ifr_ & 0x7F) | (irq() ? 0x80 : 0));
    case 0xE: return uint8_t(ier_ | 0x80);
    }
    return 0xFF;
}

void Via6522::write(int reg, uint8_t v)
{
    switch (reg & 0xF) {
    case 0x0: orb_ = v; ifr_ &= ~(IFR_CB1 | IFR_CB2); outB(); break;
    case 0x1: ifr_ &= ~(IFR_CA1 | IFR_CA2); [[fallthrough]];
    case 0xF: ora_ = v; outA(); break;
    case 0x2: ddrb_ = v; outB(); break;
    case 0x3: ddra_ = v; outA(); break;
    case 0x4: case 0x6: t1latch_ = uint16_t((t1latch_ & 0xFF00) | v); break;
    case 0x7: t1latch_ = uint16_t((t1latch_ & 0x00FF) | (v << 8)); ifr_ &= ~IFR_T1; break;
    case 0x5:
        t1latch_ = uint16_t((t1latch_ & 0x00FF) | (v << 8));
        t1_ = t1latch_ + 1;
        ifr_ &= ~IFR_T1;
        t1armed_ = true;
        if (acr_ & 0x80) { pb7_ = false; outB(); }
        break;
    case 0x8: t2latchLo_ = v; break;
    case 0x9:
        t2_ = (v << 8 | t2latchLo_) + 1;
        ifr_ &= ~IFR_T2;
        t2armed_ = true;
        break;
    case 0xA: sr_ = v; ifr_ &= ~IFR_SR; break;
    case 0xB: acr_ = v; outB(); break;
    case 0xC: pcr_ = v; break;
    case 0xD: ifr_ &= ~(v & 0x7F); break;
    case 0xE:
        if (v & 0x80) ier_ |= (v & 0x7F);
        else ier_ &= ~(v & 0x7F);
        break;
    }
}

void Via6522::tick(int cycles)
{
    // Timer 1
    t1_ -= cycles;
    while (t1_ < 0) {
        if (t1armed_) {
            ifr_ |= IFR_T1;
            if (!(acr_ & 0x40)) t1armed_ = false; // one-shot
        }
        if (acr_ & 0x40) {
            t1_ += t1latch_ + 2;
            if (acr_ & 0x80) pb7_ = !pb7_;
        } else {
            t1_ += 0x10000;
            if ((acr_ & 0x80) && !pb7_) pb7_ = true;
        }
    }
    // Timer 2 (timed mode only; pulse counting on PB6 is unused here)
    if (!(acr_ & 0x20)) {
        t2_ -= cycles;
        while (t2_ < 0) {
            if (t2armed_) { ifr_ |= IFR_T2; t2armed_ = false; }
            t2_ += 0x10000;
        }
    }
}

void Via6522::ca1(bool level)
{
    const bool posEdge = pcr_ & 0x01;
    if (level != ca1_ && level == posEdge) ifr_ |= IFR_CA1;
    ca1_ = level;
}

void Via6522::shiftInByte(uint8_t v)
{
    sr_ = v;
    ifr_ |= IFR_SR;
}

} // namespace mirage
