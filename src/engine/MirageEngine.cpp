#include "MirageEngine.h"
#include <algorithm>
#include <cmath>

namespace mirage {

MirageEngine::MirageEngine(const std::vector<uint8_t>& rom, const std::vector<uint8_t>& bootDisk,
                           const std::string& bootDiskName)
{
    m_.loadRom(rom);
    m_.reset();
    m_.fdc().insert(bootDisk);
    diskName_ = bootDiskName;
    designFilter();
    boot();
}

void MirageEngine::boot()
{
    // Load the OS and the first sounds the way the instrument does at power-on, but
    // faster than real time: run until the drive has been on and gone off again.
    // Done when the display is lit and the drive has been off for half a second.
    int idle = 0;
    for (int i = 0; i < 600 && idle < 10; ++i) {   // at most 30 s of machine time
        m_.run(int64_t(0.05 * Mirage::kCpuHz));
        const bool lit = m_.displaySegments(0) || m_.displaySegments(1);
        idle = (!m_.fdc().motorOn() && lit) ? idle + 1 : 0;
    }
    m_.run(int64_t(0.2 * Mirage::kCpuHz));

    // Master Tune (parameter 21) defaults to 50, which plays about 14 cents sharp of A440
    // on every key. 47 is the closest setting (measured within +-2 cents across the
    // keyboard), so set it through the front panel as a player would:
    // PARAM 2 1 VALUE, three times Off/Down, then back to PARAM.
    for (int key : {Mirage::Param, Mirage::K2, Mirage::K1, Mirage::Value,
                    Mirage::Down, Mirage::Down, Mirage::Down, Mirage::Param}) {
        m_.setKey(key, true);
        m_.run(int64_t(0.08 * Mirage::kCpuHz));
        m_.setKey(key, false);
        m_.run(int64_t(0.08 * Mirage::kCpuHz));
    }
    m_.fdc().clearDirty();
}

void MirageEngine::setSampleRate(double fs)
{
    fs_ = fs;
    step_ = Mirage::kAnalogHz / fs;
    designFilter();
}

void MirageEngine::designFilter()
{
    // 8th-order Butterworth low-pass as four biquads (bilinear transform).
    const double fc = std::min(18000.0, 0.42 * fs_);
    const double K = std::tan(M_PI * fc / Mirage::kAnalogHz);
    for (int i = 0; i < 4; ++i) {
        const double q = 1.0 / (2.0 * std::cos(M_PI * (2 * i + 1) / 16.0));
        const double norm = 1.0 / (1.0 + K / q + K * K);
        Biquad& b = aa_[i];
        b.b0 = K * K * norm; b.b1 = 2 * b.b0; b.b2 = b.b0;
        b.a1 = 2 * (K * K - 1) * norm;
        b.a2 = (1 - K / q + K * K) * norm;
        b.z1 = b.z2 = 0;
    }
}

void MirageEngine::setPanelKey(int key, bool down)
{
    const uint32_t bit = 1u << keyBit(key);
    if (down) keys_.fetch_or(bit);
    else keys_.fetch_and(~bit);
}

void MirageEngine::insertDisk(std::vector<uint8_t> image, const std::string& name, const std::string& path)
{
    std::lock_guard<std::mutex> l(diskLock_);
    pendingImage_ = std::move(image);
    pendingName_ = name;
    pendingPath_ = path;
    diskPending_ = true;
}

bool MirageEngine::takeWrittenDisk(std::vector<uint8_t>& image, std::string& path)
{
    std::lock_guard<std::mutex> l(diskLock_);
    if (!writtenReady_) return false;
    image.swap(writtenImage_);
    path = writtenPath_;
    writtenReady_ = false;
    return true;
}

void MirageEngine::process(float* out, int frames)
{
    // Editor input
    uiMidi_.drain([this](uint8_t b) { m_.midiIn(b); });
    const uint32_t k = keys_.load(), changed = k ^ appliedKeys_.load();
    if (changed) {
        for (int bit = 0; bit < 24; ++bit)
            if (changed & (1u << bit)) m_.setKey(((bit / 8) << 4) | (bit % 8), (k >> bit) & 1);
        appliedKeys_.store(k);
    }
    if (diskLock_.try_lock()) {
        if (diskPending_) {
            if (m_.fdc().insert(pendingImage_)) {
                std::lock_guard<std::mutex> l(nameLock_);
                diskName_ = pendingName_;
                diskPath_ = pendingPath_;
            }
            diskPending_ = false;
        }
        diskLock_.unlock();
    }

    // Fast disk: while the drive runs, let the machine run ahead (the sound is muted by the
    // OS during loading anyway).
    const bool motor = m_.fdc().motorOn();
    if (motor && fastDisk.load()) {
        const int turbo = std::max(1, diskTurbo.load());
        m_.run(int64_t(double(frames) / fs_ * Mirage::kCpuHz * (turbo - 1)));
    }

    // Analog samples needed for this block.
    const double needF = phase_ + frames * step_;
    const int need = int(std::floor(needF));
    if (int(scratch_.size()) < need + 4) scratch_.resize(size_t(need) + 4);
    m_.render(scratch_.data(), need);

    const float gain = volume.load() * 0.8f;
    int consumed = 0;
    for (int i = 0; i < frames; ++i) {
        phase_ += step_;
        while (phase_ >= 1.0 && consumed < need) {
            float x = scratch_[size_t(consumed++)];
            for (auto& b : aa_) x = b.run(x);
            hist_[0] = hist_[1]; hist_[1] = hist_[2]; hist_[2] = hist_[3]; hist_[3] = x;
            phase_ -= 1.0;
        }
        // Catmull-Rom between hist_[1] and hist_[2]
        const float t = float(phase_);
        const float p0 = hist_[0], p1 = hist_[1], p2 = hist_[2], p3 = hist_[3];
        const float y = p1 + 0.5f * t * (p2 - p0 + t * (2 * p0 - 5 * p1 + 4 * p2 - p3 + t * (3 * (p1 - p2) + p3 - p0)));
        out[i] = std::clamp(y * gain, -1.0f, 1.0f);
    }

    disp0_.store(m_.displaySegments(0));
    disp1_.store(m_.displaySegments(1));
    motor_.store(motor);

    // Hand a written disk to the editor once the drive stops.
    if (wasMotor_ && !motor && m_.fdc().dirty()) {
        if (diskLock_.try_lock()) {
            writtenImage_ = m_.fdc().image();
            {
                std::lock_guard<std::mutex> l(nameLock_);
                writtenPath_ = diskPath_;
            }
            writtenReady_ = true;
            m_.fdc().clearDirty();
            diskLock_.unlock();
        }
    }
    wasMotor_ = motor;
}

} // namespace mirage
