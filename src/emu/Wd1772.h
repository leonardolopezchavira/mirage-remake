// WD1772 floppy controller with a Mirage-format disk (80 tracks, single sided,
// sectors 0-4 of 1024 bytes + sector 5 of 512 bytes = 5632 bytes per track). MIT licence.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace mirage {

class Wd1772 {
public:
    static constexpr int kTracks = 80;
    static constexpr int kTrackBytes = 5 * 1024 + 512;
    static constexpr int kImageBytes = kTracks * kTrackBytes; // 450560

    void reset();
    uint8_t read(int reg);
    void write(int reg, uint8_t v);
    void tick(int cycles);

    bool intrq() const { return intrq_; }
    bool drq() const { return drq_; }

    bool insert(const std::vector<uint8_t>& image);  // false if the size is wrong
    void eject() { disk_.clear(); }
    bool hasDisk() const { return !disk_.empty(); }
    const std::vector<uint8_t>& image() const { return disk_; }
    bool dirty() const { return dirty_; }
    void clearDirty() { dirty_ = false; }
    void setMotor(bool on) { motor_ = on; }
    bool motorOn() const { return motor_; }
    bool writeProtect = false;

    // Disk transfer speed: cycles between a byte being consumed and the next DRQ.
    int byteCycles = 32;

private:
    enum class Op { None, Seek, Read, Write, ReadAddr, ReadTrack, WriteTrack };
    std::vector<uint8_t> disk_;
    uint8_t status_ = 0, track_ = 0, sector_ = 0, data_ = 0, cmd_ = 0;
    int head_ = 0;
    bool intrq_ = false, drq_ = false, motor_ = false, dirty_ = false;
    Op op_ = Op::None;
    int delay_ = 0;          // cycles until the next state change
    int pos_ = 0, len_ = 0;  // transfer position / length
    long trackOff_ = 0;      // byte offset of the current transfer in the image
    uint8_t buf_[8192];
    bool typeI_ = true;

    static int sectorOffset(int s) { return s < 5 ? s * 1024 : 5 * 1024; }
    static int sectorLen(int s) { return s < 5 ? 1024 : 512; }
    void finish(uint8_t extraStatus);
    uint8_t typeIStatus() const;
};

} // namespace mirage
