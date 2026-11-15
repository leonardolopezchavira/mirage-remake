// Fork addition (mirage-remake), linked into the standalone app (Mirage.app) only.
//
// A virtual MIDI port pair named "Mirage Remake", so that other programs on this Mac (an
// editor such as BoxedEar Visage, a librarian, a DAW) can talk to the emulated Mirage as if
// it were connected with two MIDI cables:
//   - the source "Mirage Remake" carries the Mirage's MIDI OUT: every byte the OS sends
//     through its 6850 ACIA (SysEx dumps, parameter and status messages, MIDI echo);
//   - the destination "Mirage Remake" is fed into the Mirage's MIDI IN, alongside the
//     hardware inputs chosen in Audio/MIDI Settings.
// The standalone never opens its own source as an input (patches/dpf-virtual-midi.patch),
// so the Mirage is not looped into itself.

#import <Foundation/Foundation.h>
#include <CoreMIDI/CoreMIDI.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "StandaloneHook.h"
#include "engine/MirageEngine.h"

#pragma clang diagnostic ignored "-Wdeprecated-declarations"  // MIDIPacketList / MIDIReceived

namespace dpf = DISTRHO_NAMESPACE;

namespace {

NSString* const kSourceIdKey = @"VirtualMIDISourceID";
NSString* const kDestinationIdKey = @"VirtualMIDIDestinationID";

MIDIClientRef gClient = 0;
MIDIEndpointRef gSource = 0, gDestination = 0;
dispatch_source_t gTimer = nil;

// --- diagnostics ------------------------------------------------------------------------
// Byte counters (shown in Settings) and a log of every message in both directions,
// ~/Library/Logs/Mirage/MIDI.log (rewritten at each launch, long SysEx shortened).

std::atomic<uint64_t> gInBytes{0}, gOutBytes{0}, gInSysex{0}, gOutSysex{0};
dispatch_queue_t gLogQueue = nil;
FILE* gLog = nullptr;
size_t gLogSize = 0;
CFAbsoluteTime gLogStart = 0;

void openLog()
{
    NSString* dir = [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Logs/Mirage"];
    [NSFileManager.defaultManager createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
    gLog = std::fopen([dir stringByAppendingPathComponent:@"MIDI.log"].fileSystemRepresentation, "w");
    gLogQueue = dispatch_queue_create("mirage.midi-log", DISPATCH_QUEUE_SERIAL);
    gLogStart = CFAbsoluteTimeGetCurrent();
}

void logText(const std::string& line)
{
    if (!gLog || !gLogQueue) return;
    const double t = CFAbsoluteTimeGetCurrent() - gLogStart;
    std::string copy = line;
    dispatch_async(gLogQueue, ^{
        if (gLogSize > (4u << 20)) return;  // 4 MB is plenty
        gLogSize += size_t(std::fprintf(gLog, "%9.3f  %s\n", t, copy.c_str()));
        std::fflush(gLog);
    });
}

void logBytes(const char* what, const uint8_t* p, size_t n)
{
    std::string line = what;
    char hex[4];
    const size_t shown = n <= 48 ? n : 40;
    for (size_t i = 0; i < shown; ++i) { std::snprintf(hex, sizeof hex, " %02X", p[i]); line += hex; }
    if (shown < n) {
        line += " ...";
        for (size_t i = n - 4; i < n; ++i) { std::snprintf(hex, sizeof hex, " %02X", p[i]); line += hex; }
        line += " (" + std::to_string(n) + " bytes)";
    }
    logText(line);
}

// CoreMIDI wants each packet to hold complete messages. The ACIA hands over a raw byte
// stream, so this reassembles it: running status is expanded, and a SysEx message is sent
// once it is complete, in one packet, the way a hardware MIDI interface delivers it (only a
// message longer than a packet can hold, a full sample dump, goes out in pieces). Every
// message goes out in its own MIDIReceived call, so CoreMIDI never merges two of them.
class OutAssembler {
public:
    void byte(uint8_t b)
    {
        if (b >= 0xF8) { emit(&b, 1); return; }                         // real time
        if (b == 0xF0) { flushSysex(); inSysex_ = true; sysex_.assign(1, b); status_ = 0; return; }
        if (b == 0xF7) {
            if (inSysex_) { sysex_.push_back(b); flushSysex(); inSysex_ = false; }
            return;                                                     // stray EOX: drop
        }
        if (b & 0x80) {
            if (inSysex_) { flushSysex(); inSysex_ = false; }           // SysEx cut short
            status_ = b;
            have_ = 0;
            if (b < 0xF0) need_ = ((b & 0xE0) == 0xC0) ? 1 : 2;           // Cn, Dn take one byte
            else need_ = (b == 0xF2) ? 2 : (b == 0xF1 || b == 0xF3) ? 1 : 0;
            if (need_ == 0) { emit(&b, 1); status_ = 0; }
            return;
        }
        if (inSysex_) {
            sysex_.push_back(b);
            if (sysex_.size() >= kMaxPacket) flushSysex();              // very long dump
            return;
        }
        if (!status_) return;                                           // no status yet
        data_[have_++] = b;
        if (have_ == need_) {
            uint8_t msg[3] = {status_, data_[0], data_[1]};
            emit(msg, need_ + 1);
            have_ = 0;
            if (status_ >= 0xF0) status_ = 0;                            // no running status
        }
    }

private:
    static constexpr size_t kMaxPacket = 60000;  // MIDIPacket length is 16 bits
    uint8_t status_ = 0, data_[2] = {};
    int have_ = 0, need_ = 0;
    bool inSysex_ = false;
    std::vector<uint8_t> sysex_;
    std::vector<uint8_t> buf_ = std::vector<uint8_t>(kMaxPacket + 1024);

    void flushSysex()
    {
        if (!sysex_.empty()) {
            if (sysex_[0] == 0xF0) gOutSysex++;
            emit(sysex_.data(), sysex_.size());
        }
        sysex_.clear();
    }

    void emit(const uint8_t* p, size_t n)
    {
        gOutBytes += n;
        logBytes("out", p, n);
        if (!gSource) return;
        MIDIPacketList* list = reinterpret_cast<MIDIPacketList*>(buf_.data());
        MIDIPacket* cur = MIDIPacketListInit(list);
        cur = MIDIPacketListAdd(list, buf_.size(), cur, 0, ByteCount(n), p);
        if (!cur) { logText("out: packet too large, dropped"); return; }
        const OSStatus st = MIDIReceived(gSource, list);
        if (st != noErr) logText("out: MIDIReceived error " + std::to_string(int(st)));
    }
};

OutAssembler gOut;

void restoreUniqueId(MIDIEndpointRef endpoint, NSString* key)
{
    // Keep the same ID across launches, so programs that remember ports by ID find it again.
    NSUserDefaults* defaults = NSUserDefaults.standardUserDefaults;
    if ([defaults objectForKey:key] != nil) {
        const SInt32 saved = SInt32([defaults integerForKey:key]);
        if (MIDIObjectSetIntegerProperty(endpoint, kMIDIPropertyUniqueID, saved) == noErr) return;
    }
    SInt32 assigned = 0;
    if (MIDIObjectGetIntegerProperty(endpoint, kMIDIPropertyUniqueID, &assigned) == noErr)
        [defaults setInteger:assigned forKey:key];
}

}  // namespace

void mirageStartVirtualMidi()
{
    if (gClient) return;
    openLog();
    OSStatus cs = MIDIClientCreate(CFSTR("Mirage Remake"), nullptr, nullptr, &gClient);
    if (cs != noErr) {
        gClient = 0;
        logText("MIDIClientCreate failed: " + std::to_string(int(cs)));
        return;
    }
    cs = MIDISourceCreate(gClient, CFSTR("Mirage Remake"), &gSource);
    if (cs == noErr)
        restoreUniqueId(gSource, kSourceIdKey);
    else
        gSource = 0;
    logText("source \"Mirage Remake\": " + std::string(cs == noErr ? "created" : "FAILED " + std::to_string(int(cs))));

    // The MIDIPacketList API is deprecated from macOS 11 but works on every version the app
    // supports (10.15 and later); the replacement needs macOS 11.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    const OSStatus st = MIDIDestinationCreateWithBlock(
        gClient, CFSTR("Mirage Remake"), &gDestination, ^(const MIDIPacketList* list, void* srcRef) {
            (void)srcRef;
            const MIDIPacket* p = &list->packet[0];
            for (UInt32 i = 0; i < list->numPackets; ++i) {
                gInBytes += p->length;
                for (UInt16 k = 0; k < p->length; ++k) if (p->data[k] == 0xF0) gInSysex++;
                logBytes("in ", p->data, p->length);
                mirage::MirageEngine* engine = dpf::g_mirageEngine.load();
                if (!engine) logText("in : no engine yet, dropped");
                if (engine) {
                    // The audio thread empties the ring every block; wait for room if a large
                    // dump arrives faster than that.
                    for (int tries = 0; tries < 500 && !engine->postExternalMidi(p->data, int(p->length)); ++tries)
                        usleep(1000);
                }
                p = MIDIPacketNext(p);
            }
        });
#pragma clang diagnostic pop
    if (st == noErr)
        restoreUniqueId(gDestination, kDestinationIdKey);
    else
        gDestination = 0;
    logText("destination \"Mirage Remake\": " + std::string(st == noErr ? "created" : "FAILED " + std::to_string(int(st))));

    // MIDI OUT: collect what the Mirage sent every millisecond (a byte takes 320 us at MIDI
    // speed) and pass it on.
    dispatch_queue_t queue = dispatch_queue_create("mirage.midi-out", DISPATCH_QUEUE_SERIAL);
    gTimer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, queue);
    dispatch_source_set_timer(gTimer, DISPATCH_TIME_NOW, NSEC_PER_MSEC, NSEC_PER_MSEC / 4);
    dispatch_source_set_event_handler(gTimer, ^{
        mirage::MirageEngine* engine = dpf::g_mirageEngine.load();
        if (!engine) return;
        engine->drainMidiOut([](uint8_t b) { gOut.byte(b); });
    });
    dispatch_resume(gTimer);
}

// For the Settings window: bytes and SysEx messages through the port so far.
void mirageVirtualMidiStats(uint64_t& inBytes, uint64_t& inSysex, uint64_t& outBytes, uint64_t& outSysex)
{
    inBytes = gInBytes.load();
    inSysex = gInSysex.load();
    outBytes = gOutBytes.load();
    outSysex = gOutSysex.load();
}
