#include "Os32Link.h"
#include <algorithm>
#include <cstring>

namespace mirage {

namespace {
constexpr double kKeyMs = 90;   // each key held 90 ms and released 90 ms (the OS needs ~60 ms)
const int kDigit[10] = {Mirage::K0Prog, Mirage::K1, Mirage::K2, Mirage::K3, Mirage::K4,
                        Mirage::K5, Mirage::K6, Mirage::K7, Mirage::K8, Mirage::K9};
} // namespace

constexpr uint16_t Os32Link::kHalfBase[2];
constexpr uint16_t Os32Link::kProgram[2];
constexpr uint16_t Os32Link::kWavesample[2];

Os32Link::Os32Link(MirageEngine& e, std::vector<uint8_t> fingerprint, uint16_t fingerprintAddr)
    : e_(e), fingerprint_(std::move(fingerprint)), fingerprintAddr_(fingerprintAddr)
{
}

bool Os32Link::needsKeys(int number)
{
    // OS 3.2 rebuilds its key map after 27 and 72 and recalculates the sample and loop
    // pointers after 60-65: those go through the front panel.
    return number == 27 || number == 72 || (number >= 60 && number <= 65);
}

int Os32Link::dumpOffset(int half, const MasosParam& p) const
{
    const SoundHalf& h = halves_[half & 1];
    return p.wavesample ? SoundHalf::kWcbOffset + h.wavesample * SoundHalf::kWcbSize + p.offset
                        : SoundHalf::kPpbOffset + h.program * SoundHalf::kPpbSize + p.offset;
}

int Os32Link::value(int half, int number) const
{
    const MasosParam* p = masosParam(number);
    if (!p) return 0;
    return halves_[half & 1].dump[dumpOffset(half, *p)];   // overrides are already applied
}

void Os32Link::readMemory()
{
    const uint8_t* r = ram();
    bool changed = false;
    if (now_ > wantUntil_) wantHalf_ = wantProgram_[0] = wantProgram_[1] = wantWavesample_[0] = wantWavesample_[1] = -1;
    int cur = r[kCurrentHalf - 0x8000] & 1;
    if (wantHalf_ >= 0) { if (cur == wantHalf_) wantHalf_ = -1; else cur = wantHalf_; }
    if (cur != curHalf_) { curHalf_ = cur; changed = true; }
    for (int h = 0; h < 2; ++h) {
        SoundHalf& sh = halves_[h];
        uint8_t fresh[SoundHalf::kDumpBytes];
        std::memcpy(fresh, r + (kHalfBase[h] - 0x8000), sizeof fresh);
        // edits not yet applied by the Mirage keep their new value in the view
        for (auto it = overrides_[h].begin(); it != overrides_[h].end();) {
            if (fresh[it->first] == it->second.raw || now_ > it->second.until) it = overrides_[h].erase(it);
            else { fresh[it->first] = uint8_t(it->second.raw); ++it; }
        }
        int prog = r[kProgram[h] - 0x8000] & 3, ws = r[kWavesample[h] - 0x8000] & 7;
        if (wantProgram_[h] >= 0) { if (prog == wantProgram_[h]) wantProgram_[h] = -1; else prog = wantProgram_[h]; }
        if (wantWavesample_[h] >= 0) { if (ws == wantWavesample_[h]) wantWavesample_[h] = -1; else ws = wantWavesample_[h]; }
        if (!sh.valid || std::memcmp(fresh, sh.dump, sizeof fresh) || prog != sh.program || ws != sh.wavesample) {
            std::memcpy(sh.dump, fresh, sizeof fresh);
            sh.program = prog;
            sh.wavesample = ws;
            sh.valid = true;
            changed = true;
        }
    }
    if (changed) ++revision_;
}

void Os32Link::update(double nowMs)
{
    now_ = nowMs;
    const bool os32 = fingerprint_.size() &&
                      !std::memcmp(ram() + (fingerprintAddr_ - 0x8000), fingerprint_.data(), fingerprint_.size());
    if (os32 != connected_) {
        connected_ = os32;
        macro_.clear();
        pending_.clear();
        overrides_[0].clear();
        overrides_[1].clear();
        halves_[0].valid = halves_[1].valid = false;
        ++revision_;
    }
    if (!connected_) return;
    readMemory();
    if (paused_) return;

    // the key presses (and pokes between them) of the edit in progress
    while (!macro_.empty() && now_ >= nextStep_) {
        Step s = macro_.front();
        macro_.pop_front();
        if (s.kind == Step::Poke) { e_.postPoke(uint16_t(s.a), uint8_t(s.v)); continue; }
        if (s.kind == Step::EnsureHalf) {
            // CANCEL, PROG, PROG switches the half the panel acts on; checked again afterwards
            if ((peek(kCurrentHalf) & 1) != s.a && s.v < 3) {
                macro_.push_front({Step::EnsureHalf, s.a, s.v + 1});
                for (int k : {Mirage::K0Prog, Mirage::K0Prog, Mirage::Cancel}) {
                    macro_.push_front({Step::Up, k});
                    macro_.push_front({Step::Down, k});
                }
            }
            continue;
        }
        e_.setPanelKey(s.a, s.kind == Step::Down);
        nextStep_ = now_ + kKeyMs;
        break;
    }
    if (!macro_.empty()) return;

    // next edit: direct writes all at once, then at most one that needs keys
    while (!pending_.empty()) {
        const Edit ed = pending_.front();
        pending_.pop_front();
        start(ed);
        if (!macro_.empty()) break;
    }
}

void Os32Link::queue(const Edit& ed)
{
    for (auto& p : pending_)
        if (p.kind == ed.kind && p.half == ed.half && p.number == ed.number) { p.raw = ed.raw; return; }   // latest wins
    pending_.push_back(ed);
}

void Os32Link::keys(std::initializer_list<int> ks)
{
    for (int k : ks) {
        macro_.push_back({Step::Down, k});
        macro_.push_back({Step::Up, k});
    }
}

void Os32Link::digits(int number) { keys({kDigit[(number / 10) % 10], kDigit[number % 10]}); }

void Os32Link::start(const Edit& ed)
{
    const int h = ed.half & 1;
    const uint16_t base = kHalfBase[h];
    if (ed.kind == 1) {            // program: (half) CANCEL PROG digit
        macro_.push_back({Step::EnsureHalf, h});
        keys({Mirage::Cancel, Mirage::K0Prog, kDigit[(ed.raw & 3) + 1]});
        return;
    }
    if (ed.kind == 2) {            // wavesample: parameter 26, one step from a neighbouring value
        const int ws = ed.raw & 7, from = ws > 0 ? ws - 1 : 1;
        macro_.push_back({Step::EnsureHalf, h});
        macro_.push_back({Step::Poke, kWavesample[h], from});
        keys({Mirage::Param});
        digits(26);
        keys({Mirage::Value, ws > from ? Mirage::Up : Mirage::Down});
        return;
    }
    const MasosParam* p = masosParam(ed.number);
    if (!p) return;
    const int addr = base + dumpOffset(h, *p);
    if (!needsKeys(ed.number)) {   // read by the OS when a note starts: a write is enough
        e_.postPoke(uint16_t(addr), uint8_t(ed.raw));
        return;
    }
    // Put the value one step away into memory, then let the OS make the last step itself.
    macro_.push_back({Step::EnsureHalf, h});
    int dir = Mirage::Up;
    if (ed.number == 65) {
        dir = ed.raw ? Mirage::Up : Mirage::Down;      // loop on / off
    } else {
        int from = ed.raw - p->step;
        if (from < 0) { from = ed.raw + p->step; dir = Mirage::Down; }
        macro_.push_back({Step::Poke, addr, from});
    }
    keys({Mirage::Param});
    digits(ed.number);
    keys({Mirage::Value, dir});
}

void Os32Link::setParam(int half, int number, int raw)
{
    const MasosParam* p = masosParam(number);
    if (!p || !connected_) return;
    half &= 1;
    raw = std::clamp(raw, 0, p->maxRaw);
    const int off = dumpOffset(half, *p);
    overrides_[half][off] = {off, raw, now_ + 4000};
    halves_[half].dump[off] = uint8_t(raw);
    queue({0, half, number, raw});
    ++revision_;
}

void Os32Link::selectWavesample(int half, int ws)
{
    if (!connected_) return;
    half &= 1;
    queue({2, half, 26, ws & 7});
    wantWavesample_[half] = ws & 7;
    wantHalf_ = half;
    wantUntil_ = now_ + 6000;
    halves_[half].wavesample = ws & 7;
    curHalf_ = half;
    ++revision_;
}

void Os32Link::selectProgram(int half, int prog)
{
    if (!connected_) return;
    half &= 1;
    queue({1, half, 0, prog & 3});
    wantProgram_[half] = prog & 3;
    wantHalf_ = half;
    wantUntil_ = now_ + 6000;
    halves_[half].program = prog & 3;
    curHalf_ = half;
    ++revision_;
}

} // namespace mirage
