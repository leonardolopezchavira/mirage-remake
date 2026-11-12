#include "Mirage.h"
#include <algorithm>
#include <cmath>

namespace mirage {

Mirage::Mirage() : cpu_(*this), doc_(sram_.data())
{
    via_.readPortA = [this]() -> uint8_t {
        const int col = via_.portAOut() & 7;
        uint8_t v = 0xE0;
        for (int r = 0; r < 3; ++r)
            if (keys_[r] & (1 << col)) v &= uint8_t(~(0x20 << r));
        return uint8_t(v | 0x1F);
    };
    via_.onPortA = [this](uint8_t v) {
        // PA0-2 select a segment (through a 74LS145), PA3/PA4 enable the left/right digit.
        creditSegments();
        lastPA_ = v;
    };
    via_.readPortB = [this]() -> uint8_t {
        uint8_t v = 0;
        if ((cpu_.totalCycles >> 1) & 1) v |= 0x20;    // DOC CA3 clock (500 kHz)
        if (fdc_.hasDisk()) v |= 0x40;                 // disk present
        return v;
    };
    via_.onPortB = [this](uint8_t v) {
        portB_ = v;
        bank_ = v & 3;
        fdc_.setMotor(!(v & 0x10));
    };
    doc_.readAdc = [this]() { return adc(); };

    static const double kDetune[8] = { 0.7, -1.1, 0.3, 1.6, -0.4, -1.8, 1.0, -0.2 };
    for (int i = 0; i < 8; ++i) {
        vcf_[i].setTuning(&tuning_);
        vcf_[i].setSampleRate(kAnalogHz);
        vcf_[i].setDetune(kDetune[i]);
    }
}

bool Mirage::loadRom(const std::vector<uint8_t>& rom)
{
    if (rom.size() != 0x1000) return false;
    rom_ = rom;
    return true;
}

void Mirage::reset()
{
    via_.reset();
    acia_.reset();
    fdc_.reset();
    doc_.reset();
    for (auto& f : vcf_) f.reset();
    bank_ = 0;
    docAcc_ = analogAcc_ = 0;
    cpu_.reset();
}

uint8_t Mirage::read(uint16_t a)
{
    if (a < 0x8000) return sram_[size_t(bank_) << 15 | a];
    if (a < 0xE000) return ram_[a - 0x8000];
    if (a >= 0xF000) return rom_[a - 0xF000];
    if (a >= 0xEC00 && a <= 0xECFF) return doc_.read(a & 0xFF);
    if ((a & 0xFF00) == 0xE200) { uint8_t v = via_.read(a & 0xF); updateIrqs(); return v; }
    if (a >= 0xE800 && a <= 0xE803) { uint8_t v = fdc_.read(a & 3); updateIrqs(); return v; }
    if (a == 0xE100 || a == 0xE101) { uint8_t v = acia_.read(a & 1); updateIrqs(); return v; }
    return 0xFF;
}

void Mirage::write(uint16_t a, uint8_t v)
{
    if (a < 0x8000) { sram_[size_t(bank_) << 15 | a] = v; return; }
    if (a < 0xE000) { ram_[a - 0x8000] = v; return; }
    if (a >= 0xF000) return;
    if (a >= 0xEC00 && a <= 0xECFF) { doc_.write(a & 0xFF, v); updateIrqs(); return; }
    if ((a & 0xFF00) == 0xE200) { via_.write(a & 0xF, v); updateIrqs(); return; }
    if (a >= 0xE800 && a <= 0xE803) { fdc_.write(a & 3, v); updateIrqs(); return; }
    if (a == 0xE100 || a == 0xE101) { acia_.write(a & 1, v); updateIrqs(); return; }
    if (a >= 0xE400 && a <= 0xE41F) {
        // $E418-F: preset the multiplexed DAC. $E400-7: strobe cutoff+resonance S&H,
        // $E408-F: resonance S&H, $E410-7: cutoff S&H (per channel = low 3 bits).
        const int ch = a & 7, sel = (a >> 3) & 3;
        dac_ = v;
        if (sel != 3) {
            if (!(sel & 2)) vcf_[ch].setResonanceCode(dac_);
            if (!(sel & 1)) vcf_[ch].setCutoffCode(dac_);
        }
        return;
    }
}

void Mirage::setKey(int key, bool down)
{
    const int row = (key >> 4) & 3, col = key & 7;
    if (row > 2) return;
    if (down) keys_[row] |= uint8_t(1 << col);
    else keys_[row] &= uint8_t(~(1 << col));
}

void Mirage::creditSegments()
{
    const uint64_t now = cpu_.totalCycles;
    const uint64_t dt = now - lastPAChange_;
    const int seg = lastPA_ & 7;
    if (!(lastPA_ & 0x08)) segAcc_[0][seg] += dt;  // left digit anode (PA3, active low)
    if (!(lastPA_ & 0x10)) segAcc_[1][seg] += dt;  // right digit anode (PA4, active low)
    lastPAChange_ = now;

    const uint64_t window = now - segWindowStart_;
    if (window >= 40000) { // 20 ms
        for (int d = 0; d < 2; ++d)
            for (int s = 0; s < 8; ++s) {
                // One of 8 segment lines is driven at a time: full duty for a segment is 1/8.
                segLevel_[d][s] = std::min(1.0f, float(segAcc_[d][s]) * 8.0f / float(window));
                segAcc_[d][s] = 0;
            }
        segWindowStart_ = now;
    }
}

uint8_t Mirage::displaySegments(int digit) const
{
    uint8_t bits = 0;
    for (int s = 0; s < 8; ++s)
        if (segLevel_[digit][s] > 0.25f) bits |= uint8_t(1 << s);
    return bits;
}

uint8_t Mirage::adc()
{
    // VIA PB2-3 select the A/D input: 0 = compressed (mic) input, 1 = line input, 2 = pitch
    // wheel, 3 = mod wheel. The audio input reaches the converter through VCF 7 (the
    // sampling "input filter", see analogStep()), mixed with the other filters' outputs.
    switch ((portB_ >> 2) & 3) {
    case 2: return pitch_;
    case 3: return mod_;
    default: {
        const int v = int(std::lround(128.0f + 300.0f * mix_));
        return uint8_t(std::clamp(v, 0, 255));
    }
    }
}

void Mirage::updateIrqs()
{
    cpu_.setIrq(via_.irq() || fdc_.drq() || doc_.irq());
    cpu_.setFirq(acia_.irq());
    cpu_.setNmi(fdc_.intrq());
}

void Mirage::analogStep()
{
    // A little analog noise at each filter input (the real VCFs never sit at exactly zero,
    // which is what lets them start self-oscillating during the boot-time tuning).
    if (inBuf_ && inPos_ < inLen_) audioIn_ = inBuf_[inPos_++];

    // While sampling, the audio input feeds VCF 7, which the OS sets up as the anti-alias
    // "input filter" (parameter 74). VIA PB2-3 pick the source: 01 = line input,
    // 00 = microphone input with its compressor (parameter 75).
    float in7 = 0;
    if (sampling()) {
        const int src = (portB_ >> 2) & 3;
        if (src == 1) in7 = audioIn_;
        else if (src == 0) in7 = 0.8f * std::tanh(4.0f * audioIn_);
        const float a = std::fabs(in7);
        if (a > inLevel_) inLevel_ = a;
    }

    float sum = 0;
    for (int i = 0; i < 8; ++i) {
        noise_ = noise_ * 1664525u + 1013904223u;
        const float n = (int32_t(noise_) >> 8) * (1.0f / 8388608.0f) * 1e-4f;
        sum += vcf_[i].process(chanIn_[i] + n + (i == 7 ? in7 : 0.0f));
    }
    mix_ = sum;
    outQ_.push_back(sum);
}

void Mirage::tick(int c)
{
    via_.tick(c);
    acia_.tick(c);
    fdc_.tick(c);

    docAcc_ += c;
    const int docFrameCpu = doc_.frameClocks() / 4; // 8 MHz DOC clocks -> 2 MHz CPU cycles
    while (docAcc_ >= docFrameCpu) {
        docAcc_ -= docFrameCpu;
        doc_.frame();
        for (int i = 0; i < 8; ++i) chanIn_[i] = doc_.channel(i) * (1.0f / 32768.0f);
    }
    analogAcc_ += c;
    while (analogAcc_ >= kAnalogDiv) {
        analogAcc_ -= kAnalogDiv;
        analogStep();
    }
    updateIrqs();
}

void Mirage::run(int64_t cycles)
{
    const uint64_t end = cpu_.totalCycles + uint64_t(cycles);
    while (cpu_.totalCycles < end) tick(cpu_.step());
    outQ_.clear();
}

void Mirage::render(float* out, int frames, const float* in)
{
    inBuf_ = in;
    inPos_ = 0;
    inLen_ = in ? frames : 0;
    while (int(outQ_.size()) < frames) tick(cpu_.step());
    std::copy(outQ_.begin(), outQ_.begin() + frames, out);
    outQ_.erase(outQ_.begin(), outQ_.begin() + frames);
    inBuf_ = nullptr;
}

} // namespace mirage
