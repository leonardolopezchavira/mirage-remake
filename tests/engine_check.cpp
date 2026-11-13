// Boots the embedded Mirage, plays a chord through the engine at 48 kHz and checks that it
// sounds, that every filter was tuned, and that it runs comfortably faster than real time.
#include "engine/MirageEngine.h"
#include "EmbeddedData.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace mirage;

int main()
{
    auto t0 = std::chrono::steady_clock::now();
    MirageEngine e(std::vector<uint8_t>(embedded::kRom, embedded::kRom + embedded::kRomSize),
                   std::vector<uint8_t>(embedded::kBootDisk, embedded::kBootDisk + embedded::kBootDiskSize),
                   embedded::kBootDiskName);
    auto t1 = std::chrono::steady_clock::now();
    e.setSampleRate(48000);
    std::printf("boot: %.2f s wall, machine at %.2f s, display %02X %02X\n",
                std::chrono::duration<double>(t1 - t0).count(), e.machine().cycles() / Mirage::kCpuHz,
                e.display(0), e.display(1));

    std::vector<float> buf(256);
    auto block = [&](int n, double& peak) {
        for (int i = 0; i < n; ++i) {
            e.process(buf.data(), 256);
            for (float v : buf) peak = std::fmax(peak, std::fabs(v));
        }
    };
    double quiet = 0, loud = 0, tail = 0;
    block(100, quiet);
    for (uint8_t n : {48, 52, 55, 60}) { e.midiByte(0x90); e.midiByte(n); e.midiByte(100); }
    auto t2 = std::chrono::steady_clock::now();
    block(375, loud); // 2 s
    auto t3 = std::chrono::steady_clock::now();
    for (uint8_t n : {48, 52, 55, 60}) { e.midiByte(0x80); e.midiByte(n); e.midiByte(64); }
    block(375, tail);
    const double rt = (375 * 256 / 48000.0) / std::chrono::duration<double>(t3 - t2).count();
    std::printf("silence peak %.4f, chord peak %.4f, after release %.4f, speed %.1fx real time\n", quiet, loud, tail, rt);

    // Sampling through the audio input, the way a player does it: SAMPLE LOWER, wait for
    // level-detect mode, ENTER, then play a 440 Hz tone; the display must end on "SF" and
    // the new sample must play back at 440 Hz on the E below middle C (MIDI 52).
    double ph = 0; bool sig = false;
    std::vector<float> in(256);
    auto blockIn = [&](int n, std::vector<float>* rec) {
        for (int b = 0; b < n; ++b) {
            for (auto& v : in) { v = sig ? 0.5f * float(std::sin(ph)) : 0.0f; ph += 2 * M_PI * 440.0 / 48000; }
            e.process(buf.data(), 256, in.data());
            if (rec) rec->insert(rec->end(), buf.begin(), buf.end());
        }
    };
    auto key = [&](int k) { e.setPanelKey(k, true); blockIn(19, nullptr); e.setPanelKey(k, false); blockIn(38, nullptr); };
    key(Mirage::SampleLower);
    blockIn(750, nullptr);                         // "SL", then level-detect mode
    const bool levelDetect = e.samplingNow.load();
    key(Mirage::Enter);
    sig = true;
    blockIn(750, nullptr);                         // threshold, ~2 s of recording
    sig = false;
    blockIn(375, nullptr);
    const bool finished = e.display(0) == 0x6D && e.display(1) == 0x71;  // "SF"
    std::vector<float> rec;
    e.midiByte(0x90); e.midiByte(52); e.midiByte(100);
    blockIn(250, &rec);
    // pitch by zero crossings over the sustained part
    int zc = 0;
    for (size_t i = 9600 + 1; i < rec.size(); ++i) if ((rec[i - 1] < 0) != (rec[i] < 0)) ++zc;
    const double hz = zc / 2.0 / ((rec.size() - 9601) / 48000.0);
    std::printf("sampling: level-detect %d, finished (SF) %d, playback %.1f Hz\n", levelDetect, finished, hz);

    bool ok = quiet < 0.01 && loud > 0.05 && rt > 2.0 && levelDetect && finished && std::fabs(hz - 440.0) < 2.0;
    std::printf("%s\n", ok ? "ENGINE OK" : "ENGINE FAIL");
    return ok ? 0 : 1;
}
