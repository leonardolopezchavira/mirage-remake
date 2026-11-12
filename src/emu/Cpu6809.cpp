// Motorola MC6809 CPU core. MIT licence.
#include "Cpu6809.h"

namespace mirage {

void Cpu6809::reset()
{
    dp = 0;
    cc = CC_I | CC_F;
    cwai_ = sync_ = false;
    nmiArmed_ = false;
    nmiPending_ = false;
    illegal = false;
    pc = rd16(0xFFFE);
}

// ---------------------------------------------------------------- arithmetic helpers
uint8_t Cpu6809::add8(uint8_t x8, uint8_t y8, int c)
{
    unsigned r = unsigned(x8) + y8 + c;
    cc &= ~(CC_H | CC_N | CC_Z | CC_V | CC_C);
    if (((x8 & 0x0F) + (y8 & 0x0F) + c) & 0x10) cc |= CC_H;
    if (r & 0x100) cc |= CC_C;
    if (~(x8 ^ y8) & (x8 ^ r) & 0x80) cc |= CC_V;
    nz8(uint8_t(r));
    return uint8_t(r);
}

uint8_t Cpu6809::sub8(uint8_t x8, uint8_t y8, int c)
{
    unsigned r = unsigned(x8) - y8 - c;
    cc &= ~(CC_N | CC_Z | CC_V | CC_C);
    if (r & 0x100) cc |= CC_C;
    if ((x8 ^ y8) & (x8 ^ r) & 0x80) cc |= CC_V;
    nz8(uint8_t(r));
    return uint8_t(r);
}

uint16_t Cpu6809::add16(uint16_t x16, uint16_t y16)
{
    uint32_t r = uint32_t(x16) + y16;
    cc &= ~(CC_N | CC_Z | CC_V | CC_C);
    if (r & 0x10000) cc |= CC_C;
    if (~(x16 ^ y16) & (x16 ^ r) & 0x8000) cc |= CC_V;
    nz16(uint16_t(r));
    return uint16_t(r);
}

uint16_t Cpu6809::sub16(uint16_t x16, uint16_t y16)
{
    uint32_t r = uint32_t(x16) - y16;
    cc &= ~(CC_N | CC_Z | CC_V | CC_C);
    if (r & 0x10000) cc |= CC_C;
    if ((x16 ^ y16) & (x16 ^ r) & 0x8000) cc |= CC_V;
    nz16(uint16_t(r));
    return uint16_t(r);
}

// Read-modify-write family selected by the opcode's low nibble.
uint8_t Cpu6809::rmw(int op, uint8_t v)
{
    uint8_t r = v;
    switch (op & 0x0F) {
    case 0x0: case 0x1: // NEG
        r = uint8_t(0 - v);
        cc &= ~(CC_N | CC_Z | CC_V | CC_C);
        if (v == 0x80) cc |= CC_V;
        if (v != 0) cc |= CC_C;
        nz8(r);
        break;
    case 0x2: // undocumented: NEG if C clear, else COM
        if (!(cc & CC_C)) return rmw(0x0, v);
        [[fallthrough]];
    case 0x3: // COM
        r = uint8_t(~v);
        cc &= ~(CC_V);
        cc |= CC_C;
        nz8(r);
        break;
    case 0x4: case 0x5: // LSR
        cc &= ~(CC_N | CC_Z | CC_C);
        if (v & 1) cc |= CC_C;
        r = uint8_t(v >> 1);
        if (!r) cc |= CC_Z;
        break;
    case 0x6: { // ROR
        uint8_t c = (cc & CC_C) ? 0x80 : 0;
        cc &= ~CC_C;
        if (v & 1) cc |= CC_C;
        r = uint8_t((v >> 1) | c);
        nz8(r);
        break;
    }
    case 0x7: // ASR
        cc &= ~CC_C;
        if (v & 1) cc |= CC_C;
        r = uint8_t((v >> 1) | (v & 0x80));
        nz8(r);
        break;
    case 0x8: // ASL/LSL
        r = uint8_t(v << 1);
        cc &= ~(CC_V | CC_C);
        if (v & 0x80) cc |= CC_C;
        if ((v ^ (v << 1)) & 0x80) cc |= CC_V;
        nz8(r);
        break;
    case 0x9: { // ROL
        uint8_t c = (cc & CC_C) ? 1 : 0;
        r = uint8_t((v << 1) | c);
        cc &= ~(CC_V | CC_C);
        if (v & 0x80) cc |= CC_C;
        if ((v ^ (v << 1)) & 0x80) cc |= CC_V;
        nz8(r);
        break;
    }
    case 0xA: case 0xB: // DEC
        r = uint8_t(v - 1);
        cc &= ~CC_V;
        if (v == 0x80) cc |= CC_V;
        nz8(r);
        break;
    case 0xC: // INC
        r = uint8_t(v + 1);
        cc &= ~CC_V;
        if (v == 0x7F) cc |= CC_V;
        nz8(r);
        break;
    case 0xD: // TST
        cc &= ~CC_V;
        nz8(v);
        break;
    case 0xF: // CLR
        r = 0;
        cc &= ~(CC_N | CC_V | CC_C);
        cc |= CC_Z;
        break;
    default: break; // 0xE is JMP, handled by caller
    }
    return r;
}

bool Cpu6809::cond(int c) const
{
    const bool C = cc & CC_C, Z = cc & CC_Z, N = cc & CC_N, V = cc & CC_V;
    switch (c & 0xF) {
    case 0x0: return true;
    case 0x1: return false;
    case 0x2: return !(C || Z);
    case 0x3: return C || Z;
    case 0x4: return !C;
    case 0x5: return C;
    case 0x6: return !Z;
    case 0x7: return Z;
    case 0x8: return !V;
    case 0x9: return V;
    case 0xA: return !N;
    case 0xB: return N;
    case 0xC: return N == V;
    case 0xD: return N != V;
    case 0xE: return !Z && (N == V);
    default:  return Z || (N != V);
    }
}

// ---------------------------------------------------------------- indexed addressing
uint16_t& Cpu6809::idxReg(int r)
{
    switch (r & 3) {
    case 0: return x;
    case 1: return y;
    case 2: return u;
    default: return s;
    }
}

uint16_t Cpu6809::eaIndexed()
{
    const uint8_t pb = fetch();
    uint16_t& R = idxReg(pb >> 5);
    if (!(pb & 0x80)) { // 5-bit offset
        int off = pb & 0x1F;
        if (off & 0x10) off -= 0x20;
        extra_ += 1;
        return uint16_t(R + off);
    }
    uint16_t ea = 0;
    const bool ind = pb & 0x10;
    switch (pb & 0x0F) {
    case 0x0: ea = R; R++; extra_ += 2; break;
    case 0x1: ea = R; R += 2; extra_ += 3; break;
    case 0x2: R--; ea = R; extra_ += 2; break;
    case 0x3: R -= 2; ea = R; extra_ += 3; break;
    case 0x4: ea = R; break;
    case 0x5: ea = uint16_t(R + int8_t(b)); extra_ += 1; break;
    case 0x6: ea = uint16_t(R + int8_t(a)); extra_ += 1; break;
    case 0x8: { int8_t o = int8_t(fetch()); ea = uint16_t(R + o); extra_ += 1; break; }
    case 0x9: { uint16_t o = fetch16(); ea = uint16_t(R + o); extra_ += 4; break; }
    case 0xB: ea = uint16_t(R + d()); extra_ += 4; break;
    case 0xC: { int8_t o = int8_t(fetch()); ea = uint16_t(pc + o); extra_ += 1; break; }
    case 0xD: { uint16_t o = fetch16(); ea = uint16_t(pc + o); extra_ += 5; break; }
    case 0xF: ea = fetch16(); extra_ += 2; break; // [n16] (+3 for indirect below = 5)
    default: // 0x7, 0xA, 0xE: undefined
        ea = R; break;
    }
    if (ind) {
        ea = rd16(ea);
        extra_ += 3;
    }
    return ea;
}

// ---------------------------------------------------------------- register transfer
uint16_t Cpu6809::getReg(int code)
{
    switch (code & 0xF) {
    case 0x0: return d();
    case 0x1: return x;
    case 0x2: return y;
    case 0x3: return u;
    case 0x4: return s;
    case 0x5: return pc;
    case 0x8: return uint16_t(0xFF00 | a);
    case 0x9: return uint16_t(0xFF00 | b);
    case 0xA: return uint16_t(0xFF00 | cc);
    case 0xB: return uint16_t(0xFF00 | dp);
    default:  return 0xFFFF;
    }
}

void Cpu6809::setReg(int code, uint16_t v)
{
    switch (code & 0xF) {
    case 0x0: setD(v); break;
    case 0x1: x = v; break;
    case 0x2: y = v; break;
    case 0x3: u = v; break;
    case 0x4: s = v; nmiArmed_ = true; break;
    case 0x5: pc = v; break;
    case 0x8: a = uint8_t(v); break;
    case 0x9: b = uint8_t(v); break;
    case 0xA: cc = uint8_t(v); break;
    case 0xB: dp = uint8_t(v); break;
    default: break;
    }
}

int Cpu6809::psh(bool useU, uint8_t m)
{
    int n = 0;
    auto p8 = [&](uint8_t v) { if (useU) pushU(v); else pushS(v); n++; };
    auto p16 = [&](uint16_t v) { p8(uint8_t(v)); p8(uint8_t(v >> 8)); };
    if (m & 0x80) p16(pc);
    if (m & 0x40) p16(useU ? s : u);
    if (m & 0x20) p16(y);
    if (m & 0x10) p16(x);
    if (m & 0x08) p8(dp);
    if (m & 0x04) p8(b);
    if (m & 0x02) p8(a);
    if (m & 0x01) p8(cc);
    return n;
}

int Cpu6809::pul(bool useU, uint8_t m)
{
    int n = 0;
    auto p8 = [&]() -> uint8_t { n++; return useU ? pullU() : pullS(); };
    auto p16 = [&]() -> uint16_t { uint16_t h = p8(); return uint16_t(h << 8 | p8()); };
    if (m & 0x01) cc = p8();
    if (m & 0x02) a = p8();
    if (m & 0x04) b = p8();
    if (m & 0x08) dp = p8();
    if (m & 0x10) x = p16();
    if (m & 0x20) y = p16();
    if (m & 0x40) { if (useU) { s = p16(); nmiArmed_ = true; } else u = p16(); }
    if (m & 0x80) pc = p16();
    return n;
}

void Cpu6809::pushAll()
{
    pushS16(pc);
    pushS16(u);
    pushS16(y);
    pushS16(x);
    pushS(dp);
    pushS(b);
    pushS(a);
    pushS(cc);
}

int Cpu6809::takeInterrupts()
{
    if (nmiPending_ && nmiArmed_) {
        nmiPending_ = false;
        int cyc = 19;
        if (!cwai_) { cc |= CC_E; pushAll(); } else cyc = 7;
        cwai_ = sync_ = false;
        cc |= CC_I | CC_F;
        pc = rd16(0xFFFC);
        return cyc;
    }
    if (firq_ && !(cc & CC_F)) {
        int cyc = 10;
        if (!cwai_) {
            cc &= ~CC_E;
            pushS16(pc);
            pushS(cc);
        } else cyc = 7;
        cwai_ = sync_ = false;
        cc |= CC_I | CC_F;
        pc = rd16(0xFFF6);
        return cyc;
    }
    if (irq_ && !(cc & CC_I)) {
        int cyc = 19;
        if (!cwai_) { cc |= CC_E; pushAll(); } else cyc = 7;
        cwai_ = sync_ = false;
        cc |= CC_I;
        pc = rd16(0xFFF8);
        return cyc;
    }
    return 0;
}

// ---------------------------------------------------------------- step
int Cpu6809::step()
{
    int cyc;
    if (sync_) {
        // SYNC: wait for any interrupt line; a masked one just resumes execution.
        if (nmiPending_ || firq_ || irq_) {
            sync_ = false;
            if ((cyc = takeInterrupts())) { totalCycles += cyc; return cyc; }
        } else { totalCycles += 1; return 1; }
    }
    if ((cyc = takeInterrupts())) { totalCycles += cyc; return cyc; }
    if (cwai_) { totalCycles += 1; return 1; }

    extra_ = 0;
    const uint8_t op = fetch();
    if (op == 0x10) cyc = exec10(fetch());
    else if (op == 0x11) cyc = exec11(fetch());
    else cyc = exec0(op);
    cyc += extra_;
    totalCycles += cyc;
    return cyc;
}

// Base cycle counts for page-0 opcodes (indexed modes add extra_). 0 = handled specially.
static const uint8_t kCycles0[256] = {
 // 0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 3, 6, // 0x
    0, 0, 2, 4, 2, 2, 5, 9, 2, 2, 3, 2, 3, 2, 8, 6, // 1x
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, // 2x
    4, 4, 4, 4, 5, 5, 5, 5, 2, 5, 3, 6,20,11, 2,19, // 3x
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, // 4x
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, // 5x
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 3, 6, // 6x
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 4, 7, // 7x
    2, 2, 2, 4, 2, 2, 2, 2, 2, 2, 2, 2, 4, 7, 3, 2, // 8x
    4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 6, 7, 5, 5, // 9x
    4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 6, 7, 5, 5, // Ax
    5, 5, 5, 7, 5, 5, 5, 5, 5, 5, 5, 5, 7, 8, 6, 6, // Bx
    2, 2, 2, 4, 2, 2, 2, 2, 2, 2, 2, 2, 3, 2, 3, 2, // Cx
    4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, // Dx
    4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, // Ex
    5, 5, 5, 7, 5, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, // Fx
};

int Cpu6809::exec0(uint8_t op)
{
    int cyc = kCycles0[op];
    const int hi = op >> 4;

    // ---- read-modify-write memory / accumulator group (0x, 4x, 5x, 6x, 7x)
    if (hi == 0x0 || hi == 0x6 || hi == 0x7) {
        uint16_t ea = hi == 0 ? eaDirect() : hi == 6 ? eaIndexed() : eaExtended();
        if ((op & 0xF) == 0xE) { pc = ea; return cyc; }       // JMP
        uint8_t v = rd(ea);
        uint8_t r = rmw(op, v);
        if ((op & 0xF) != 0xD) wr(ea, r);                       // TST doesn't write
        return cyc;
    }
    if (hi == 0x4) { if ((op & 0xF) != 0xE) a = rmw(op, a); return cyc; }
    if (hi == 0x5) { if ((op & 0xF) != 0xE) b = rmw(op, b); return cyc; }

    // ---- branches
    if (hi == 0x2) {
        int8_t off = int8_t(fetch());
        if (cond(op)) pc = uint16_t(pc + off);
        return cyc;
    }

    // ---- 8x..Fx: accumulator / 16-bit ops
    if (hi >= 0x8) {
        const bool isB = hi >= 0xC;
        const int mode = hi & 3; // 0 imm, 1 direct, 2 indexed, 3 extended
        const int lo = op & 0xF;
        uint8_t& acc = isB ? b : a;

        // 16-bit operand opcodes: x3 (SUBD/ADDD), xC (CMPX / LDD), xE (LDX/LDU), (CC/CE imm)
        const bool wide = lo == 0x3 || lo == 0xC || lo == 0xE || (isB && lo == 0xD && mode != 0) ||
                          lo == 0xF;
        auto ea = [&]() -> uint16_t {
            switch (mode) {
            case 1: return eaDirect();
            case 2: return eaIndexed();
            default: return eaExtended();
            }
        };

        // Stores and jumps (no immediate)
        if (!isB && lo == 0x7) { if (mode == 0) { illegal = true; illegalPc = uint16_t(pc - 1); return 2; }
                                 uint16_t e = ea(); wr(e, a); cc &= ~CC_V; nz8(a); return cyc; }
        if (isB && lo == 0x7)  { if (mode == 0) { illegal = true; illegalPc = uint16_t(pc - 1); return 2; }
                                 uint16_t e = ea(); wr(e, b); cc &= ~CC_V; nz8(b); return cyc; }
        if (!isB && lo == 0xD) { // BSR / JSR
            if (mode == 0) { int8_t off = int8_t(fetch()); pushS16(pc); pc = uint16_t(pc + off); return cyc; }
            uint16_t e = ea(); pushS16(pc); pc = e; return cyc;
        }
        if (!isB && lo == 0xF) { // STX
            if (mode == 0) { illegal = true; illegalPc = uint16_t(pc - 1); return 2; }
            uint16_t e = ea(); wr16(e, x); cc &= ~CC_V; nz16(x); return cyc;
        }
        if (isB && lo == 0xD) { // STD
            if (mode == 0) { illegal = true; illegalPc = uint16_t(pc - 1); return 2; }
            uint16_t e = ea(); wr16(e, d()); cc &= ~CC_V; nz16(d()); return cyc;
        }
        if (isB && lo == 0xF) { // STU
            if (mode == 0) { illegal = true; illegalPc = uint16_t(pc - 1); return 2; }
            uint16_t e = ea(); wr16(e, u); cc &= ~CC_V; nz16(u); return cyc;
        }

        if (wide) {
            uint16_t v;
            if (mode == 0) v = fetch16();
            else v = rd16(ea());
            switch (lo) {
            case 0x3: if (isB) setD(add16(d(), v)); else setD(sub16(d(), v)); break; // ADDD / SUBD
            case 0xC: if (isB) { setD(v); cc &= ~CC_V; nz16(v); } else sub16(x, v); break; // LDD / CMPX
            case 0xE: if (isB) { u = v; } else { x = v; } cc &= ~CC_V; nz16(v); break; // LDU / LDX
            default: break;
            }
            return cyc;
        }

        uint8_t v = mode == 0 ? fetch() : rd(ea());
        switch (lo) {
        case 0x0: acc = sub8(acc, v, 0); break;                          // SUB
        case 0x1: sub8(acc, v, 0); break;                                // CMP
        case 0x2: acc = sub8(acc, v, (cc & CC_C) ? 1 : 0); break;        // SBC
        case 0x4: acc &= v; cc &= ~CC_V; nz8(acc); break;                // AND
        case 0x5: { uint8_t r = acc & v; cc &= ~CC_V; nz8(r); break; }   // BIT
        case 0x6: acc = v; cc &= ~CC_V; nz8(acc); break;                 // LD
        case 0x8: acc ^= v; cc &= ~CC_V; nz8(acc); break;                // EOR
        case 0x9: acc = add8(acc, v, (cc & CC_C) ? 1 : 0); break;        // ADC
        case 0xA: acc |= v; cc &= ~CC_V; nz8(acc); break;                // OR
        case 0xB: acc = add8(acc, v, 0); break;                          // ADD
        default: illegal = true; illegalPc = uint16_t(pc - 1); break;
        }
        return cyc;
    }

    // ---- 1x / 3x misc
    switch (op) {
    case 0x12: return cyc; // NOP
    case 0x13: sync_ = true; return cyc; // SYNC
    case 0x16: { uint16_t o = fetch16(); pc = uint16_t(pc + o); return cyc; } // LBRA
    case 0x17: { uint16_t o = fetch16(); pushS16(pc); pc = uint16_t(pc + o); return cyc; } // LBSR
    case 0x19: { // DAA
        uint8_t msn = a & 0xF0, lsn = a & 0x0F;
        unsigned cf = 0;
        if (lsn > 9 || (cc & CC_H)) cf |= 0x06;
        if (msn > 0x80 && lsn > 9) cf |= 0x60;
        if (msn > 0x90 || (cc & CC_C)) cf |= 0x60;
        unsigned r = unsigned(a) + cf;
        cc &= ~(CC_V); // C is only ever set by DAA
        if (r & 0x100) cc |= CC_C;
        a = uint8_t(r);
        nz8(a);
        return cyc;
    }
    case 0x1A: cc |= fetch(); return cyc; // ORCC
    case 0x1C: cc &= fetch(); return cyc; // ANDCC
    case 0x1D: a = (b & 0x80) ? 0xFF : 0x00; cc &= ~CC_V; nz16(d()); return cyc; // SEX
    case 0x1E: { // EXG
        uint8_t pb = fetch();
        uint16_t r1 = getReg(pb >> 4), r2 = getReg(pb & 0xF);
        setReg(pb >> 4, r2); setReg(pb & 0xF, r1);
        return cyc;
    }
    case 0x1F: { uint8_t pb = fetch(); setReg(pb & 0xF, getReg(pb >> 4)); return cyc; } // TFR
    case 0x30: x = eaIndexed(); cc &= ~CC_Z; if (!x) cc |= CC_Z; return cyc; // LEAX
    case 0x31: y = eaIndexed(); cc &= ~CC_Z; if (!y) cc |= CC_Z; return cyc; // LEAY
    case 0x32: s = eaIndexed(); nmiArmed_ = true; return cyc; // LEAS
    case 0x33: u = eaIndexed(); return cyc; // LEAU
    case 0x34: return cyc + psh(false, fetch());
    case 0x35: return cyc + pul(false, fetch());
    case 0x36: return cyc + psh(true, fetch());
    case 0x37: return cyc + pul(true, fetch());
    case 0x39: pc = pullS16(); return cyc; // RTS
    case 0x3A: x = uint16_t(x + b); return cyc; // ABX
    case 0x3B: { // RTI
        cc = pullS();
        if (cc & CC_E) {
            a = pullS(); b = pullS(); dp = pullS();
            x = pullS16(); y = pullS16(); u = pullS16();
            pc = pullS16();
            return 15;
        }
        pc = pullS16();
        return 6;
    }
    case 0x3C: { // CWAI
        cc &= fetch();
        cc |= CC_E;
        pushAll();
        cwai_ = true;
        return cyc;
    }
    case 0x3D: { // MUL
        uint16_t r = uint16_t(a * b);
        setD(r);
        cc &= ~(CC_Z | CC_C);
        if (!r) cc |= CC_Z;
        if (r & 0x80) cc |= CC_C;
        return cyc;
    }
    case 0x3F: // SWI
        cc |= CC_E; pushAll(); cc |= CC_I | CC_F; pc = rd16(0xFFFA); return cyc;
    default:
        illegal = true; illegalPc = uint16_t(pc - 1);
        return 2;
    }
}

int Cpu6809::exec10(uint8_t op)
{
    if (op >= 0x21 && op <= 0x2F) { // long conditional branches
        uint16_t o = fetch16();
        if (cond(op)) { pc = uint16_t(pc + o); return 6; }
        return 5;
    }
    if (op == 0x3F) { cc |= CC_E; pushAll(); pc = rd16(0xFFF4); return 20; } // SWI2

    const int hi = op >> 4, lo = op & 0xF;
    const int mode = hi & 3;
    auto ea = [&]() -> uint16_t {
        switch (mode) {
        case 1: return eaDirect();
        case 2: return eaIndexed();
        default: return eaExtended();
        }
    };
    static const int base[4][2] = { {5, 4}, {7, 6}, {7, 6}, {8, 7} }; // [mode][cmp?0 : ld/st 1]

    if (hi >= 0x8 && hi <= 0xB) {
        switch (lo) {
        case 0x3: { uint16_t v = mode ? rd16(ea()) : fetch16(); sub16(d(), v); return base[mode][0]; } // CMPD
        case 0xC: { uint16_t v = mode ? rd16(ea()) : fetch16(); sub16(y, v); return base[mode][0]; }   // CMPY
        case 0xE: { uint16_t v = mode ? rd16(ea()) : fetch16(); y = v; cc &= ~CC_V; nz16(v); return base[mode][1]; } // LDY
        case 0xF: if (!mode) break; { uint16_t e = ea(); wr16(e, y); cc &= ~CC_V; nz16(y); return base[mode][1]; } // STY
        default: break;
        }
    } else if (hi >= 0xC) {
        switch (lo) {
        case 0xE: { uint16_t v = mode ? rd16(ea()) : fetch16(); s = v; nmiArmed_ = true; cc &= ~CC_V; nz16(v); return base[mode][1]; } // LDS
        case 0xF: if (!mode) break; { uint16_t e = ea(); wr16(e, s); cc &= ~CC_V; nz16(s); return base[mode][1]; } // STS
        default: break;
        }
    }
    illegal = true; illegalPc = uint16_t(pc - 2);
    return 2;
}

int Cpu6809::exec11(uint8_t op)
{
    if (op == 0x3F) { cc |= CC_E; pushAll(); pc = rd16(0xFFF2); return 20; } // SWI3
    const int hi = op >> 4, lo = op & 0xF;
    const int mode = hi & 3;
    if (hi >= 0x8 && hi <= 0xB && (lo == 0x3 || lo == 0xC)) {
        uint16_t v;
        if (mode == 0) v = fetch16();
        else {
            uint16_t e = mode == 1 ? eaDirect() : mode == 2 ? eaIndexed() : eaExtended();
            v = rd16(e);
        }
        sub16(lo == 0x3 ? u : s, v);
        static const int c[4] = {5, 7, 7, 8};
        return c[mode];
    }
    illegal = true; illegalPc = uint16_t(pc - 2);
    return 2;
}

} // namespace mirage
