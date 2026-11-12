// Mirage disk image formats: raw .img (450560 bytes) and HxC .hfe (v1, MFM), which is
// decoded sector by sector into the raw layout. MIT licence.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace mirage {

// Converts a disk file's bytes into a raw Mirage image (80 tracks x 5632 bytes).
// Returns false with a reason in `error` if the format isn't recognised.
bool decodeDiskImage(const std::vector<uint8_t>& file, std::vector<uint8_t>& image, std::string& error);

} // namespace mirage
