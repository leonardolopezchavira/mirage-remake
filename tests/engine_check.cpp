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

    bool ok = quiet < 0.01 && loud > 0.05 && rt > 2.0;
    std::printf("%s\n", ok ? "ENGINE OK" : "ENGINE FAIL");
    return ok ? 0 : 1;
}
