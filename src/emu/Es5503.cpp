#include "Es5503.h"
#include <cstdio>
#include <cstdlib>

namespace mirage {

namespace {
constexpr uint16_t kWaveSizes[8] = { 256, 512, 1024, 2048, 4096, 8192, 16384, 32768 };
constexpr uint32_t kWaveMasks[8] = { 0x1FF00, 0x1FE00, 0x1FC00, 0x1F800, 0x1F000, 0x1E000, 0x1C000, 0x18000 };
constexpr uint32_t kAccMasks[8]  = { 0xFF, 0x1FF, 0x3FF, 0x7FF, 0xFFF, 0x1FFF, 0x3FFF, 0x7FFF };
constexpr int      kResShifts[8] = { 9, 10, 11, 12, 13, 14, 15, 16 };
enum { MODE_FREE = 0, MODE_ONESHOT = 1, MODE_SYNCAM = 2, MODE_SWAP = 3 };
}

void Es5503::reset()
{
    for (auto& o : osc_) o = Osc{};
    oscCount_ = 1;
    regE0_ = 0xFF;
    irqLine_ = false;
    for (auto& c : chan_) c = 0;
}

void Es5503::updateIrq()
{
    irqLine_ = false;
    for (int i = 0; i < oscCount_; ++i)
        if (osc_[i].irqpend) { irqLine_ = true; break; }
}

void Es5503::haltOsc(int n, bool zeroByte, uint32_t& acc, int resshift, uint8_t newCtrl)
{
    Osc& o = osc_[n];
    Osc& partner = osc_[n ^ 1];
    int mode = (o.control >> 1) & 3;
    const int partnerMode = (partner.control >> 1) & 3;

    if (mode == MODE_SYNCAM) {
        if (!(n & 1) && n > 0 && !(osc_[n - 1].control & 1)) osc_[n - 1].acc = 0;
        mode = MODE_FREE;
    }
    if (mode != MODE_FREE || zeroByte) o.control |= 1;
    else acc -= uint32_t(o.wtsize - 1) << resshift;

    if (mode == MODE_SWAP) {
        partner.control &= ~1;
        partner.acc = 0;
    } else if (partnerMode == MODE_SWAP && !(n & 1) && (o.control & 1)) {
        // An even oscillator that just halted, whose partner is in swap mode, retriggers
        // (MAME, verified on IIgs hardware). Only when it halted: a free-running oscillator
        // has already wrapped above, and rewinding it a second time made every loop
        // ~1.5x too long on the Mirage (the A2 clavinet's loop fell by a fifth).
        o.control &= ~1;
        acc -= uint32_t(o.wtsize - 1) << resshift;
    }

    if (o.control & 0x08) {
        if (!zeroByte && !(newCtrl & 0x08)) return;
        o.irqpend = true;
        irqLine_ = true;
    }
}

void Es5503::frame()
{
    for (auto& c : chan_) c = 0;
    for (int n = 0; n < oscCount_; ++n) {
        Osc& o = osc_[n];
        if (o.control & 1) continue;
        const uint32_t wtptr = o.wtptr & kWaveMasks[o.wtsizeSel];
        const int resshift = kResShifts[o.res] - o.wtsizeSel;
        const uint32_t sizemask = kAccMasks[o.wtsizeSel];
        const int mode = (o.control >> 1) & 3;
        uint32_t acc = o.acc;

        const uint32_t altram = acc >> resshift;
        const uint32_t addr = ((altram & sizemask) + wtptr) & 0x1FFFF;
        acc = (acc + o.freq) & 0xFFFFFF;
        const uint8_t raw = ram_[addr];
        if (raw == 0x00) {
            haltOsc(n, true, acc, resshift, o.control);
        } else {
            o.data = raw;
            const int32_t s = int32_t(int8_t(raw ^ 0x80)) * o.vol;
            if (mode != MODE_SYNCAM) {
                chan_[(o.control >> 4) & 7] += s;
            } else if (n & 1) {
                if (n < 31 && !(osc_[n + 1].control & 1)) osc_[n + 1].vol = raw;
            } else {
                chan_[(o.control >> 4) & 7] += s;
            }
            if (altram >= uint32_t(o.wtsize - 1)) haltOsc(n, false, acc, resshift, o.control);
        }
        o.acc = acc;
    }
}

uint8_t Es5503::read(int reg)
{
    reg &= 0xFF;
    if (reg < 0xE0) {
        const Osc& o = osc_[reg & 0x1F];
        switch (reg & 0xE0) {
        case 0x00: return uint8_t(o.freq);
        case 0x20: return uint8_t(o.freq >> 8);
        case 0x40: return o.vol;
        case 0x60: return o.data;
        case 0x80: return uint8_t(o.wtptr >> 8);
        case 0xA0: return o.control;
        case 0xC0: return uint8_t(((o.wtptr & 0x10000) ? 0x40 : 0) | (o.wtsizeSel << 3) | o.res);
        }
        return 0;
    }
    switch (reg) {
    case 0xE0: {
        uint8_t ret = regE0_;
        irqLine_ = false;
        for (int i = 0; i < oscCount_; ++i) {
            if (osc_[i].irqpend) {
                ret = uint8_t(i << 1);
                regE0_ = uint8_t(ret | 0x80);
                osc_[i].irqpend = false;
                break;
            }
        }
        updateIrq();
        return uint8_t(ret | 0x41);
    }
    case 0xE1: return uint8_t((oscCount_ - 1) << 1);
    case 0xE2: return readAdc ? readAdc() : 0x80;
    }
    return 0;
}

void Es5503::write(int reg, uint8_t v)
{
    reg &= 0xFF;
    static const int traceOsc = std::getenv("MIRAGE_DOC_TRACE") ? std::atoi(std::getenv("MIRAGE_DOC_TRACE")) : -1;
    if (traceOsc >= 0 && reg < 0xE0 && (reg & 0x1F) <= traceOsc + 1 && (reg & 0x1F) >= traceOsc && ((reg & 0xE0) >= 0x80 || std::getenv("MIRAGE_DOC_FREQ")))
        std::fprintf(stderr, "doc w %02X=%02X\n", reg, v);
    if (reg < 0xE0) {
        const int n = reg & 0x1F;
        Osc& o = osc_[n];
        switch (reg & 0xE0) {
        case 0x00: o.freq = uint16_t((o.freq & 0xFF00) | v); break;
        case 0x20: o.freq = uint16_t((o.freq & 0x00FF) | (v << 8)); break;
        case 0x40: o.vol = v; break;
        case 0x60: break;
        case 0x80: o.wtptr = (o.wtptr & 0x10000) | uint32_t(v << 8); break;
        case 0xA0:
            if ((o.control & 1) && !(v & 1)) o.acc = 0;
            if (!(o.control & 1) && (v & 1) && ((v >> 1) & 1))
                haltOsc(n, false, o.acc, kResShifts[o.res], v);
            o.control = v;
            break;
        case 0xC0:
            if (v & 0x40) o.wtptr |= 0x10000; else o.wtptr &= 0xFFFF;
            o.wtsizeSel = (v >> 3) & 7;
            o.wtsize = kWaveSizes[o.wtsizeSel];
            o.res = v & 7;
            break;
        }
        return;
    }
    if (reg == 0xE1) oscCount_ = ((v >> 1) & 0x1F) + 1;
}

} // namespace mirage
