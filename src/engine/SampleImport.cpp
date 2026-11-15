#include "SampleImport.h"
#include <algorithm>
#include <cmath>

namespace mirage {

void SampleImport::start(MirageEngine& e, const std::vector<float>& mono, double rate, int half, double nowMs)
{
    // At the host rate, with a level the line input takes without clipping: peak at -7 dB.
    std::vector<float> s = resampleLinear(mono, rate, e.sampleRate());
    float peak = 0;
    for (float v : s) peak = std::max(peak, std::fabs(v));
    if (peak > 1e-6f) for (float& v : s) v *= 0.45f / peak;
    fileMs_ = double(s.size()) / e.sampleRate() * 1000.0;
    e.beginFileInput(std::move(s));
    half_ = half & 1;
    const uint8_t* ram = e.machine().sampleRam() + (size_t(half_) << 16);
    before_.assign(ram, ram + 65536);
    state_ = State::Running;
    stage_ = Stage::PressSample;
    keyDown_ = false;
    levelSince_ = -1;
    t_ = nowMs;
    deadline_ = nowMs + 12000;
    message_.clear();
}

void SampleImport::finish(MirageEngine& e, State s, const std::string& msg)
{
    e.endFileInput();
    state_ = s;
    message_ = msg;
    if (s == State::Done) {
        // Only a success if the OS actually wrote new sample data into that half.
        const uint8_t* ram = e.machine().sampleRam() + (size_t(half_) << 16);
        if (std::equal(before_.begin(), before_.end(), ram)) {
            state_ = State::Failed;
            message_ = "The Mirage did not record anything";
        }
    }
    before_.clear();
}

SampleImport::State SampleImport::step(MirageEngine& e, double now)
{
    if (state_ != State::Running) return state_;
    const int sampleKey = half_ ? Mirage::SampleUpper : Mirage::SampleLower;
    switch (stage_) {
    case Stage::PressSample:   // one press of SAMPLE LOWER / UPPER (held 120 ms)
        if (!keyDown_) { e.setPanelKey(sampleKey, true); keyDown_ = true; t_ = now; }
        else if (now - t_ >= 120) { e.setPanelKey(sampleKey, false); keyDown_ = false; stage_ = Stage::WaitLevel; }
        break;
    case Stage::WaitLevel:     // "SL" flashes, then the level meter: the OS reads the input
        if (e.samplingNow.load()) {
            if (levelSince_ < 0) levelSince_ = now;
            if (now - levelSince_ >= 600) { stage_ = Stage::PressEnter; t_ = now; }
        } else {
            levelSince_ = -1;
        }
        if (now > deadline_) finish(e, State::Failed, "The Mirage did not enter sampling mode");
        break;
    case Stage::PressEnter:    // ENTER arms the threshold; the file starts playing with it
        if (!keyDown_) { e.setPanelKey(Mirage::Enter, true); keyDown_ = true; t_ = now; }
        else if (now - t_ >= 120) {
            e.setPanelKey(Mirage::Enter, false);
            keyDown_ = false;
            e.playFileInput();
            idleSince_ = -1;
            stage_ = Stage::Recording;
            deadline_ = now + fileMs_ + 8000;
        }
        break;
    case Stage::Recording:     // the OS records once the signal crosses the threshold
        // Done when the OS stops reading the input: OS 3.2 then shows "SF", MASOS a prompt.
        endedOnSF_ = e.display(0) == 0x6D && e.display(1) == 0x71;
        if (endedOnSF_) { finish(e, State::Done, ""); break; }
        if (!e.samplingNow.load()) {
            if (idleSince_ < 0) idleSince_ = now;
            if (now - idleSince_ >= 400) { finish(e, State::Done, ""); break; }
        } else {
            idleSince_ = -1;
        }
        if (now > deadline_) { stage_ = Stage::Cancel; t_ = now; e.setPanelKey(Mirage::Cancel, true); }
        break;
    case Stage::Cancel:        // CANCEL leaves sampling
        if (now - t_ >= 120) {
            e.setPanelKey(Mirage::Cancel, false);
            finish(e, State::Failed, "The Mirage did not record: the file may be too quiet for the threshold (76)");
        }
        break;
    }
    return state_;
}

} // namespace mirage
