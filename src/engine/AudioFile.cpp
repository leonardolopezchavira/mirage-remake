#include "AudioFile.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace mirage {

namespace {

uint32_t le32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24; }
uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
uint16_t be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }

// 80-bit IEEE extended (AIFF sample rate)
double extended(const uint8_t* p)
{
    const int exp = ((p[0] & 0x7F) << 8 | p[1]) - 16383;
    uint64_t mant = 0;
    for (int i = 0; i < 8; ++i) mant = mant << 8 | p[2 + i];
    const double v = double(mant) * std::pow(2.0, exp - 63);
    return (p[0] & 0x80) ? -v : v;
}

enum class Fmt { Int, Float };

// Decodes interleaved frames and averages the channels.
bool decode(const uint8_t* d, size_t bytes, int channels, int bits, Fmt fmt, bool bigEndian, bool unsigned8,
            std::vector<float>& out, std::string& error)
{
    if (channels < 1 || channels > 64) { error = "unsupported number of channels"; return false; }
    const int bps = (bits + 7) / 8;
    if (!(fmt == Fmt::Int && bps >= 1 && bps <= 4) && !(fmt == Fmt::Float && (bits == 32 || bits == 64))) {
        error = "unsupported sample format (" + std::to_string(bits) + "-bit)";
        return false;
    }
    const size_t frame = size_t(bps) * size_t(channels);
    const size_t frames = bytes / frame;
    out.resize(frames);
    for (size_t f = 0; f < frames; ++f) {
        double sum = 0;
        for (int c = 0; c < channels; ++c) {
            const uint8_t* p = d + f * frame + size_t(c) * size_t(bps);
            double v = 0;
            if (fmt == Fmt::Float) {
                uint8_t b[8];
                for (int i = 0; i < bps; ++i) b[i] = bigEndian ? p[bps - 1 - i] : p[i];
                if (bits == 32) { float x; std::memcpy(&x, b, 4); v = x; }
                else { double x; std::memcpy(&x, b, 8); v = x; }
            } else if (bps == 1) {
                v = unsigned8 ? (int(p[0]) - 128) / 128.0 : int8_t(p[0]) / 128.0;
            } else {
                int64_t x = 0;
                for (int i = 0; i < bps; ++i) x |= int64_t(bigEndian ? p[i] : p[bps - 1 - i]) << (8 * (bps - 1 - i));
                const int shift = 64 - 8 * bps;
                x = int64_t(uint64_t(x) << shift) >> shift;   // sign-extend
                v = double(x) / double(int64_t(1) << (8 * bps - 1));
            }
            sum += v;
        }
        out[f] = float(sum / channels);
    }
    return true;
}

bool readWav(const std::vector<uint8_t>& f, AudioData& out, std::string& error)
{
    int channels = 0, bits = 0, format = 0;
    double rate = 0;
    const uint8_t* data = nullptr;
    size_t dataBytes = 0;
    for (size_t pos = 12; pos + 8 <= f.size();) {
        const uint8_t* ck = f.data() + pos;
        const size_t size = le32(ck + 4);
        const size_t avail = std::min(size, f.size() - pos - 8);
        if (!std::memcmp(ck, "fmt ", 4) && avail >= 16) {
            format = le16(ck + 8);
            channels = le16(ck + 10);
            rate = le32(ck + 12);
            bits = le16(ck + 22);
            if (format == 0xFFFE && avail >= 40) format = le16(ck + 32);   // extensible: sub-format GUID
        } else if (!std::memcmp(ck, "data", 4)) {
            data = ck + 8;
            dataBytes = avail;
        }
        pos += 8 + size + (size & 1);
    }
    if (!data || !channels) { error = "not a readable WAV file"; return false; }
    if (format != 1 && format != 3) { error = "compressed WAV files are not supported"; return false; }
    out.sampleRate = rate;
    return decode(data, dataBytes, channels, bits, format == 3 ? Fmt::Float : Fmt::Int, false, true, out.mono, error);
}

bool readAiff(const std::vector<uint8_t>& f, AudioData& out, std::string& error)
{
    const bool aifc = !std::memcmp(f.data() + 8, "AIFC", 4);
    int channels = 0, bits = 0;
    double rate = 0;
    bool little = false;
    Fmt fmt = Fmt::Int;
    const uint8_t* data = nullptr;
    size_t dataBytes = 0;
    for (size_t pos = 12; pos + 8 <= f.size();) {
        const uint8_t* ck = f.data() + pos;
        const size_t size = be32(ck + 4);
        const size_t avail = std::min(size, f.size() - pos - 8);
        if (!std::memcmp(ck, "COMM", 4) && avail >= 18) {
            channels = be16(ck + 8);
            bits = be16(ck + 14);
            rate = extended(ck + 16);
            if (aifc && avail >= 22) {
                const uint8_t* c = ck + 26;
                if (!std::memcmp(c, "sowt", 4)) little = true;
                else if (!std::memcmp(c, "fl32", 4) || !std::memcmp(c, "FL32", 4)) { fmt = Fmt::Float; bits = 32; }
                else if (!std::memcmp(c, "fl64", 4) || !std::memcmp(c, "FL64", 4)) { fmt = Fmt::Float; bits = 64; }
                else if (std::memcmp(c, "NONE", 4) && std::memcmp(c, "twos", 4)) {
                    error = "compressed AIFF files are not supported";
                    return false;
                }
            }
        } else if (!std::memcmp(ck, "SSND", 4) && avail >= 8) {
            const size_t offset = be32(ck + 8);
            data = ck + 16 + offset;
            dataBytes = avail >= 8 + offset ? avail - 8 - offset : 0;
        }
        pos += 8 + size + (size & 1);
    }
    if (!data || !channels) { error = "not a readable AIFF file"; return false; }
    out.sampleRate = rate;
    return decode(data, dataBytes, channels, bits, fmt, !little, false, out.mono, error);
}

} // namespace

bool readAudioFile(const std::vector<uint8_t>& file, AudioData& out, std::string& error)
{
    out = AudioData();
    if (file.size() < 12) { error = "file too short"; return false; }
    bool ok;
    if (!std::memcmp(file.data(), "RIFF", 4) && !std::memcmp(file.data() + 8, "WAVE", 4))
        ok = readWav(file, out, error);
    else if (!std::memcmp(file.data(), "FORM", 4) &&
             (!std::memcmp(file.data() + 8, "AIFF", 4) || !std::memcmp(file.data() + 8, "AIFC", 4)))
        ok = readAiff(file, out, error);
    else {
        error = "only WAV and AIFF files can be imported";
        return false;
    }
    if (ok && (out.sampleRate < 1000 || out.sampleRate > 768000)) { error = "unusual sample rate"; return false; }
    if (ok && out.mono.empty()) { error = "the file contains no audio"; return false; }
    return ok;
}

std::vector<float> resampleLinear(const std::vector<float>& in, double fromRate, double toRate)
{
    if (in.empty() || fromRate <= 0 || toRate <= 0) return {};
    const double step = fromRate / toRate;
    const size_t n = size_t(double(in.size() - 1) / step) + 1;
    std::vector<float> out(n);
    for (size_t i = 0; i < n; ++i) {
        const double p = double(i) * step;
        const size_t k = size_t(p);
        const double t = p - double(k);
        out[i] = k + 1 < in.size() ? float(in[k] * (1 - t) + in[k + 1] * t) : in.back();
    }
    return out;
}

} // namespace mirage
