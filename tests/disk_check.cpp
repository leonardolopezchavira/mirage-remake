// Decodes disk files and compares them with a raw image of the same disk:
//   mirage_disk_check file.hfe reference.img
#include "engine/DiskImage.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
int main(int argc, char** argv)
{
    if (argc < 2) { std::fprintf(stderr, "usage: %s disk [reference.img]\n", argv[0]); return 2; }
    auto rd = [](const char* p) { std::ifstream f(p, std::ios::binary); return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {}); };
    std::vector<uint8_t> img; std::string err;
    if (!mirage::decodeDiskImage(rd(argv[1]), img, err)) { std::printf("FAIL: %s\n", err.c_str()); return 1; }
    if (argc > 2) {
        const auto ref = rd(argv[2]);
        size_t diff = 0; for (size_t i = 0; i < img.size() && i < ref.size(); ++i) diff += img[i] != ref[i];
        std::printf("%s: %zu bytes differ\n", diff ? "FAIL" : "OK", diff);
        return diff ? 1 : 0;
    }
    std::printf("OK\n");
    return 0;
}
