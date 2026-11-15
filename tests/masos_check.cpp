// Runs the editor's protocol code (MasosLink) against the emulated Mirage booted from a
// MASOS disk: connection, both program dumps, and (with --write, for MASOS_BE) parameter
// writes read back from the Mirage.
// usage: mirage_masos_check <masos disk .img/.hfe> [--write]
#include "engine/DiskImage.h"
#include "engine/MasosLink.h"
#include "engine/MirageEngine.h"
#include "EmbeddedData.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

using namespace mirage;

int main(int argc, char** argv)
{
    if (argc < 2) { std::fprintf(stderr, "usage: %s disk [--write]\n", argv[0]); return 2; }
    const bool write = argc > 2 && std::strcmp(argv[2], "--write") == 0;
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<uint8_t> file((std::istreambuf_iterator<char>(f)), {}), image;
    std::string err;
    if (!decodeDiskImage(file, image, err)) { std::printf("bad disk: %s\n", err.c_str()); return 2; }

    MirageEngine e(std::vector<uint8_t>(embedded::kRom, embedded::kRom + embedded::kRomSize), image, "masos");
    e.setSampleRate(48000);
    MasosLink link;
    std::vector<float> buf(256);
    double t = 0;
    auto run = [&](double seconds) {
        for (double end = t + seconds * 1000; t < end; t += 256 / 48.0) {
            e.process(buf.data(), 256);
            e.drainEditorMidiOut([&](uint8_t b) { link.receive(b); });
            link.pump(t, [&](const uint8_t* p, int n) { e.postMidi(p, n); });
        }
    };

    run(3);
    const auto& lo = link.half(0);
    const auto& up = link.half(1);
    std::printf("connected %d, lower dump %d, upper dump %d, current half %d\n", link.connected(), lo.valid, up.valid,
                link.currentHalf());
    bool ok = link.connected() && lo.valid && up.valid;
    for (int h = 0; h < 2; ++h) {
        const auto& hf = link.half(h);
        std::printf("%s: program %d, wavesample %d, cutoff %s, resonance %s, amp sustain %s\n", h ? "upper" : "lower",
                    hf.program + 1, hf.wavesample + 1,
                    masosDisplay(*masosParam(36), link.value(h, 36)).c_str(),
                    masosDisplay(*masosParam(37), link.value(h, 37)).c_str(),
                    masosDisplay(*masosParam(53), link.value(h, 53)).c_str());
        for (int ws = 0; ws < 8; ++ws) {
            const int s = hf.wavesampleRaw(ws, *masosParam(60)), en = hf.wavesampleRaw(ws, *masosParam(61));
            const int top = hf.wavesampleRaw(ws, *masosParam(72));
            // sample memory: lower half in the first 64 KB, upper in the second
            const uint8_t* ram = e.machine().sampleRam() + (h << 16);
            int nonFlat = 0;
            for (int i = s * 256; i < (en + 1) * 256 && i < 65536; ++i) if (ram[i] != 0x80 && ram[i] != 0x7F) ++nonFlat;
            if (s || en) std::printf("  ws%d pages %02X-%02X top key %d, %d non-silent bytes\n", ws + 1, s, en, top + 1, nonFlat);
        }
    }

    if (write) {
        // Filter cutoff of the selected lower program, and the top key of a lower wavesample.
        const int cutoff = 100, topKey = 30;
        link.selectWavesample(0, 0);
        run(0.3);
        link.setParam(0, 36, cutoff);
        link.setParam(0, 72, topKey);
        run(0.5);
        link.requestHalf(0);
        // forget the local copy so the read-back is the Mirage's
        run(1.5);
        const int gotCut = link.value(0, 36), gotTop = link.value(0, 72);
        std::printf("write: cutoff %d -> %d, top key %d -> %d\n", cutoff, gotCut, topKey, gotTop);
        ok = ok && gotCut == cutoff && gotTop == topKey;

        // A front-panel edit reaches the editor: PARAM 3 6, VALUE, then ▲ once.
        auto press = [&](int key) { e.setPanelKey(key, true); run(0.1); e.setPanelKey(key, false); run(0.15); };
        for (int k : {Mirage::Param, Mirage::K3, Mirage::K6, Mirage::Value, Mirage::Up}) press(k);
        run(0.3);
        const int panel = link.value(0, 36);
        std::printf("front panel: cutoff %d -> %d\n", cutoff, panel);
        ok = ok && panel == cutoff + 2;
    }
    std::printf("%s\n", ok ? "MASOS OK" : "MASOS FAIL");
    return ok ? 0 : 1;
}
