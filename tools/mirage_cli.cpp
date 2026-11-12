// Headless Mirage runner for testing.
// usage: mirage_cli rom.bin [script commands...]
//   disk <file.img>      insert disk image
//   run <seconds>        run the machine
//   key <name> [ms]      press a front-panel key (default 100 ms)
//   midi <hex bytes...>  send MIDI bytes (comma or space separated hex in one arg: 90,3c,7f)
//   wav <file> <seconds> render audio to a 16-bit WAV at 125 kHz -> 44.1 kHz
//   show                 print the display
//   regs                 print CPU registers
//   peek <hexaddr> <n>
#include "../src/emu/Mirage.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace mirage;

static std::vector<uint8_t> readFile(const std::string& p)
{
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

static char segChar(uint8_t s)
{
    static const std::map<uint8_t, char> m = {
        {0x3F, '0'}, {0x06, '1'}, {0x5B, '2'}, {0x4F, '3'}, {0x66, '4'}, {0x6D, '5'}, {0x7D, '6'},
        {0x07, '7'}, {0x7F, '8'}, {0x6F, '9'}, {0x77, 'A'}, {0x7C, 'b'}, {0x39, 'C'}, {0x5E, 'd'},
        {0x79, 'E'}, {0x71, 'F'}, {0x76, 'H'}, {0x38, 'L'}, {0x54, 'n'}, {0x5C, 'o'}, {0x73, 'P'},
        {0x50, 'r'}, {0x3E, 'U'}, {0x1C, 'u'}, {0x40, '-'}, {0x00, ' '}, {0x78, 't'}, {0x74, 'h'},
        {0x58, 'c'}, {0x1E, 'J'}, {0x6E, 'y'}, {0x10, 'i'}, {0x37, 'N'}, {0x3D, 'G'}, {0x08, '_'},
    };
    auto it = m.find(uint8_t(s & 0x7F));
    return it == m.end() ? '?' : it->second;
}

static void show(Mirage& m)
{
    uint8_t l = m.displaySegments(0), r = m.displaySegments(1);
    std::printf("[%c%s%c%s]  (segs %02X %02X)  t=%.3fs pc=%04X\n", segChar(l), (l & 0x80) ? "." : "",
                segChar(r), (r & 0x80) ? "." : "", l, r, m.cycles() / Mirage::kCpuHz, m.cpu().pc);
}

static const std::map<std::string, int> kKeys = {
    {"LOADUPPER", Mirage::LoadUpper}, {"LOADLOWER", Mirage::LoadLower}, {"SAMPLEUPPER", Mirage::SampleUpper},
    {"PLAYSEQ", Mirage::PlaySeq}, {"LOADSEQ", Mirage::LoadSeq}, {"SAVESEQ", Mirage::SaveSeq},
    {"RECSEQ", Mirage::RecSeq}, {"SAMPLELOWER", Mirage::SampleLower},
    {"0", Mirage::K0Prog}, {"1", Mirage::K1}, {"2", Mirage::K2}, {"3", Mirage::K3}, {"4", Mirage::K4},
    {"5", Mirage::K5}, {"6", Mirage::K6}, {"7", Mirage::K7}, {"8", Mirage::K8}, {"9", Mirage::K9},
    {"ENTER", Mirage::Enter}, {"UP", Mirage::Up}, {"DOWN", Mirage::Down}, {"PARAM", Mirage::Param},
    {"VALUE", Mirage::Value}, {"CANCEL", Mirage::Cancel},
};

static void writeWav(const std::string& path, const std::vector<float>& s, int rate)
{
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<char*>(&v), 2); };
    f.write("RIFF", 4); u32(uint32_t(36 + s.size() * 2)); f.write("WAVEfmt ", 8);
    u32(16); u16(1); u16(1); u32(uint32_t(rate)); u32(uint32_t(rate * 2)); u16(2); u16(16);
    f.write("data", 4); u32(uint32_t(s.size() * 2));
    for (float v : s) { int x = int(std::lround(std::fmax(-1.0, std::fmin(1.0, v)) * 32767)); u16(uint16_t(int16_t(x))); }
}

int main(int argc, char** argv)
{
    if (argc < 2) { std::fprintf(stderr, "usage: %s rom.bin [commands]\n", argv[0]); return 1; }
    Mirage m;
    if (!m.loadRom(readFile(argv[1]))) { std::fprintf(stderr, "bad rom\n"); return 1; }
    m.reset();
    for (int i = 2; i < argc; ++i) {
        std::string c = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (c == "disk") {
            auto p = next();
            if (!m.fdc().insert(readFile(p))) std::printf("disk: bad image %s\n", p.c_str());
        } else if (c == "eject") {
            m.fdc().eject();
        } else if (c == "run") {
            double sec = std::atof(next().c_str());
            m.run(int64_t(sec * Mirage::kCpuHz));
            if (m.cpu().illegal) std::printf("ILLEGAL opcode at %04X\n", m.cpu().illegalPc);
        } else if (c == "key") {
            std::string k = next();
            auto it = kKeys.find(k);
            if (it == kKeys.end()) { std::printf("unknown key %s\n", k.c_str()); continue; }
            m.setKey(it->second, true);
            m.run(int64_t(0.1 * Mirage::kCpuHz));
            m.setKey(it->second, false);
            m.run(int64_t(0.1 * Mirage::kCpuHz));
        } else if (c == "midi") {
            std::string hex = next();
            for (auto& ch : hex) if (ch == ',') ch = ' ';
            std::istringstream ss(hex);
            std::string b;
            while (ss >> b) m.midiIn(uint8_t(std::strtoul(b.c_str(), nullptr, 16)));
        } else if (c == "bp") {
            unsigned addr = unsigned(std::strtoul(next().c_str(), nullptr, 16));
            double sec = std::atof(next().c_str());
            const uint64_t end = m.cycles() + uint64_t(sec * Mirage::kCpuHz);
            int hits = 0;
            while (m.cycles() < end && hits < 40) {
                m.run(1);
                if (m.cpu().pc == addr) {
                    auto& p = m.cpu(); hits++;
                    std::printf("bp %04X t=%.4f A=%02X B=%02X X=%04X Y=%04X U=%04X S=%04X\n", addr,
                                m.cycles() / Mirage::kCpuHz, p.a, p.b, p.x, p.y, p.u, p.s);
                }
            }
        } else if (c == "doc") {
            auto& d = m.doc();
            std::printf("oscs enabled %d (frame %.2f kHz)\n", d.oscillatorsEnabled(), 8000.0 / d.frameClocks());
            for (int o = 0; o < 32; ++o) {
                uint8_t ctl = d.read(0xA0 + o);
                if (ctl & 1) continue;
                std::printf("osc%02d ctl=%02X freq=%02X%02X vol=%02X ptr=%02X sz=%02X\n", o, ctl, d.read(0x20 + o),
                            d.read(o), d.read(0x40 + o), d.read(0x80 + o), d.read(0xC0 + o));
            }
        } else if (c == "levels") {
            for (int d = 0; d < 2; ++d) {
                std::printf("digit %d:", d);
                for (int k = 0; k < 8; ++k) std::printf(" %c=%.2f", "abcdefgP"[k], m.segmentLevel(d, k));
                std::printf("\n");
            }
        } else if (c == "fdc") {
            std::printf("motor=%d disk=%d\n", m.fdc().motorOn(), m.fdc().hasDisk());
        } else if (c == "show") {
            show(m);
        } else if (c == "regs") {
            auto& p = m.cpu();
            std::printf("PC=%04X A=%02X B=%02X X=%04X Y=%04X U=%04X S=%04X DP=%02X CC=%02X\n",
                        p.pc, p.a, p.b, p.x, p.y, p.u, p.s, p.dp, p.cc);
        } else if (c == "peek") {
            unsigned a = unsigned(std::strtoul(next().c_str(), nullptr, 16));
            int n = std::atoi(next().c_str());
            for (int k = 0; k < n; ++k) std::printf("%02X%s", m.read(uint16_t(a + k)), (k % 16 == 15) ? "\n" : " ");
            std::printf("\n");
        } else if (c == "filters") {
            for (int k = 0; k < 8; ++k)
                std::printf("vcf%d cut=%02X (%.0f Hz) res=%02X\n", k, m.filter(k).cutoffCode(),
                            m.filter(k).cutoffHz(), m.filter(k).resonanceCode());
        } else if (c == "wav") {
            std::string p = next();
            double sec = std::atof(next().c_str());
            const int n = int(sec * Mirage::kAnalogHz);
            std::vector<float> buf(static_cast<size_t>(n));
            m.render(buf.data(), n);
            // crude decimation: 2-pole lowpass + linear interpolation to 44.1k
            std::vector<float> out;
            double lp1 = 0, lp2 = 0;
            const double a = 1.0 - std::exp(-2 * M_PI * 15000.0 / Mirage::kAnalogHz);
            std::vector<float> f(buf.size());
            for (size_t k = 0; k < buf.size(); ++k) { lp1 += a * (buf[k] - lp1); lp2 += a * (lp1 - lp2); f[k] = float(lp2); }
            for (double t = 0; t < f.size() - 1; t += Mirage::kAnalogHz / 44100.0) {
                size_t k = size_t(t); double fr = t - k;
                out.push_back(float((f[k] * (1 - fr) + f[k + 1] * fr) * 0.25));
            }
            writeWav(p, out, 44100);
            float peak = 0; for (float v : out) peak = std::fmax(peak, std::fabs(v));
            std::printf("wrote %s (%zu samples, peak %.3f)\n", p.c_str(), out.size(), peak);
        } else {
            std::printf("unknown command %s\n", c.c_str());
        }
    }
    return 0;
}
