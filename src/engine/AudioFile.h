// Reads WAV (PCM 8/16/24/32-bit, float 32/64, WAVE_FORMAT_EXTENSIBLE) and AIFF / AIFF-C
// (PCM 8/16/24/32-bit big-endian, 'sowt' little-endian, 'fl32'/'fl64') into mono floats.
// MIT licence.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace mirage {

struct AudioData {
    std::vector<float> mono;   // channels averaged, -1..1
    double sampleRate = 0;
};

bool readAudioFile(const std::vector<uint8_t>& file, AudioData& out, std::string& error);

// Linear-interpolation resampling (enough here: the Mirage's input filter follows).
std::vector<float> resampleLinear(const std::vector<float>& in, double fromRate, double toRate);

} // namespace mirage
