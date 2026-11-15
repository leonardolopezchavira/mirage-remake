// Imports an audio file the way the Mirage samples: the file is played into the emulated
// sampling input (through the input filter, VCF 7, and the 8-bit converter) while the
// front-panel keys SAMPLE LOWER/UPPER and ENTER are pressed for the user, so the OS records
// it with its own sampling routine and current settings (73 rate, 74 input filter, 75 line or
// mic, 76 threshold, 77 multisample). Works with any Mirage OS. Driven by step() from one
// thread (the editor's). MIT licence.
#pragma once
#include "AudioFile.h"
#include "MirageEngine.h"
#include <string>
#include <vector>

namespace mirage {

class SampleImport {
public:
    enum class State { Idle, Running, Done, Failed };

    // `mono` at `rate` Hz; `half` 0 = lower, 1 = upper.
    void start(MirageEngine& e, const std::vector<float>& mono, double rate, int half, double nowMs);
    // Advances the key presses; call often (every 30 ms or so).
    State step(MirageEngine& e, double nowMs);
    State state() const { return state_; }
    const std::string& message() const { return message_; }
    // After a successful import: true if the OS ended on "SF" (OS 3.2); MASOS instead shows a
    // flashing prompt of its own, left for the user.
    bool endedOnSF() const { return endedOnSF_; }
    void reset() { state_ = State::Idle; }

private:
    enum class Stage { PressSample, WaitLevel, PressEnter, Recording, Cancel };
    State state_ = State::Idle;
    Stage stage_ = Stage::PressSample;
    std::string message_;
    int half_ = 0;
    double t_ = 0, deadline_ = 0, levelSince_ = -1, fileMs_ = 0, idleSince_ = -1;
    bool keyDown_ = false, endedOnSF_ = false;
    std::vector<uint8_t> before_;   // the half's sample memory, to see that something was recorded

    void finish(MirageEngine& e, State s, const std::string& msg);
};

} // namespace mirage
