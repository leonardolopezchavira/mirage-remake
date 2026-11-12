// Motorola MC6809 / MC6809E CPU core.
// Cycle-counted at instruction granularity (datasheet timings), with NMI, FIRQ, IRQ,
// SWI/SWI2/SWI3, CWAI and SYNC.
// MIT licence (new code for this project).
#pragma once
#include <cstdint>

namespace mirage {

class Bus6809 {
public:
    virtual ~Bus6809() = default;
    virtual uint8_t read(uint16_t addr) = 0;
    virtual void write(uint16_t addr, uint8_t v) = 0;
};

class Cpu6809 {
public:
    enum : uint8_t { CC_C = 0x01, CC_V = 0x02, CC_Z = 0x04, CC_N = 0x08,
                     CC_I = 0x10, CC_H = 0x20, CC_F = 0x40, CC_E = 0x80 };

    explicit Cpu6809(Bus6809& bus) : bus_(bus) {}

    void reset();
    // Executes one instruction (or takes one interrupt); returns cycles used.
    int step();

    // Interrupt lines. IRQ/FIRQ are level-sensitive; NMI is edge-triggered.
    void setIrq(bool on) { irq_ = on; }
    void setFirq(bool on) { firq_ = on; }
    void setNmi(bool on) { if (on && !nmiLine_) nmiPending_ = true; nmiLine_ = on; }

    // Registers (public for debugging / tests).
    uint8_t a = 0, b = 0, dp = 0, cc = CC_I | CC_F;
    uint16_t x = 0, y = 0, u = 0, s = 0, pc = 0;

    uint16_t d() const { return uint16_t(a << 8 | b); }
    void setD(uint16_t v) { a = uint8_t(v >> 8); b = uint8_t(v); }

    uint64_t totalCycles = 0;
    bool illegal = false;   // set when an undefined opcode was hit (debug aid)
    uint16_t illegalPc = 0;

private:
    Bus6809& bus_;
    bool irq_ = false, firq_ = false, nmiLine_ = false, nmiPending_ = false;
    bool nmiArmed_ = false;     // NMI is disabled until S has been loaded
    bool cwai_ = false, sync_ = false;
    int extra_ = 0;             // extra cycles from indexed addressing

    uint8_t rd(uint16_t a) { return bus_.read(a); }
    void wr(uint16_t a, uint8_t v) { bus_.write(a, v); }
    uint16_t rd16(uint16_t a) { return uint16_t(rd(a) << 8 | rd(uint16_t(a + 1))); }
    void wr16(uint16_t a, uint16_t v) { wr(a, uint8_t(v >> 8)); wr(uint16_t(a + 1), uint8_t(v)); }
    uint8_t fetch() { return rd(pc++); }
    uint16_t fetch16() { uint16_t v = rd16(pc); pc += 2; return v; }

    void pushS(uint8_t v) { wr(--s, v); }
    void pushS16(uint16_t v) { pushS(uint8_t(v)); pushS(uint8_t(v >> 8)); }
    uint8_t pullS() { return rd(s++); }
    uint16_t pullS16() { uint16_t h = pullS(); return uint16_t(h << 8 | pullS()); }
    void pushU(uint8_t v) { wr(--u, v); }
    void pushU16(uint16_t v) { pushU(uint8_t(v)); pushU(uint8_t(v >> 8)); }
    uint8_t pullU() { return rd(u++); }
    uint16_t pullU16() { uint16_t h = pullU(); return uint16_t(h << 8 | pullU()); }

    uint16_t eaDirect() { return uint16_t(dp << 8 | fetch()); }
    uint16_t eaExtended() { return fetch16(); }
    uint16_t eaIndexed();
    uint16_t& idxReg(int r);

    void pushAll();             // E must already be set appropriately
    int takeInterrupts();

    // flag helpers
    void nz8(uint8_t r) { cc &= ~(CC_N | CC_Z); if (r & 0x80) cc |= CC_N; if (!r) cc |= CC_Z; }
    void nz16(uint16_t r) { cc &= ~(CC_N | CC_Z); if (r & 0x8000) cc |= CC_N; if (!r) cc |= CC_Z; }
    uint8_t add8(uint8_t a, uint8_t b, int c);
    uint8_t sub8(uint8_t a, uint8_t b, int c);
    uint16_t add16(uint16_t a, uint16_t b);
    uint16_t sub16(uint16_t a, uint16_t b);
    uint8_t rmw(int op, uint8_t v);   // NEG..CLR family by low nibble

    uint16_t getReg(int code);
    void setReg(int code, uint16_t v);
    int psh(bool useU, uint8_t mask);
    int pul(bool useU, uint8_t mask);

    int exec0(uint8_t op);
    int exec10(uint8_t op);
    int exec11(uint8_t op);
    bool cond(int c) const;
};

} // namespace mirage
