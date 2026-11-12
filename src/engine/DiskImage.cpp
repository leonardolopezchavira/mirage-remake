#include "DiskImage.h"
#include "../emu/Wd1772.h"
#include <cstring>

namespace mirage {

namespace {

uint16_t le16(const std::vector<uint8_t>& d, size_t o) { return uint16_t(d[o] | (d[o + 1] << 8)); }

// HFE v1: header "HXCPICFE"; track table at block[18..19]*512; each track is stored in
// 512-byte blocks whose first 256 bytes belong to side 0. Bits are stored LSB first.
bool decodeHfe(const std::vector<uint8_t>& d, std::vector<uint8_t>& img, std::string& err)
{
    if (d.size() < 512) { err = "Truncated HFE file"; return false; }
    const int tracks = d[9];
    const size_t lut = size_t(le16(d, 18)) * 512;
    img.assign(size_t(Wd1772::kImageBytes), 0);
    int found = 0;

    for (int t = 0; t < tracks && t < Wd1772::kTracks; ++t) {
        if (lut + 4 * size_t(t) + 4 > d.size()) break;
        const size_t off = size_t(le16(d, lut + 4 * size_t(t))) * 512;
        const size_t len = le16(d, lut + 4 * size_t(t) + 2);
        // side-0 bit stream
        std::vector<uint8_t> bits;
        bits.reserve(len * 4);
        for (size_t b = 0; b < len; b += 512) {
            const size_t n = (len - b) < 512 ? (len - b) / 2 : 256;
            for (size_t k = 0; k < n && off + b + k < d.size(); ++k) {
                const uint8_t v = d[off + b + k];
                for (int i = 0; i < 8; ++i) bits.push_back((v >> i) & 1);
            }
        }
        const size_t nb = bits.size();
        auto isSync = [&](size_t p) { // MFM 0x4489 (A1 with missing clock)
            static const uint8_t s[16] = {0,1,0,0,0,1,0,0,1,0,0,0,1,0,0,1};
            if (p + 16 > nb) return false;
            for (int i = 0; i < 16; ++i) if (bits[p + size_t(i)] != s[i]) return false;
            return true;
        };
        auto byteAt = [&](size_t p) {
            uint8_t v = 0;
            for (int k = 0; k < 8; ++k) v = uint8_t((v << 1) | bits[p + size_t(2 * k + 1)]);
            return v;
        };
        int pTrack = -1, pSector = -1, pSize = 0;
        size_t i = 0;
        while (i + 16 < nb) {
            if (!isSync(i)) { ++i; continue; }
            size_t j = i;
            while (isSync(j)) j += 16;
            if (j + 16 * 8 > nb) break;
            const uint8_t mark = byteAt(j);
            if (mark == 0xFE) {
                pTrack = byteAt(j + 16);
                pSector = byteAt(j + 48);
                pSize = 128 << (byteAt(j + 64) & 3);
                i = j + 16 * 5;
                continue;
            }
            if (mark == 0xFB && pSector >= 0) {
                if (j + 16 * size_t(1 + pSize) <= nb && pTrack < Wd1772::kTracks && pSector <= 5) {
                    const size_t base = size_t(pTrack) * Wd1772::kTrackBytes + (pSector < 5 ? size_t(pSector) * 1024 : 5120);
                    const int n = std::min(pSize, pSector < 5 ? 1024 : 512);
                    for (int k = 0; k < n; ++k) img[base + size_t(k)] = byteAt(j + 16 * size_t(1 + k));
                    ++found;
                }
                i = j + 16 * size_t(1 + pSize);
                pSector = -1;
                continue;
            }
            i = j;
        }
    }
    if (found < 6) { err = "HFE file contains no Mirage sectors"; return false; }
    return true;
}

} // namespace

bool decodeDiskImage(const std::vector<uint8_t>& file, std::vector<uint8_t>& image, std::string& error)
{
    if (file.size() == size_t(Wd1772::kImageBytes)) { image = file; return true; }
    if (file.size() >= 8 && std::memcmp(file.data(), "HXCPICFE", 8) == 0) return decodeHfe(file, image, error);
    if (file.size() >= 8 && std::memcmp(file.data(), "HXCHFEV3", 8) == 0) {
        error = "HFE v3 files are not supported (save as HFE v1)";
        return false;
    }
    if (file.size() > 0xA0 && std::memcmp(file.data() + 2, "Mirage", 6) == 0) {
        error = "EDM files are not supported (use the .img version)";
        return false;
    }
    error = "Not a Mirage disk image (.img of 450560 bytes or .hfe)";
    return false;
}

} // namespace mirage
