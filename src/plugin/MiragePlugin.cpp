#include "MiragePlugin.hpp"
#include "DistrhoPluginUtils.hpp"
#include "EmbeddedData.h"
#include "../engine/DiskImage.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <dirent.h>

START_NAMESPACE_DISTRHO

namespace {

std::string dirName(const std::string& p)
{
    const size_t s = p.find_last_of('/');
    return s == std::string::npos ? std::string(".") : p.substr(0, s);
}

std::string baseName(const std::string& p)
{
    const size_t s = p.find_last_of('/');
    std::string b = s == std::string::npos ? p : p.substr(s + 1);
    const size_t dot = b.find_last_of('.');
    return dot == std::string::npos ? b : b.substr(0, dot);
}

bool readFile(const std::string& p, std::vector<uint8_t>& out)
{
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return true;
}

bool isDiskFile(const std::string& n)
{
    if (n.size() < 5 || n[0] == '.') return false;
    std::string ext = n.substr(n.size() - 4);
    for (auto& c : ext) c = char(std::tolower((unsigned char)c));
    return ext == ".img" || ext == ".hfe";
}

bool isDir(const std::string& p)
{
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// Natural order: A2 before A10.
bool naturalLess(const std::string& a, const std::string& b)
{
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (std::isdigit((unsigned char)a[i]) && std::isdigit((unsigned char)b[j])) {
            long x = std::strtol(a.c_str() + i, nullptr, 10), y = std::strtol(b.c_str() + j, nullptr, 10);
            if (x != y) return x < y;
            while (i < a.size() && std::isdigit((unsigned char)a[i])) ++i;
            while (j < b.size() && std::isdigit((unsigned char)b[j])) ++j;
        } else {
            if (a[i] != b[j]) return a[i] < b[j];
            ++i; ++j;
        }
    }
    return a.size() < b.size();
}

} // namespace

MiragePlugin::MiragePlugin() : Plugin(kParamCount, 0, 1)
{
    std::vector<uint8_t> rom(embedded::kRom, embedded::kRom + embedded::kRomSize);
    std::vector<uint8_t> boot(embedded::kBootDisk, embedded::kBootDisk + embedded::kBootDiskSize);
    engine_ = std::make_unique<mirage::MirageEngine>(rom, boot, embedded::kBootDiskName);
    engine_->setSampleRate(getSampleRate());
    scanDisks();
}

std::string MiragePlugin::userDiskDir() const
{
    const char* home = std::getenv("HOME");
    std::string base = home ? home : ".";
#ifdef __APPLE__
    base += "/Library/Application Support/Mirage";
#else
    base += "/.local/share/Mirage";
#endif
    mkdir(base.c_str(), 0755);
    base += "/Disks";
    mkdir(base.c_str(), 0755);
    return base;
}

void MiragePlugin::scanDisks()
{
    // Bundle resources: <bundle>/Contents/Resources/Disks (macOS app/component/vst3), or a
    // Disks folder next to the binary (development builds); MIRAGE_DISKS overrides.
    std::vector<std::string> roots;
    if (const char* env = std::getenv("MIRAGE_DISKS")) roots.push_back(env);
    if (const char* bin = getBinaryFilename()) {
        const std::string d = dirName(bin);
        roots.push_back(d + "/../Resources/Disks");
        roots.push_back(d + "/Disks");
        roots.push_back(d + "/../../Resources/Disks");
    }
    for (const auto& root : roots) {
        if (!isDir(root)) continue;
        templatePath_ = root + "/Template/Blank.img";
        for (const char* sub : {"Factory", "OS"}) {
            const std::string dir = root + "/" + sub;
            std::map<std::string, std::string> labels;
            std::ifstream lf(dir + "/labels.txt");
            for (std::string line; std::getline(lf, line);) {
                const size_t t = line.find('\t');
                if (t != std::string::npos) labels[line.substr(0, t)] = line.substr(t + 1);
            }
            std::vector<DiskEntry> found;
            if (DIR* dp = opendir(dir.c_str())) {
                while (dirent* e = readdir(dp)) {
                    std::string n = e->d_name;
                    if (!isDiskFile(n)) continue;
                    DiskEntry de;
                    de.name = baseName(n);
                    de.path = dir + "/" + n;
                    de.label = labels.count(de.name) ? labels[de.name] : (std::string(sub) == "OS" ? "System disk" : "");
                    found.push_back(de);
                }
                closedir(dp);
            }
            std::sort(found.begin(), found.end(), [](const DiskEntry& a, const DiskEntry& b) { return naturalLess(a.name, b.name); });
            disks_.insert(disks_.end(), found.begin(), found.end());
        }
        break;
    }
    // The user's own disks (saved copies, formatted disks).
    const std::string user = userDiskDir();
    std::vector<DiskEntry> found;
    if (DIR* dp = opendir(user.c_str())) {
        while (dirent* e = readdir(dp)) {
            std::string n = e->d_name;
            if (!isDiskFile(n)) continue;
            found.push_back({baseName(n), "Your disk", user + "/" + n});
        }
        closedir(dp);
    }
    std::sort(found.begin(), found.end(), [](const DiskEntry& a, const DiskEntry& b) { return naturalLess(a.name, b.name); });
    disks_.insert(disks_.end(), found.begin(), found.end());
}

bool MiragePlugin::loadDiskFile(const std::string& path, std::string* error)
{
    std::vector<uint8_t> file, img;
    std::string err;
    if (!readFile(path, file)) err = "Could not read " + baseName(path);
    else if (mirage::decodeDiskImage(file, img, err)) {
        engine_->insertDisk(std::move(img), baseName(path), path);
        return true;
    }
    if (error) *error = err;
    return false;
}

void MiragePlugin::savePreference(const std::string& key, float value)
{
    const std::string path = dirName(userDiskDir()) + "/settings.txt";
    std::map<std::string, std::string> kv;
    {
        std::ifstream f(path);
        for (std::string line; std::getline(f, line);) {
            const size_t eq = line.find('=');
            if (eq != std::string::npos) kv[line.substr(0, eq)] = line.substr(eq + 1);
        }
    }
    kv[key] = std::to_string(value);
    std::ofstream f(path, std::ios::trunc);
    for (const auto& [k, v] : kv) f << k << '=' << v << '\n';
}

float MiragePlugin::preference(const std::string& key, float fallback) const
{
    std::ifstream f(dirName(userDiskDir()) + "/settings.txt");
    for (std::string line; std::getline(f, line);) {
        const size_t eq = line.find('=');
        if (eq != std::string::npos && line.substr(0, eq) == key) return std::strtof(line.c_str() + eq + 1, nullptr);
    }
    return fallback;
}

int MiragePlugin::newDisk(std::string* error)
{
    std::vector<uint8_t> img;
    if (templatePath_.empty() || !readFile(templatePath_, img) || img.size() != size_t(mirage::Wd1772::kImageBytes)) {
        // no bundled template: the built-in OS 3.2 disk is a bootable, formatted disk too
        img.assign(embedded::kBootDisk, embedded::kBootDisk + embedded::kBootDiskSize);
    }
    const std::string dir = userDiskDir();
    std::string name, path;
    for (int n = 1; n < 1000; ++n) {
        name = "My Disk " + std::to_string(n);
        path = dir + "/" + name + ".img";
        struct stat st;
        if (stat(path.c_str(), &st) != 0) break;
    }
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f || !f.write(reinterpret_cast<const char*>(img.data()), std::streamsize(img.size()))) {
        if (error) *error = "Could not create " + path;
        return -1;
    }
    f.close();
    engine_->insertDisk(img, name, path);
    disks_.push_back({name, "Your disk (new)", path});
    return int(disks_.size()) - 1;
}

std::string MiragePlugin::flushWrittenDisk()
{
    std::vector<uint8_t> img;
    std::string path;
    if (!engine_->takeWrittenDisk(img, path)) return "";
    const std::string user = userDiskDir();
    std::string target = path;
    // Disks from the user's folder are written back in place (.img only); anything else
    // (bundled disks, other folders, .hfe files) is saved as an .img copy in that folder.
    const bool isImg = target.size() > 4 && (target.substr(target.size() - 4) == ".img");
    if (target.empty() || !isImg || target.compare(0, user.size(), user) != 0) {
        const std::string name = target.empty() ? engine_->diskName() : baseName(target);
        target = user + "/" + name + ".img";
    }
    std::ofstream f(target, std::ios::binary | std::ios::trunc);
    if (!f) return "";
    f.write(reinterpret_cast<const char*>(img.data()), std::streamsize(img.size()));
    engine_->setDiskPath(target);
    return target;
}

void MiragePlugin::initParameter(uint32_t index, Parameter& p)
{
    switch (index) {
    case kParamVolume:
        p.hints = kParameterIsAutomatable;
        p.name = "Volume";
        p.symbol = "volume";
        p.ranges.def = 0.8f; p.ranges.min = 0.0f; p.ranges.max = 1.0f;
        break;
    case kParamFactoryTuning:
        p.hints = kParameterIsAutomatable | kParameterIsBoolean | kParameterIsInteger;
        p.name = "Factory tuning";
        p.symbol = "factorytuning";
        p.ranges.min = 0.0f; p.ranges.max = 1.0f;
        p.ranges.def = preference("factory_tuning", 1.0f) > 0.5f ? 1.0f : 0.0f;
        engine_->factoryTuning.store(p.ranges.def > 0.5f);
        break;
    case kParamFastDisk:
        p.hints = kParameterIsBoolean | kParameterIsInteger;
        p.name = "Fast disk";
        p.symbol = "fastdisk";
        p.ranges.def = 1.0f; p.ranges.min = 0.0f; p.ranges.max = 1.0f;
        break;
    }
}

float MiragePlugin::getParameterValue(uint32_t index) const
{
    switch (index) {
    case kParamVolume: return engine_->volume.load();
    case kParamFastDisk: return engine_->fastDisk.load() ? 1.0f : 0.0f;
    case kParamFactoryTuning: return engine_->factoryTuning.load() ? 1.0f : 0.0f;
    }
    return 0.0f;
}

void MiragePlugin::setParameterValue(uint32_t index, float v)
{
    switch (index) {
    case kParamVolume: engine_->volume.store(v); break;
    case kParamFastDisk: engine_->fastDisk.store(v > 0.5f); break;
    case kParamFactoryTuning: engine_->factoryTuning.store(v > 0.5f); break;
    }
}

void MiragePlugin::initState(uint32_t index, State& s)
{
    if (index == 0) {
        s.key = "disk";
        s.defaultValue = "";
        s.hints = kStateIsOnlyForDSP;
    }
}

String MiragePlugin::getState(const char* key) const
{
    if (std::strcmp(key, "disk") == 0) return String(engine_->diskPath().c_str());
    return String();
}

void MiragePlugin::setState(const char* key, const char* value)
{
    if (std::strcmp(key, "disk") == 0 && value && *value) {
        // Insert the disk the project used and load both sounds from it (LOAD UPPER is not
        // pressed here: the disk's own boot sounds come up when the OS reads it on demand).
        if (value != engine_->diskPath()) loadDiskFile(value);
    }
}

void MiragePlugin::run(const float** inputs, float** outputs, uint32_t frames, const MidiEvent* ev, uint32_t evCount)
{
    if (mono_.size() < frames) mono_.resize(frames);
    if (monoIn_.size() < frames) monoIn_.resize(frames);
    for (uint32_t i = 0; i < frames; ++i) monoIn_[i] = 0.5f * (inputs[0][i] + inputs[1][i]);
    uint32_t pos = 0, e = 0;
    while (pos < frames) {
        while (e < evCount && ev[e].frame <= pos) {
            const MidiEvent& m = ev[e++];
            const uint8_t* d = m.size > MidiEvent::kDataSize ? m.dataExt : m.data;
            for (uint32_t i = 0; i < m.size; ++i) engine_->midiByte(d[i]);
        }
        const uint32_t end = e < evCount ? std::min(frames, ev[e].frame) : frames;
        if (end > pos) engine_->process(mono_.data() + pos, int(end - pos), monoIn_.data() + pos);
        pos = end;
    }
    while (e < evCount) {
        const MidiEvent& m = ev[e++];
        const uint8_t* d = m.size > MidiEvent::kDataSize ? m.dataExt : m.data;
        for (uint32_t i = 0; i < m.size; ++i) engine_->midiByte(d[i]);
    }
    std::copy(mono_.begin(), mono_.begin() + frames, outputs[0]);
    std::copy(mono_.begin(), mono_.begin() + frames, outputs[1]);
}

Plugin* createPlugin() { return new MiragePlugin(); }

END_NAMESPACE_DISTRHO
