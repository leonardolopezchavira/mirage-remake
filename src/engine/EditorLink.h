// What the built-in editor needs from the running Mirage OS, whichever way it is reached:
// MasosLink (BoxedEar's MASOS_BE SysEx protocol) or Os32Link (OS 3.2's memory, read and
// written directly, with front-panel key presses where the OS has to recalculate).
// Both keep each half as the 625-byte sound block of the MASOS dump format, which is also
// how OS 3.2 holds it in memory. MIT licence.
#pragma once
#include <cstdint>

namespace mirage {

struct MasosParam;

struct SoundHalf {
    static constexpr int kDumpBytes = 625;
    static constexpr int kWcbOffset = 1, kWcbSize = 24, kPpbOffset = 481, kPpbSize = 36;
    bool valid = false;
    uint8_t dump[kDumpBytes] = {};
    int program = 0;          // selected program 0..3
    int wavesample = 0;       // selected wavesample 0..7
    int programRaw(int prog, const MasosParam& p) const;
    int wavesampleRaw(int ws, const MasosParam& p) const;
};

class EditorLink {
public:
    virtual ~EditorLink() = default;
    virtual bool connected() const = 0;
    virtual const char* osName() const = 0;          // for the editor's status line
    virtual int currentHalf() const = 0;
    virtual const SoundHalf& half(int h) const = 0;
    // Current value of a parameter for the selected program / wavesample of that half.
    virtual int value(int half, int number) const = 0;
    virtual void setParam(int half, int number, int raw) = 0;
    virtual void selectWavesample(int half, int ws) = 0;
    virtual void selectProgram(int half, int prog) = 0;
    virtual void requestAll() = 0;
    // Counts every change of the model, so a view knows when to redraw.
    virtual uint64_t revision() const = 0;
};

} // namespace mirage
