// Boot ROM and default boot disk compiled into the binary (generated: cmake/embed.cmake).
#pragma once
#include <cstddef>
#include <cstdint>
namespace embedded {
extern const uint8_t kRom[];
extern const size_t kRomSize;
extern const uint8_t kBootDisk[];
extern const size_t kBootDiskSize;
extern const char* const kBootDiskName;
extern const uint8_t kWordmarkPng[];   // resources/ui/wordmark.png (tools/make_wordmark.py)
extern const size_t kWordmarkPngSize;
}
