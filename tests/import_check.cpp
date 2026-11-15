// Audio file import: writes a 440 Hz WAV (16-bit stereo, 44.1 kHz) and an AIFF, reads them
// back, imports the WAV into the lower half through the emulated sampling input, and checks
// that the Mirage finished ("SF") and plays it at about 440.8 Hz on MIDI 52, as a sound
// sampled through the input does.
#include "engine/AudioFile.h"
#include "engine/SampleImport.h"
#include "EmbeddedData.h"
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <iterator>
#include "engine/DiskImage.h"

using namespace mirage;

static void put16(std::vector<uint8_t>& v, int x, bool be) { if (be) { v.push_back(uint8_t(x >> 8)); v.push_back(uint8_t(x)); } else { v.push_back(uint8_t(x)); v.push_back(uint8_t(x >> 8)); } }
static void put32(std::vector<uint8_t>& v, uint32_t x, bool be) { for (int i = 0; i < 4; ++i) v.push_back(uint8_t(be ? x >> (24 - 8 * i) : x >> (8 * i))); }

int main(int argc, char** argv)
{
    const int rate = 44100, n = rate * 3 / 2;
    std::vector<int16_t> pcm(size_t(n) * 2);
    for (int i = 0; i < n; ++i) pcm[size_t(i) * 2] = pcm[size_t(i) * 2 + 1] = int16_t(20000 * std::sin(2 * M_PI * 440.0 * i / rate));

    std::vector<uint8_t> wav = {'R', 'I', 'F', 'F'};
    put32(wav, uint32_t(36 + n * 4), false);
    for (char c : std::string("WAVEfmt ")) wav.push_back(uint8_t(c));
    put32(wav, 16, false); put16(wav, 1, false); put16(wav, 2, false); put32(wav, rate, false);
    put32(wav, rate * 4, false); put16(wav, 4, false); put16(wav, 16, false);
    for (char c : std::string("data")) wav.push_back(uint8_t(c));
    put32(wav, uint32_t(n * 4), false);
    for (int16_t s : pcm) put16(wav, s, false);

    std::vector<uint8_t> aiff = {'F', 'O', 'R', 'M'};
    put32(aiff, uint32_t(4 + 26 + 16 + n * 4), true);
    for (char c : std::string("AIFFCOMM")) aiff.push_back(uint8_t(c));
    put32(aiff, 18, true); put16(aiff, 2, true); put32(aiff, uint32_t(n), true); put16(aiff, 16, true);
    const uint8_t r44100[10] = {0x40, 0x0E, 0xAC, 0x44, 0, 0, 0, 0, 0, 0};
    aiff.insert(aiff.end(), r44100, r44100 + 10);
    for (char c : std::string("SSND")) aiff.push_back(uint8_t(c));
    put32(aiff, uint32_t(8 + n * 4), true); put32(aiff, 0, true); put32(aiff, 0, true);
    for (int16_t s : pcm) put16(aiff, s, true);

    AudioData a, b;
    std::string err;
    const bool wavOk = readAudioFile(wav, a, err) && a.mono.size() == size_t(n) && a.sampleRate == rate;
    const bool aiffOk = readAudioFile(aiff, b, err) && b.mono.size() == size_t(n) && b.sampleRate == rate &&
                        std::fabs(a.mono[1000] - b.mono[1000]) < 1e-6f;
    std::printf("read: wav %d, aiff %d %s\n", wavOk, aiffOk, err.c_str());

    // optional: boot from another disk (e.g. MASOS_BE) instead of the built-in OS 3.2
    std::vector<uint8_t> boot(embedded::kBootDisk, embedded::kBootDisk + embedded::kBootDiskSize);
    if (argc > 1) {
        std::ifstream f(argv[1], std::ios::binary);
        std::vector<uint8_t> file((std::istreambuf_iterator<char>(f)), {});
        if (!decodeDiskImage(file, boot, err)) { std::printf("bad disk %s\n", err.c_str()); return 2; }
    }
    MirageEngine e(std::vector<uint8_t>(embedded::kRom, embedded::kRom + embedded::kRomSize), boot, "boot");
    e.setSampleRate(48000);
    std::vector<float> buf(256), silence(256, 0.0f);
    double t = 0;
    SampleImport imp;
    imp.start(e, a.mono, a.sampleRate, 0, t);
    while (imp.state() == SampleImport::State::Running && t < 30000) {
        e.process(buf.data(), 256, silence.data());   // a live input that stays silent
        t += 256 / 48.0;
        imp.step(e, t);
        static int last = -1;
        const int d = e.display(0) << 8 | e.display(1);
        if (d != last && std::getenv("IMPORT_TRACE")) std::printf("  t=%.2f display %04X sampling %d\n", t / 1000, d, e.samplingNow.load());
        last = d;
    }
    const bool done = imp.state() == SampleImport::State::Done;
    std::printf("import: %s after %.1f s %s\n", done ? "done" : "FAILED", t / 1000, imp.message().c_str());

    // play it back
    for (int i = 0; i < 100; ++i) e.process(buf.data(), 256, silence.data());
    e.midiByte(0x90); e.midiByte(52); e.midiByte(100);
    std::vector<float> rec;
    for (int i = 0; i < 250; ++i) { e.process(buf.data(), 256, silence.data()); rec.insert(rec.end(), buf.begin(), buf.end()); }
    int zc = 0;
    for (size_t i = 9601; i < rec.size(); ++i) if ((rec[i - 1] < 0) != (rec[i] < 0)) ++zc;
    const double hz = zc / 2.0 / ((rec.size() - 9601) / 48000.0);
    std::printf("playback %.1f Hz\n", hz);
    const bool ok = wavOk && aiffOk && done && std::fabs(hz - 440.8) < 2.0;
    std::printf("%s\n", ok ? "IMPORT OK" : "IMPORT FAIL");
    return ok ? 0 : 1;
}
