// The editor's side of the MASOS / MASOS_BE SysEx protocol (BoxedEar's "MASOS_BE MIDI
// Specification"): keeps a copy of the Mirage's configuration and of both sound halves
// (eight wavesample control blocks and four programs each) from the dumps and the parameter
// and status messages the Mirage sends, and turns edits into parameter writes, paced as the
// spec asks (at least 8 ms between SysEx messages, fast movements combined).
// Plain C++, one thread (the editor's). MIT licence.
#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace mirage {

enum class ParamDisplay { Decimal, Hex, OnOff, PlusOne };

struct MasosParam {
    int number;             // front-panel parameter number
    const char* name;
    int offset;             // byte offset in its block (program 36 bytes, wavesample 24 bytes)
    int maxRaw;
    int step;               // raw units per front-panel step
    ParamDisplay display;
    bool wavesample;        // in the wavesample control block, not the program block
};

const std::vector<MasosParam>& masosParams();
const MasosParam* masosParam(int number);
// The value as the Mirage's display shows it ("45", "7F", "on", ...).
std::string masosDisplay(const MasosParam& p, int raw);

class MasosLink {
public:
    static constexpr int kDumpBytes = 625;
    static constexpr int kWcbOffset = 1, kWcbSize = 24, kPpbOffset = 481, kPpbSize = 36;

    struct Half {
        bool valid = false;
        uint8_t dump[kDumpBytes] = {};
        int program = 0;          // selected program 0..3
        int wavesample = 0;       // selected wavesample 0..7
        int programRaw(int prog, const MasosParam& p) const { return dump[kPpbOffset + prog * kPpbSize + p.offset]; }
        int wavesampleRaw(int ws, const MasosParam& p) const { return dump[kWcbOffset + ws * kWcbSize + p.offset]; }
    };

    // Bytes the Mirage sent on MIDI OUT.
    void receive(uint8_t b);
    // Sends what is due: at most one SysEx message per call, at least 10 ms apart. `nowMs` is
    // any monotonic clock in milliseconds.
    void pump(double nowMs, const std::function<void(const uint8_t*, int)>& send);

    // Forgets everything (the Mirage restarted): not connected until it answers again.
    void reset() { queue_.clear(); connected_ = false; halves_[0] = Half(); halves_[1] = Half(); curHalf_ = 0; ++revision_; }
    // Asks for the configuration and both halves (also done on the first pump()).
    void requestAll();
    void requestHalf(int half);

    // Edits (also applied to the local copy at once). Program parameters go to the selected
    // program of that half; select() it first.
    void setParam(int half, int number, int raw);
    // Selection, through front-panel key presses.
    void selectWavesample(int half, int ws);
    void selectProgram(int half, int prog);

    bool connected() const { return connected_; }
    int currentHalf() const { return curHalf_; }
    const Half& half(int h) const { return halves_[h & 1]; }
    // Current value of a parameter for the selected program / wavesample of that half.
    int value(int half, int number) const;
    // Counts every change of the model, so a view knows when to redraw.
    uint64_t revision() const { return revision_; }

private:
    struct Out {
        std::vector<uint8_t> bytes;
        int coalesceKey = -1;      // writes to the same parameter replace each other
        double notBefore = 0;      // ms
    };
    std::deque<Out> queue_;
    double lastSend_ = -1e9, now_ = 0;
    bool started_ = false, connected_ = false;
    Half halves_[2];
    int curHalf_ = 0;
    uint64_t revision_ = 0;
    std::vector<uint8_t> rx_;
    bool inSysex_ = false;
    uint8_t config_[29] = {};
    // a Program Status just before a Wavesample Status carries the wavesample number
    bool lastWasProgramStatus_ = false;
    int programBefore_ = 0;

    void enqueue(std::vector<uint8_t> msg, int coalesceKey = -1, double delayMs = 0);
    void handle(const std::vector<uint8_t>& m);
    static void nybbles(std::vector<uint8_t>& out, uint8_t v) { out.push_back(v & 15); out.push_back(v >> 4); }
};

} // namespace mirage
