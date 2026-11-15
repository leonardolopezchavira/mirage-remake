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

#include <cstdint>
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

// CoreMIDI wants each packet to hold complete messages (SysEx may be split across packets but
// not mixed with other messages). The ACIA hands over a raw byte stream, so this reassembles
// it: running status is expanded, SysEx is passed on in chunks as it arrives.
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
        if (inSysex_) { sysex_.push_back(b); return; }
        if (!status_) return;                                           // no status yet
        data_[have_++] = b;
        if (have_ == need_) {
            uint8_t msg[3] = {status_, data_[0], data_[1]};
            emit(msg, need_ + 1);
            have_ = 0;
            if (status_ >= 0xF0) status_ = 0;                            // no running status
        }
    }

    // Ends the batch: sends what is complete, plus any SysEx received so far.
    void finish()
    {
        if (inSysex_) flushSysex();
        send();
    }

private:
    uint8_t status_ = 0, data_[2] = {};
    int have_ = 0, need_ = 0;
    bool inSysex_ = false;
    std::vector<uint8_t> sysex_;
    alignas(MIDIPacketList) uint8_t buf_[16384];
    MIDIPacketList* list_ = reinterpret_cast<MIDIPacketList*>(buf_);
    MIDIPacket* cur_ = nullptr;

    void flushSysex()
    {
        if (!sysex_.empty()) emit(sysex_.data(), int(sysex_.size()));
        sysex_.clear();
    }

    void emit(const uint8_t* p, int n)
    {
        while (n > 0) {
            const int chunk = n < 256 ? n : 256;
            if (!cur_) cur_ = MIDIPacketListInit(list_);
            MIDIPacket* next = MIDIPacketListAdd(list_, sizeof(buf_), cur_, 0, ByteCount(chunk), p);
            if (!next) {            // list full: send it and start a new one
                send();
                cur_ = MIDIPacketListInit(list_);
                next = MIDIPacketListAdd(list_, sizeof(buf_), cur_, 0, ByteCount(chunk), p);
                if (!next) return;
            }
            cur_ = next;
            p += chunk;
            n -= chunk;
        }
    }

    void send()
    {
        if (cur_ && list_->numPackets > 0 && gSource) MIDIReceived(gSource, list_);
        cur_ = nullptr;
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
    if (MIDIClientCreate(CFSTR("Mirage Remake"), nullptr, nullptr, &gClient) != noErr) {
        gClient = 0;
        NSLog(@"Mirage: could not create the MIDI client; no virtual MIDI port");
        return;
    }
    if (MIDISourceCreate(gClient, CFSTR("Mirage Remake"), &gSource) == noErr)
        restoreUniqueId(gSource, kSourceIdKey);
    else
        gSource = 0;

    // The MIDIPacketList API is deprecated from macOS 11 but works on every version the app
    // supports (10.15 and later); the replacement needs macOS 11.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    const OSStatus st = MIDIDestinationCreateWithBlock(
        gClient, CFSTR("Mirage Remake"), &gDestination, ^(const MIDIPacketList* list, void* srcRef) {
            (void)srcRef;
            const MIDIPacket* p = &list->packet[0];
            for (UInt32 i = 0; i < list->numPackets; ++i) {
                mirage::MirageEngine* engine = dpf::g_mirageEngine.load();
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

    // MIDI OUT: collect what the Mirage sent every millisecond (a byte takes 320 us at MIDI
    // speed) and pass it on.
    dispatch_queue_t queue = dispatch_queue_create("mirage.midi-out", DISPATCH_QUEUE_SERIAL);
    gTimer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, queue);
    dispatch_source_set_timer(gTimer, DISPATCH_TIME_NOW, NSEC_PER_MSEC, NSEC_PER_MSEC / 4);
    dispatch_source_set_event_handler(gTimer, ^{
        mirage::MirageEngine* engine = dpf::g_mirageEngine.load();
        if (!engine) return;
        engine->drainMidiOut([](uint8_t b) { gOut.byte(b); });
        gOut.finish();
    });
    dispatch_resume(gTimer);
}
