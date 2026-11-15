#include "MasosLink.h"
#include <cstdio>

namespace mirage {

namespace {

constexpr ParamDisplay D = ParamDisplay::Decimal, H = ParamDisplay::Hex, O = ParamDisplay::OnOff,
                       P = ParamDisplay::PlusOne;

// Offsets and ranges: spec tables 7.3 and 7.4. Steps: how far one press of the Mirage's ▲
// moves the raw value (measured in the emulator, MASOS_BE).
const std::vector<MasosParam> kParams = {
    {27, "Initial wavesample", 10, 7, 1, P, false},
    {28, "Mix mode", 11, 1, 1, O, false},
    {29, "Mono mode", 0, 1, 1, O, false},
    {31, "LFO frequency", 1, 99, 1, D, false},
    {32, "LFO depth", 2, 99, 1, D, false},
    {33, "Osc detune", 3, 99, 1, D, false},
    {34, "Osc mix", 4, 252, 4, D, false},
    {35, "Mix velocity", 5, 124, 4, D, false},
    {36, "Filter cutoff", 6, 198, 2, D, false},
    {37, "Resonance", 7, 160, 4, D, false},
    {38, "Keyboard tracking", 8, 4, 1, D, false},
    {40, "Attack", 12, 31, 1, D, false},
    {41, "Peak", 13, 31, 1, D, false},
    {42, "Decay", 14, 31, 1, D, false},
    {43, "Sustain", 15, 31, 1, D, false},
    {44, "Release", 16, 31, 1, D, false},
    {45, "Attack \xC3\x97 velocity", 17, 124, 4, D, false},
    {46, "Peak \xC3\x97 velocity", 18, 124, 4, D, false},
    {47, "Decay \xC3\x97 key", 19, 124, 4, D, false},
    {48, "Sustain \xC3\x97 velocity", 20, 124, 4, D, false},
    {49, "Release \xC3\x97 velocity", 21, 124, 4, D, false},
    {50, "Attack", 22, 31, 1, D, false},
    {51, "Peak", 23, 31, 1, D, false},
    {52, "Decay", 24, 31, 1, D, false},
    {53, "Sustain", 25, 31, 1, D, false},
    {54, "Release", 26, 31, 1, D, false},
    {55, "Attack \xC3\x97 velocity", 27, 124, 4, D, false},
    {56, "Peak \xC3\x97 velocity", 28, 124, 4, D, false},
    {57, "Decay \xC3\x97 key", 29, 124, 4, D, false},
    {58, "Sustain \xC3\x97 velocity", 30, 124, 4, D, false},
    {59, "Release \xC3\x97 velocity", 31, 124, 4, D, false},
    {60, "Sample start", 15, 254, 1, H, true},
    {61, "Sample end", 16, 255, 1, H, true},
    {62, "Loop start", 17, 254, 1, H, true},
    {63, "Loop end", 18, 255, 1, H, true},
    {64, "Loop end fine", 19, 255, 1, H, true},
    {65, "Loop", 8, 1, 1, O, true},
    {67, "Coarse tune", 9, 7, 1, D, true},
    {68, "Fine tune", 10, 255, 1, H, true},
    {69, "Relative amplitude", 11, 63, 1, D, true},
    {70, "Relative filter", 12, 198, 2, D, true},
    {71, "Maximum filter", 13, 198, 2, D, true},
    {72, "Top key", 14, 60, 1, P, true},
};

} // namespace

const std::vector<MasosParam>& masosParams() { return kParams; }

const MasosParam* masosParam(int number)
{
    for (const auto& p : kParams)
        if (p.number == number) return &p;
    return nullptr;
}

std::string masosDisplay(const MasosParam& p, int raw)
{
    char s[16];
    switch (p.display) {
    case ParamDisplay::OnOff: return raw ? "on" : "off";
    case ParamDisplay::Hex: std::snprintf(s, sizeof s, "%02X", raw & 0xFF); return s;
    case ParamDisplay::PlusOne: std::snprintf(s, sizeof s, "%d", raw + 1); return s;
    default: std::snprintf(s, sizeof s, "%d", raw / (p.step > 0 ? p.step : 1)); return s;
    }
}

// ------------------------------------------------------------------------------------------

void MasosLink::enqueue(std::vector<uint8_t> msg, int key, double delayMs)
{
    if (key >= 0)
        for (auto& o : queue_)
            if (o.coalesceKey == key) { o.bytes = std::move(msg); return; }   // latest value wins
    queue_.push_back({std::move(msg), key, now_ + delayMs});
}

void MasosLink::requestAll()
{
    enqueue({0xF0, 0x0F, 0x01, 0x00, 0xF7}, 1000);
    requestHalf(0);
    requestHalf(1);
}

void MasosLink::requestHalf(int half)
{
    enqueue({0xF0, 0x0F, 0x01, uint8_t(0x03 | (half & 1) << 4), 0xF7}, 1001 + (half & 1));
}

void MasosLink::pump(double nowMs, const std::function<void(const uint8_t*, int)>& send)
{
    now_ = nowMs;
    if (!started_) { started_ = true; requestAll(); }
    if (queue_.empty() || nowMs - lastSend_ < 10.0) return;
    // the first message that is due (a delayed resync doesn't hold up edits)
    for (auto it = queue_.begin(); it != queue_.end(); ++it) {
        if (it->notBefore > nowMs) continue;
        send(it->bytes.data(), int(it->bytes.size()));
        queue_.erase(it);
        lastSend_ = nowMs;
        return;
    }
}

void MasosLink::setParam(int half, int number, int raw)
{
    const MasosParam* p = masosParam(number);
    if (!p) return;
    half &= 1;
    if (raw < 0) raw = 0;
    if (raw > p->maxRaw) raw = p->maxRaw;
    Half& h = halves_[half];
    std::vector<uint8_t> m = {0xF0, 0x0F, 0x01};
    if (p->wavesample) {
        h.dump[kWcbOffset + h.wavesample * kWcbSize + p->offset] = uint8_t(raw);
        m.push_back(0x0E);
        m.push_back(uint8_t(half << 4 | h.wavesample));
    } else {
        h.dump[kPpbOffset + h.program * kPpbSize + p->offset] = uint8_t(raw);
        m.push_back(0x0D);
        m.push_back(uint8_t(half << 4 | h.program));
    }
    m.push_back(uint8_t(p->offset));
    nybbles(m, uint8_t(raw));
    m.push_back(0xF7);
    const int key = (half << 12) | (p->wavesample ? (1 << 11) | (h.wavesample << 8) : (h.program << 8)) | p->number;
    enqueue(std::move(m), key);
    // The Mirage checks sample and loop pointers itself (and resets the loop after a new
    // start or end): read the half back once the edits have settled.
    if (p->number >= 60 && p->number <= 65) {
        for (auto it = queue_.begin(); it != queue_.end(); ++it)
            if (it->coalesceKey == 1001 + half) { queue_.erase(it); break; }
        enqueue({0xF0, 0x0F, 0x01, uint8_t(0x03 | half << 4), 0xF7}, 1001 + half, 400);
    }
    ++revision_;
}

void MasosLink::selectWavesample(int half, int ws)
{
    half &= 1;
    ws &= 7;
    enqueue({0xF0, 0x0F, 0x01, 0x01, uint8_t(half ? 0x14 : 0x15), uint8_t(ws + 1), 0x7F, 0xF7});
    halves_[half].wavesample = ws;
    curHalf_ = half;
    ++revision_;
}

void MasosLink::selectProgram(int half, int prog)
{
    half &= 1;
    prog &= 3;
    // PROG acts on the current half: move there first (by selecting its wavesample).
    if (half != curHalf_) selectWavesample(half, halves_[half].wavesample);
    enqueue({0xF0, 0x0F, 0x01, 0x01, 0x00, uint8_t(prog + 1), 0x7F, 0xF7});
    halves_[half].program = prog;
    ++revision_;
}

int MasosLink::value(int half, int number) const
{
    const MasosParam* p = masosParam(number);
    if (!p) return 0;
    const Half& h = halves_[half & 1];
    return p->wavesample ? h.wavesampleRaw(h.wavesample, *p) : h.programRaw(h.program, *p);
}

// ------------------------------------------------------------------------------------------

void MasosLink::receive(uint8_t b)
{
    if (b >= 0xF8) return;
    if (b == 0xF0) { rx_.assign(1, b); inSysex_ = true; return; }
    if (!inSysex_) return;
    if (b == 0xF7) { inSysex_ = false; handle(rx_); rx_.clear(); return; }
    if (b & 0x80) { inSysex_ = false; rx_.clear(); return; }
    rx_.push_back(b);
    if (rx_.size() > 140000) { inSysex_ = false; rx_.clear(); }
}

void MasosLink::handle(const std::vector<uint8_t>& m)
{
    if (m.size() < 4 || m[1] != 0x0F || m[2] != 0x01) return;
    const uint8_t cmd = m[3];
    const uint8_t* d = m.data() + 4;
    const size_t n = m.size() - 4;
    auto byteAt = [&](size_t i) { return uint8_t(d[2 * i] | d[2 * i + 1] << 4); };

    const bool afterProgramStatus = lastWasProgramStatus_;
    lastWasProgramStatus_ = false;
    switch (cmd & 0x0F) {
    case 0x02:   // Configuration Dump Data
        if (n >= 58) for (size_t i = 0; i < 29; ++i) config_[i] = byteAt(i);
        connected_ = true;
        break;
    case 0x05: { // Program Dump Data
        const int half = (cmd >> 4) & 1;
        if (n < size_t(kDumpBytes) * 2) return;
        Half& h = halves_[half];
        for (size_t i = 0; i < size_t(kDumpBytes); ++i) h.dump[i] = byteAt(i);
        h.valid = true;
        connected_ = true;
        break;
    }
    case 0x07: { // Program Status
        if (n < 1) return;
        const int half = (d[0] >> 4) & 1, prog = d[0] & 0x0F;
        programBefore_ = halves_[half].program;
        if (prog <= 3) halves_[half].program = prog;
        curHalf_ = half;
        connected_ = true;
        lastWasProgramStatus_ = true;
        ++revision_;
        return;
    }
    case 0x08: { // Wavesample Status
        if (n < 1) return;
        const int half = (d[0] >> 4) & 1;
        halves_[half].wavesample = d[0] & 7;
        // the Program Status sent with a wavesample selection is not a program change
        if (afterProgramStatus && curHalf_ == half) halves_[half].program = programBefore_;
        curHalf_ = half;
        connected_ = true;
        break;
    }
    case 0x0D: { // Program Parameter Message (front-panel edit): parameter number
        if (cmd != 0x0D || n < 4) return;
        const int half = (d[0] >> 4) & 1, prog = d[0] & 3;
        const MasosParam* p = masosParam(d[1]);
        if (!p || p->wavesample) return;
        halves_[half].dump[kPpbOffset + prog * kPpbSize + p->offset] = uint8_t(d[2] | d[3] << 4);
        halves_[half].program = prog;
        break;
    }
    case 0x0E: { // Wavesample Parameter Message
        if (cmd != 0x0E || n < 4) return;
        const int half = (d[0] >> 4) & 1, ws = d[0] & 7;
        const MasosParam* p = masosParam(d[1]);
        if (!p || !p->wavesample) return;
        halves_[half].dump[kWcbOffset + ws * kWcbSize + p->offset] = uint8_t(d[2] | d[3] << 4);
        halves_[half].wavesample = ws;
        // the Mirage may have adjusted the loop: read the half back
        if (p->number == 60 || p->number == 61) requestHalf(half);
        break;
    }
    default:
        return;
    }
    ++revision_;
}

} // namespace mirage
