// Model of one Curtis CEM3328 4-pole low-pass VCF as wired in the Mirage: cutoff and
// resonance come from 8-bit DAC values held on sample-and-hold capacitors.
// Topology-preserving (TPT) 4-pole ladder with a soft-saturating feedback path, so that it
// self-oscillates at high resonance (the boot ROM's filter tuning relies on this).
// MIT licence.
#pragma once
#include <cmath>
#include <cstdint>

namespace mirage {

struct FilterTuning {
    // Cutoff (Hz) for DAC value `refCode`, and DAC steps per octave (exponential CV law).
    double refHz = 1231.0;
    double refCode = 0xB0;
    double stepsPerOctave = 24.0;   // measured: OS "tracking 4 = 1 octave" moves 2 codes per semitone
    double maxHz = 22000.0;
    double minHz = 5.0;
    // Feedback gain at resonance DAC = 255 (4.0 is the onset of self-oscillation).
    double maxResonance = 4.25;
    double inputGain = 0.5;
};

class Cem3328 {
public:
    void setSampleRate(double fs) { fs_ = fs; dirty_ = true; }
    void setTuning(const FilterTuning* t) { tune_ = t; dirty_ = true; }
    void setDetune(double steps) { detune_ = steps; dirty_ = true; } // component tolerance
    void setCutoffCode(uint8_t c) { if (c != cut_) { cut_ = c; dirty_ = true; } }
    void setResonanceCode(uint8_t r) { res_ = r; k_ = tune_->maxResonance * r / 255.0; }
    uint8_t cutoffCode() const { return cut_; }
    uint8_t resonanceCode() const { return res_; }

    void reset() { s1_ = s2_ = s3_ = s4_ = 0; y_ = 0; }

    double cutoffHz() const
    {
        double hz = tune_->refHz * std::exp2((cut_ - tune_->refCode + detune_) / tune_->stepsPerOctave);
        if (hz > tune_->maxHz) hz = tune_->maxHz;
        if (hz < tune_->minHz) hz = tune_->minHz;
        if (hz > fs_ * 0.45) hz = fs_ * 0.45;
        return hz;
    }

    float process(float in)
    {
        if (dirty_) update();
        const double x = in * tune_->inputGain;
        // Linear estimate of the 4th-stage output, then a saturating input stage.
        const double S = G3_ * s1_ * a_ + G2_ * s2_ * a_ + G_ * s3_ * a_ + s4_ * a_;
        const double y4est = (G4_ * x + S) / (1.0 + k_ * G4_);
        const double u = std::tanh(x - k_ * y4est);
        double v, y;
        v = (u - s1_) * G_;  y = v + s1_; s1_ = y + v;
        v = (y - s2_) * G_;  y = v + s2_; s2_ = y + v;
        v = (y - s3_) * G_;  y = v + s3_; s3_ = y + v;
        v = (y - s4_) * G_;  y = v + s4_; s4_ = y + v;
        y_ = y;
        return float(y);
    }
    float output() const { return float(y_); }

private:
    const FilterTuning* tune_ = nullptr;
    double fs_ = 125000.0;
    double detune_ = 0;
    uint8_t cut_ = 0, res_ = 0;
    double k_ = 0;
    double G_ = 0, G2_ = 0, G3_ = 0, G4_ = 0, a_ = 0;
    double s1_ = 0, s2_ = 0, s3_ = 0, s4_ = 0, y_ = 0;
    bool dirty_ = true;

    void update()
    {
        const double g = std::tan(M_PI * cutoffHz() / fs_);
        G_ = g / (1.0 + g);
        G2_ = G_ * G_; G3_ = G2_ * G_; G4_ = G3_ * G_;
        a_ = 1.0 / (1.0 + g);
        dirty_ = false;
    }
};

} // namespace mirage
