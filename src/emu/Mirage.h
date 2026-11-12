// The Ensoniq Mirage (DSK-8 main board): 6809E at 2 MHz, 128 KB sample RAM, 6522 VIA,
// 6850 ACIA (MIDI), WD1772 floppy, ES5503 DOC, 8 x CEM3328 VCF.
// Memory map after MAME's enmirage.cpp (BSD-3-Clause, R. Belmont, tim lindner). MIT licence.
#pragma once
#include "Acia6850.h"
#include "Cem3328.h"
#include "Cpu6809.h"
#include "Es5503.h"
#include "Via6522.h"
#include "Wd1772.h"
#include <array>
#include <cstdint>
#include <vector>

namespace mirage {

class Mirage : public Bus6809 {
public:
    static constexpr double kCpuHz = 2000000.0;
    static constexpr int kAnalogDiv = 16;                 // analog section runs every 16 CPU cycles
    static constexpr double kAnalogHz = kCpuHz / kAnalogDiv; // 125 kHz

    // Front-panel keys: row 0..2, column 0..7 (see MAME's layout).
    enum Key {
        LoadUpper = 0x00, LoadLower = 0x01, SampleUpper = 0x02, PlaySeq = 0x03, LoadSeq = 0x04,
        SaveSeq = 0x05, RecSeq = 0x06, SampleLower = 0x07,
        K3 = 0x10, K6 = 0x11, K9 = 0x12, K5 = 0x13, K8 = 0x14, K0Prog = 0x15, K2 = 0x16, Enter = 0x17,
        K1 = 0x20, K4 = 0x21, K7 = 0x22, Up = 0x23, Param = 0x24, Down = 0x25, Value = 0x26, Cancel = 0x27,
    };

    Mirage();

    bool loadRom(const std::vector<uint8_t>& rom);   // 4096 bytes at $F000
    void reset();

    // Runs the machine for (at least) the given number of CPU cycles.
    void run(int64_t cycles);
    // Produces analog-section samples at kAnalogHz into `out` (mono). Runs the CPU as needed.
    void render(float* out, int frames);

    void midiIn(uint8_t b) { acia_.receive(b); }
    void setKey(int key, bool down);
    void setPitchWheel(uint8_t v) { pitch_ = v; }
    void setModWheel(uint8_t v) { mod_ = v; }
    void setAudioIn(float v) { audioIn_ = v; }

    // Display: segment bits (a..g = bit0..6, dp = bit7) for the left and right digit,
    // lit at any time during the last ~20 ms.
    uint8_t displaySegments(int digit) const;
    // Brightness (0..1, relative to a segment that is always on) of each segment over the
    // last display window: the LEDs are multiplexed, so this is the duty cycle.
    float segmentLevel(int digit, int seg) const { return segLevel_[digit][seg]; }

    Wd1772& fdc() { return fdc_; }
    Cpu6809& cpu() { return cpu_; }
    Es5503& doc() { return doc_; }
    FilterTuning& filterTuning() { return tuning_; }
    const Cem3328& filter(int i) const { return vcf_[i]; }
    uint8_t* sampleRam() { return sram_.data(); }
    uint8_t* mainRam() { return ram_.data(); }
    uint64_t cycles() const { return cpu_.totalCycles; }

    // Bus6809
    uint8_t read(uint16_t a) override;
    void write(uint16_t a, uint8_t v) override;

private:
    // memory first: the DOC keeps a pointer to sample RAM
    std::vector<uint8_t> sram_ = std::vector<uint8_t>(128 * 1024, 0x7F);
    std::vector<uint8_t> ram_ = std::vector<uint8_t>(0x6000, 0);   // $8000-$DFFF
    std::vector<uint8_t> rom_ = std::vector<uint8_t>(0x1000, 0xFF);
    Cpu6809 cpu_;
    Via6522 via_;
    Acia6850 acia_;
    Wd1772 fdc_;
    Es5503 doc_;
    FilterTuning tuning_;
    std::array<Cem3328, 8> vcf_;


    int bank_ = 0;
    uint8_t portB_ = 0;
    uint8_t keys_[3] = {0, 0, 0};       // pressed bits per row
    uint8_t dac_ = 0;
    uint8_t pitch_ = 0x80, mod_ = 0x00;
    float audioIn_ = 0;

    uint64_t segAcc_[2][8] = {};
    float segLevel_[2][8] = {};
    uint8_t lastPA_ = 0xFF;
    uint64_t lastPAChange_ = 0, segWindowStart_ = 0;
    void creditSegments();

    int docAcc_ = 0;        // CPU cycles accumulated towards the next DOC frame
    int analogAcc_ = 0;     // CPU cycles accumulated towards the next analog sample
    float chanIn_[8] = {};  // sample-and-hold outputs of the DOC demultiplexer
    float mix_ = 0;
    uint32_t noise_ = 12345;         // summed VCF output
    std::vector<float> outQ_;

    void tick(int c);
    void analogStep();
    void updateIrqs();
    uint8_t adc();
};

} // namespace mirage
