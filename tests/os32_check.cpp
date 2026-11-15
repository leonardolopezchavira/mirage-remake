// The editor's OS 3.2 link (no MASOS_BE): connection, reading both halves from memory,
// direct and key-press edits applied by the OS, selection of program / wavesample / half,
// and front-panel edits showing up in the editor's model.
#include "engine/Os32Link.h"
#include "EmbeddedData.h"
#include <cmath>
#include <cstdio>

using namespace mirage;

int main()
{
    std::vector<uint8_t> disk(embedded::kBootDisk, embedded::kBootDisk + embedded::kBootDiskSize);
    MirageEngine e(std::vector<uint8_t>(embedded::kRom, embedded::kRom + embedded::kRomSize), disk, "os32");
    e.setSampleRate(48000);
    Os32Link link(e, std::vector<uint8_t>(disk.begin() + 2048, disk.begin() + 3072), 0x8800);
    std::vector<float> buf(256);
    double t = 0;
    auto run = [&](double ms) {
        for (double end = t + ms; t < end; t += 256 / 48.0) { e.process(buf.data(), 256); link.update(t); }
    };
    auto settle = [&]() { run(50); while (link.busy() && t < 600000) run(50); run(200); };
    auto ram = [&](int a) { return int(e.machine().mainRam()[a - 0x8000]); };
    // brightness of a note: rms of the first difference over the rms
    auto bright = [&](int note) {
        e.midiByte(0x90); e.midiByte(uint8_t(note)); e.midiByte(100);
        std::vector<float> rec;
        for (int i = 0; i < 120; ++i) { e.process(buf.data(), 256); rec.insert(rec.end(), buf.begin(), buf.end()); }
        e.midiByte(0x80); e.midiByte(uint8_t(note)); e.midiByte(0);
        run(1200);
        double a = 0, d = 0;
        for (size_t i = 4801; i < rec.size(); ++i) { a += rec[i] * rec[i]; d += (rec[i] - rec[i - 1]) * (rec[i] - rec[i - 1]); }
        return std::sqrt(d / std::max(a, 1e-12));
    };
    bool ok = true;
    auto check = [&](const char* what, bool c) { std::printf("%-46s %s\n", what, c ? "ok" : "FAIL"); ok = ok && c; };

    run(300);
    check("connected to OS 3.2", link.connected() && link.half(0).valid && link.half(1).valid);
    const int cut0 = link.value(0, 36);
    check("lower cutoff read from memory", cut0 == ram(0xB31D + 481 + 6));

    // direct edit: cutoff, heard on the next note
    const double b0 = bright(60);
    link.setParam(0, 36, 120);
    settle();
    const double b1 = bright(60);
    std::printf("  brightness %.3f -> %.3f\n", b0, b1);
    check("cutoff written and brighter", ram(0xB31D + 481 + 6) == 120 && b1 > b0 * 1.1);

    // key-press edits: top key of wavesample 1 (key map rebuilt), loop end, loop switch
    link.setParam(0, 72, 20);
    settle();
    check("top key 72 = 20 (via panel)", ram(0xB31E + 14) == 20 && link.value(0, 72) == 20);
    const int le = ram(0xB31E + 18);       // the loop end can't go below the loop start:
    link.setParam(0, 62, le - 4);          // move the start down first, as on the panel
    settle();
    link.setParam(0, 63, le - 2);
    settle();
    std::printf("  loop %02X-%02X (end was %02X)\n", ram(0xB31E + 17), ram(0xB31E + 18), le);
    check("loop start and end moved by the OS", ram(0xB31E + 17) == le - 4 && ram(0xB31E + 18) == le - 2);
    const int se = ram(0xB31E + 16);
    link.setParam(0, 61, se - 0x10);       // sample end: the OS moves the loop to the new end
    settle();
    std::printf("  sample end %02X -> %02X, loop %02X-%02X\n", se, ram(0xB31E + 16), ram(0xB31E + 17), ram(0xB31E + 18));
    check("sample end moved by the OS", ram(0xB31E + 16) == se - 0x10);
    link.setParam(0, 27, 1);               // initial wavesample 2 (key map rebuilt)
    settle();
    check("initial wavesample 27 = 2", ram(0xB31D + 481 + 10) == 1);
    link.setParam(0, 27, 0);
    settle();
    link.setParam(0, 65, 1);
    settle();
    check("loop switched on", ram(0xB31E + 8) == 1);

    // selection
    link.selectProgram(0, 2);
    settle();
    check("lower program 3 selected", ram(0x808F) == 2 && link.half(0).program == 2);
    link.selectWavesample(0, 3);
    settle();
    check("lower wavesample 4 selected", ram(0x80AA) == 3 && link.half(0).wavesample == 3);
    link.selectProgram(1, 1);
    settle();
    check("upper half, program 2 selected", ram(0x80A4) == 1 && ram(0x808E) == 1 && link.currentHalf() == 1);
    link.setParam(1, 53, 31);   // amp sustain of upper program 2
    settle();
    check("upper program 2 amp sustain written", ram(0xB58E + 481 + 36 + 25) == 31);

    // a front-panel edit shows up: PARAM 3 6 VALUE ▲ on the upper half
    const int before = link.value(1, 36);
    for (int k : {Mirage::Param, Mirage::K3, Mirage::K6, Mirage::Value, Mirage::Up}) {
        e.setPanelKey(k, true); run(100); e.setPanelKey(k, false); run(120);
    }
    run(200);
    check("front-panel edit seen by the editor", link.value(1, 36) == before + 2);

    std::printf("%s\n", ok ? "OS32 OK" : "OS32 FAIL");
    return ok ? 0 : 1;
}
