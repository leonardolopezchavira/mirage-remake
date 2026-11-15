// The editor's link to a Mirage running Ensoniq's OS 3.2, without MASOS_BE: OS 3.2 keeps each
// half as the 625-byte sound block of the MASOS dump format ($B31D lower, $B58E upper), so
// the editor reads the values straight from memory. Edits are written into memory too
// (through the engine, on its own thread); the parameters OS 3.2 has to recalculate after a
// change (27 initial wavesample, 60-65 sample and loop, 72 top key) and the selection of
// half, program and wavesample are done the way a player does them, with front-panel key
// presses, so the OS applies its own checks. Editor thread only. MIT licence.
#pragma once
#include "EditorLink.h"
#include "MasosLink.h"
#include "MirageEngine.h"
#include <deque>
#include <map>
#include <vector>

namespace mirage {

class Os32Link : public EditorLink {
public:
    // `fingerprint`: the bytes the OS 3.2 code has at `fingerprintAddr` once loaded; the link
    // is connected while RAM matches them.
    Os32Link(MirageEngine& e, std::vector<uint8_t> fingerprint, uint16_t fingerprintAddr);

    // Reads the sound blocks and the selection, and presses the next key; call often.
    void update(double nowMs);
    // While paused (e.g. during an audio file import) no keys are pressed and no edits made.
    void setPaused(bool p) { paused_ = p; }
    bool busy() const { return !macro_.empty() || !pending_.empty(); }

    bool connected() const override { return connected_; }
    const char* osName() const override { return "OS 3.2"; }
    int currentHalf() const override { return curHalf_; }
    const SoundHalf& half(int h) const override { return halves_[h & 1]; }
    int value(int half, int number) const override;
    void setParam(int half, int number, int raw) override;
    void selectWavesample(int half, int ws) override;
    void selectProgram(int half, int prog) override;
    void requestAll() override { ++revision_; }
    uint64_t revision() const override { return revision_; }

    // Memory map (OS 3.2)
    static constexpr uint16_t kHalfBase[2] = {0xB31D, 0xB58E};
    static constexpr uint16_t kCurrentHalf = 0x80A4;           // 0 lower, 1 upper: what panel edits act on
    static constexpr uint16_t kProgram[2] = {0x808F, 0x808E};  // selected program, per half
    static constexpr uint16_t kWavesample[2] = {0x80AA, 0x80A9};

private:
    struct Step {
        enum Kind { Poke, Down, Up, EnsureHalf } kind;
        int a = 0, v = 0;
    };
    struct Edit {
        int kind;           // 0 parameter, 1 program selection, 2 wavesample selection
        int half, number, raw;
    };
    struct Override { int offset, raw; double until; };

    MirageEngine& e_;
    std::vector<uint8_t> fingerprint_;
    uint16_t fingerprintAddr_;
    bool connected_ = false, paused_ = false;
    SoundHalf halves_[2];
    int curHalf_ = 0;
    uint64_t revision_ = 0;
    double now_ = 0, nextStep_ = 0;
    std::deque<Step> macro_;
    std::deque<Edit> pending_;
    std::map<int, Override> overrides_[2];   // per half, by dump offset
    // selections asked for but not yet made by the OS: shown at once (-1 = none)
    int wantProgram_[2] = {-1, -1}, wantWavesample_[2] = {-1, -1}, wantHalf_ = -1;
    double wantUntil_ = 0;

    const uint8_t* ram() { return e_.machine().mainRam(); }
    uint8_t peek(int a) { return ram()[a - 0x8000]; }
    static bool needsKeys(int number);
    int dumpOffset(int half, const MasosParam& p) const;
    void queue(const Edit& ed);
    void start(const Edit& ed);
    void keys(std::initializer_list<int> ks);
    void digits(int number);
    void readMemory();
};

} // namespace mirage

namespace mirage {

// The editor talks to whichever OS is running: OS 3.2 directly, else MASOS (SysEx).
class LinkSwitch : public EditorLink {
public:
    LinkSwitch(MasosLink& masos, Os32Link& os32) : masos_(masos), os32_(os32) {}
    EditorLink& active() const { return os32_.connected() ? static_cast<EditorLink&>(os32_) : static_cast<EditorLink&>(masos_); }
    bool connected() const override { return active().connected(); }
    const char* osName() const override { return active().osName(); }
    int currentHalf() const override { return active().currentHalf(); }
    const SoundHalf& half(int h) const override { return active().half(h); }
    int value(int half, int number) const override { return active().value(half, number); }
    void setParam(int half, int number, int raw) override { active().setParam(half, number, raw); }
    void selectWavesample(int half, int ws) override { active().selectWavesample(half, ws); }
    void selectProgram(int half, int prog) override { active().selectProgram(half, prog); }
    void requestAll() override { active().requestAll(); }
    uint64_t revision() const override { return masos_.revision() + os32_.revision() + (os32_.connected() ? 1u << 31 : 0); }

private:
    MasosLink& masos_;
    Os32Link& os32_;
};

} // namespace mirage
