// DPF plug-in around MirageEngine. The editor reaches the instance directly
// (DISTRHO_PLUGIN_WANT_DIRECT_ACCESS).
#pragma once
#include "DistrhoPlugin.hpp"
#include "../engine/MirageEngine.h"
#include <memory>
#include <string>
#include <vector>

START_NAMESPACE_DISTRHO

enum Params { kParamVolume = 0, kParamFastDisk, kParamCount };

class MiragePlugin : public Plugin {
public:
    MiragePlugin();

    mirage::MirageEngine& engine() { return *engine_; }

    // Bundled disk images (Contents/Resources/Disks on macOS).
    struct DiskEntry { std::string name, label, path; };
    const std::vector<DiskEntry>& disks() const { return disks_; }
    bool loadDiskFile(const std::string& path, std::string* error = nullptr);
    std::string userDiskDir() const;
    // Writes a disk the Mirage saved to back to a file (a copy in the user's disk folder
    // when it came from the read-only bundle). Returns the path written, or "".
    std::string flushWrittenDisk();
    ~MiragePlugin() override { flushWrittenDisk(); }

protected:
    const char* getLabel() const override { return "Mirage"; }
    const char* getDescription() const override { return "Ensoniq Mirage sampler (hardware emulation)"; }
    const char* getMaker() const override { return "mirage-remake"; }
    const char* getHomePage() const override { return "https://github.com/leonardolopezchavira/mirage-remake"; }
    const char* getLicense() const override { return "MIT (code); Ensoniq ROM/OS/sounds: see LICENSE.md"; }
    uint32_t getVersion() const override { return d_version(0, 1, 0); }

    void initParameter(uint32_t index, Parameter& p) override;
    float getParameterValue(uint32_t index) const override;
    void setParameterValue(uint32_t index, float v) override;
    void initState(uint32_t index, State& s) override;
    String getState(const char* key) const override;
    void setState(const char* key, const char* value) override;

    void sampleRateChanged(double sr) override { engine_->setSampleRate(sr); }
    void run(const float**, float** outputs, uint32_t frames, const MidiEvent* ev, uint32_t evCount) override;

private:
    std::unique_ptr<mirage::MirageEngine> engine_;
    std::vector<DiskEntry> disks_;
    std::vector<float> mono_;
    void scanDisks();
};

END_NAMESPACE_DISTRHO
