// Host-independent wrapper around the emulated Mirage: resampling to the host rate,
// thread-safe front-panel / MIDI / disk access for the editor, fast disk loading.
// MIT licence.
#pragma once
#include "../emu/Mirage.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace mirage {

// Single-producer single-consumer byte ring (editor -> audio thread).
class ByteRing {
public:
    bool push(const uint8_t* p, int n)
    {
        const uint32_t w = w_.load(std::memory_order_relaxed), r = r_.load(std::memory_order_acquire);
        if (kSize - (w - r) < uint32_t(n)) return false;
        for (int i = 0; i < n; ++i) buf_[(w + uint32_t(i)) & (kSize - 1)] = p[i];
        w_.store(w + uint32_t(n), std::memory_order_release);
        return true;
    }
    template <class F> void drain(F&& f)
    {
        const uint32_t w = w_.load(std::memory_order_acquire);
        uint32_t r = r_.load(std::memory_order_relaxed);
        for (; r != w; ++r) f(buf_[r & (kSize - 1)]);
        r_.store(r, std::memory_order_release);
    }
private:
    static constexpr uint32_t kSize = 4096;
    uint8_t buf_[kSize];
    std::atomic<uint32_t> w_{0}, r_{0};
};

class MirageEngine {
public:
    MirageEngine(const std::vector<uint8_t>& rom, const std::vector<uint8_t>& bootDisk, const std::string& bootDiskName);

    void setSampleRate(double fs);
    // Audio thread.
    void midiByte(uint8_t b) { m_.midiIn(b); }
    void process(float* out, int frames);   // mono

    // Editor thread (thread-safe).
    void setPanelKey(int key, bool down);
    void postMidi(const uint8_t* p, int n) { uiMidi_.push(p, n); }
    void insertDisk(std::vector<uint8_t> image, const std::string& name, const std::string& path);
    uint8_t display(int digit) const { return digit ? disp1_.load() : disp0_.load(); }
    bool diskActive() const { return motor_.load(); }
    std::string diskName() const { std::lock_guard<std::mutex> l(nameLock_); return diskName_; }
    std::string diskPath() const { std::lock_guard<std::mutex> l(nameLock_); return diskPath_; }
    // If the inserted disk was written to since the last call, returns its image.
    bool takeWrittenDisk(std::vector<uint8_t>& image, std::string& path);
    void setDiskPath(const std::string& p) { std::lock_guard<std::mutex> l(nameLock_); diskPath_ = p; }

    std::atomic<float> volume{0.8f};
    std::atomic<bool> fastDisk{true};
    std::atomic<int> diskTurbo{8};

    Mirage& machine() { return m_; }

private:
    Mirage m_;
    double fs_ = 48000.0;
    double step_ = Mirage::kAnalogHz / 48000.0;  // analog samples per host sample
    double phase_ = 0.0;
    float hist_[4] = {};    // last 4 filtered analog samples, for cubic interpolation
    struct Biquad { double b0, b1, b2, a1, a2, z1 = 0, z2 = 0;
        float run(float x) { double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return float(y); } };
    Biquad aa_[4];          // 8th-order Butterworth anti-alias filter at the analog rate
    std::vector<float> scratch_;

    ByteRing uiMidi_;
    std::atomic<uint32_t> keys_{0}, appliedKeys_{0};

    std::mutex diskLock_;   // pending disk + written disk
    bool diskPending_ = false;
    std::vector<uint8_t> pendingImage_;
    std::string pendingName_, pendingPath_;
    bool writtenReady_ = false;
    std::vector<uint8_t> writtenImage_;
    std::string writtenPath_;

    mutable std::mutex nameLock_;
    std::string diskName_, diskPath_;

    std::atomic<uint8_t> disp0_{0}, disp1_{0};
    std::atomic<bool> motor_{false};
    bool wasMotor_ = false;

    void designFilter();
    void boot();
    static int keyBit(int key) { return ((key >> 4) & 3) * 8 + (key & 7); }
};

} // namespace mirage
