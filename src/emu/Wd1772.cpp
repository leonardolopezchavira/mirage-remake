#include "Wd1772.h"
#include <algorithm>
#include <climits>
#include <cstring>

namespace mirage {

namespace {
constexpr uint8_t ST_BUSY = 0x01, ST_DRQ = 0x02, ST_TR00 = 0x04, ST_LOST = 0x04,
                  ST_RNF = 0x10, ST_SPINUP = 0x20, ST_WP = 0x40, ST_MOTOR = 0x80;
constexpr int kWait = INT_MAX / 2;
}

void Wd1772::reset()
{
    status_ = 0; track_ = 0; sector_ = 0; data_ = 0; cmd_ = 0;
    intrq_ = drq_ = false;
    op_ = Op::None;
    typeI_ = true;
}

bool Wd1772::insert(const std::vector<uint8_t>& image)
{
    if (image.size() != size_t(kImageBytes)) return false;
    disk_ = image;
    dirty_ = false;
    return true;
}

uint8_t Wd1772::typeIStatus() const
{
    uint8_t s = ST_SPINUP;
    if (motor_) s |= ST_MOTOR;
    if (head_ == 0) s |= ST_TR00;
    if (writeProtect && hasDisk()) s |= ST_WP;
    return s;
}

void Wd1772::finish(uint8_t extra)
{
    op_ = Op::None;
    drq_ = false;
    status_ = uint8_t((typeI_ ? typeIStatus() : (motor_ ? ST_MOTOR : 0)) | extra);
    intrq_ = true;
}

uint8_t Wd1772::read(int reg)
{
    switch (reg & 3) {
    case 0: {
        intrq_ = false;
        uint8_t s = status_;
        if (op_ == Op::None && typeI_) s = uint8_t(typeIStatus() | (status_ & 0x18));
        if (drq_) s |= ST_DRQ;
        if (op_ != Op::None) s |= ST_BUSY;
        return s;
    }
    case 1: return track_;
    case 2: return sector_;
    default:
        if (drq_ && (op_ == Op::Read || op_ == Op::ReadAddr || op_ == Op::ReadTrack)) {
            drq_ = false;
            delay_ = pos_ >= len_ ? 40 : byteCycles;
        }
        return data_;
    }
}

void Wd1772::write(int reg, uint8_t v)
{
    switch (reg & 3) {
    case 1: track_ = v; return;
    case 2: sector_ = v; return;
    case 3:
        data_ = v;
        if (drq_ && (op_ == Op::Write || op_ == Op::WriteTrack)) {
            drq_ = false;
            buf_[pos_++] = v;
            delay_ = pos_ >= len_ ? 40 : byteCycles;
        }
        return;
    default: break;
    }

    // command
    intrq_ = false;
    if ((v & 0xF0) == 0xD0) { // force interrupt
        op_ = Op::None;
        drq_ = false;
        typeI_ = true;
        status_ = typeIStatus();
        if (v & 0x08) intrq_ = true;
        return;
    }
    if (op_ != Op::None) return;
    cmd_ = v;
    drq_ = false;

    if (!(v & 0x80)) { // ---- type I
        typeI_ = true;
        int steps = 0;
        const int hi = v >> 4;
        if (hi == 0x0) { steps = head_; head_ = 0; track_ = 0; }
        else if (hi == 0x1) {
            int diff = int(data_) - int(track_);
            head_ = std::clamp(head_ + diff, 0, 83);
            steps = diff < 0 ? -diff : diff;
            track_ = data_;
        } else {
            int dir = 0;
            if (hi == 0x4 || hi == 0x5) dir = 1;
            else if (hi == 0x6 || hi == 0x7) dir = -1;
            else dir = 1; // STEP: assume last direction was inward
            head_ = std::clamp(head_ + dir, 0, 83);
            if (v & 0x10) track_ = uint8_t(track_ + dir);
            steps = 1;
        }
        op_ = Op::Seek;
        delay_ = 2000 + 200 * steps;
        status_ = typeIStatus() | ST_BUSY;
        return;
    }

    typeI_ = false;
    status_ = ST_BUSY | (motor_ ? ST_MOTOR : 0);
    const int hi = v >> 4;
    auto locate = [&]() -> bool {
        return hasDisk() && head_ < kTracks && sector_ <= 5 && track_ == head_;
    };

    if (hi == 0x8 || hi == 0x9) { // read sector
        if (!locate()) { op_ = Op::Seek; delay_ = 20000; typeI_ = false; status_ = ST_BUSY;
                         // completes with RNF
                         cmd_ = 0xFF; return; }
        trackOff_ = long(head_) * kTrackBytes + sectorOffset(sector_);
        len_ = sectorLen(sector_);
        std::memcpy(buf_, &disk_[size_t(trackOff_)], size_t(len_));
        pos_ = 0;
        op_ = Op::Read;
        delay_ = 1000;
    } else if (hi == 0xA || hi == 0xB) { // write sector
        if (writeProtect && hasDisk()) { finish(ST_WP); return; }
        if (!locate()) { op_ = Op::Seek; delay_ = 20000; status_ = ST_BUSY; cmd_ = 0xFF; return; }
        trackOff_ = long(head_) * kTrackBytes + sectorOffset(sector_);
        len_ = sectorLen(sector_);
        pos_ = 0;
        op_ = Op::Write;
        delay_ = 500;
    } else if (hi == 0xC) { // read address
        if (!hasDisk()) { op_ = Op::Seek; delay_ = 20000; cmd_ = 0xFF; return; }
        const int s = 0;
        buf_[0] = uint8_t(head_); buf_[1] = 0; buf_[2] = uint8_t(s); buf_[3] = 3; buf_[4] = 0; buf_[5] = 0;
        len_ = 6; pos_ = 0;
        op_ = Op::ReadAddr;
        delay_ = 500;
    } else if (hi == 0xE) { // read track (data only, simplified)
        if (!hasDisk() || head_ >= kTracks) { op_ = Op::Seek; delay_ = 20000; cmd_ = 0xFF; return; }
        len_ = kTrackBytes; pos_ = 0;
        std::memcpy(buf_, &disk_[size_t(head_) * kTrackBytes], size_t(len_));
        op_ = Op::ReadTrack;
        delay_ = 1000;
    } else if (hi == 0xF) { // write track (format)
        if (writeProtect && hasDisk()) { finish(ST_WP); return; }
        len_ = 6250; pos_ = 0;
        op_ = Op::WriteTrack;
        delay_ = 1000;
    }
}

void Wd1772::tick(int cycles)
{
    if (op_ == Op::None || drq_) return;
    delay_ -= cycles;
    if (delay_ > 0) return;

    switch (op_) {
    case Op::Seek:
        if (cmd_ == 0xFF) finish(ST_RNF);
        else finish(0);
        break;
    case Op::Read: case Op::ReadAddr: case Op::ReadTrack:
        if (pos_ >= len_) {
            if (op_ == Op::ReadAddr) sector_ = buf_[0];
            finish(0);
            break;
        }
        data_ = buf_[pos_++];
        drq_ = true;
        delay_ = kWait;
        break;
    case Op::Write:
        if (pos_ >= len_) {
            if (hasDisk()) {
                std::memcpy(&disk_[size_t(trackOff_)], buf_, size_t(len_));
                dirty_ = true;
            }
            finish(0);
            break;
        }
        drq_ = true;
        delay_ = kWait;
        break;
    case Op::WriteTrack:
        if (pos_ >= len_) {
            // Parse the formatting stream: ID mark (FE) followed by track/side/sector/size,
            // then data mark (FB) followed by the sector's bytes.
            if (hasDisk() && head_ < kTracks) {
                int sec = -1;
                for (int i = 0; i < len_; ++i) {
                    if (buf_[i] == 0xFE && i + 4 < len_) { sec = buf_[i + 3]; i += 4; continue; }
                    if (buf_[i] == 0xFB && sec >= 0 && sec <= 5) {
                        int n = sectorLen(sec);
                        if (i + 1 + n <= len_)
                            std::memcpy(&disk_[size_t(head_) * kTrackBytes + sectorOffset(sec)], &buf_[i + 1], size_t(n));
                        i += n;
                        sec = -1;
                    }
                }
                dirty_ = true;
            }
            finish(0);
            break;
        }
        drq_ = true;
        delay_ = kWait;
        break;
    default: break;
    }
}

} // namespace mirage
