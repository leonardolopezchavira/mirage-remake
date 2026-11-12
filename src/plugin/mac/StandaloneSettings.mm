// Fork addition (mirage-remake), linked into the standalone app (Mirage.app) only.
//
// DPF's standalone program plays through CoreAudio and receives MIDI through CoreMIDI, but it
// has no menu bar and no settings: it always opens the default output at 48 kHz and no MIDI
// input. This file gives the app
//   - a standard app menu (About, Settings… ⌘, , Hide, Quit ⌘Q),
//   - an Audio/MIDI Settings window: output device, sample rate, buffer size and which MIDI
//     inputs play Mirage, with a Rescan button for devices plugged in later,
//   - and restores those choices at launch (NSUserDefaults, domain
//     com.github.leonardolopezchavira.mirage).
// The device and MIDI control itself is in patches/dpf-standalone-settings.patch.

#import <Cocoa/Cocoa.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "DistrhoStandaloneUtils.hpp"
#include "StandaloneHook.h"

namespace dpf = DISTRHO_NAMESPACE;

static NSString* const kDeviceKey = @"AudioOutputDevice";
static NSString* const kSampleRateKey = @"SampleRate";
static NSString* const kBufferSizeKey = @"BufferSize";
static NSString* const kDisabledMidiKey = @"DisabledMIDIInputs";

static NSString* toNS(const std::string& s) {
    NSString* result = [NSString stringWithUTF8String:s.c_str()];
    return result != nil ? result : @"";
}

static std::string fromNS(NSString* s) {
    return s != nil ? std::string(s.UTF8String) : std::string();
}

static NSString* rateTitle(unsigned rate) {
    if (rate % 1000 == 0) return [NSString stringWithFormat:@"%u kHz", rate / 1000];
    return [NSString stringWithFormat:@"%.1f kHz", rate / 1000.0];
}

// RtAudio names CoreAudio devices "Manufacturer: Device"; the menu shows the device part.
static NSString* deviceTitle(const std::string& name) {
    const std::string::size_type colon = name.find(": ");
    return toNS(colon == std::string::npos ? name : name.substr(colon + 2));
}

static NSTextField* makeLabel(NSString* text) {
    NSTextField* label = [NSTextField labelWithString:text];
    label.alignment = NSTextAlignmentRight;
    return label;
}

// ------------------------------------------------------------------------------------------

@interface MirageSettingsController : NSObject <NSWindowDelegate>
@property(strong) NSPanel* panel;
@property(strong) NSPopUpButton* devicePopup;
@property(strong) NSPopUpButton* ratePopup;
@property(strong) NSPopUpButton* bufferPopup;
@property(strong) NSStackView* midiStack;
@property(strong) NSTextField* statusLabel;
+ (instancetype)shared;
- (void)applySavedSettings;
- (void)installMenu;
@end

@implementation MirageSettingsController

+ (instancetype)shared {
    static MirageSettingsController* shared = nil;
    if (shared == nil) shared = [[MirageSettingsController alloc] init];
    return shared;
}

// ---------------------------------------------------------------- saved settings

- (std::vector<std::string>)savedDisabledMidi {
    std::vector<std::string> names;
    for (NSString* name in [NSUserDefaults.standardUserDefaults stringArrayForKey:kDisabledMidiKey])
        names.push_back(fromNS(name));
    return names;
}

- (void)saveDisabledMidi:(const std::vector<std::string>&)names {
    NSMutableArray* array = [NSMutableArray array];
    for (const std::string& name : names) [array addObject:toNS(name)];
    [NSUserDefaults.standardUserDefaults setObject:array forKey:kDisabledMidiKey];
}

- (void)applySavedSettings {
    if (!dpf::isUsingNativeAudio()) return;  // running under real JACK: JACK decides
    NSUserDefaults* defaults = NSUserDefaults.standardUserDefaults;

    // MIDI first: every input except the ones switched off (none, the first time)
    if (dpf::supportsMIDI()) dpf::setDisabledMIDIInputs([self savedDisabledMidi]);

    // A saved device that is not plugged in now: stay on the default, keep the choice saved
    NSString* device = [defaults stringForKey:kDeviceKey];
    if (device.length > 0 && fromNS(device) != dpf::getAudioOutputDeviceName())
        dpf::requestAudioOutputDevice(device.UTF8String);

    const NSInteger rate = [defaults integerForKey:kSampleRateKey];
    if (rate >= 44100 && static_cast<unsigned>(rate) != dpf::getCurrentSampleRate())
        dpf::requestSampleRateChange(static_cast<unsigned>(rate));

    const NSInteger frames = [defaults integerForKey:kBufferSizeKey];
    if (frames > 0 && static_cast<unsigned>(frames) != dpf::getBufferSize() &&
        dpf::supportsBufferSizeChanges())
        dpf::requestBufferSizeChange(static_cast<unsigned>(frames));
}

// ---------------------------------------------------------------- menu bar

- (void)installMenu {
    NSMenuItem* settings = [[NSMenuItem alloc] initWithTitle:@"Settings…"
                                                      action:@selector(showSettings:)
                                               keyEquivalent:@","];
    settings.target = self;

    NSMenu* mainMenu = NSApp.mainMenu;
    if (mainMenu.numberOfItems > 0 && mainMenu.itemArray.firstObject.submenu != nil) {
        // someone already made an app menu: add Settings… below its first item
        NSMenu* appMenu = mainMenu.itemArray.firstObject.submenu;
        const NSInteger at = MIN((NSInteger)1, appMenu.numberOfItems);
        [appMenu insertItem:NSMenuItem.separatorItem atIndex:at];
        [appMenu insertItem:settings atIndex:at + 1];
        return;
    }

    NSMenu* appMenu = [[NSMenu alloc] initWithTitle:@"Mirage"];
    [appMenu addItemWithTitle:@"About Mirage"
                       action:@selector(orderFrontStandardAboutPanel:)
                keyEquivalent:@""];
    [appMenu addItem:NSMenuItem.separatorItem];
    [appMenu addItem:settings];
    [appMenu addItem:NSMenuItem.separatorItem];
    [appMenu addItemWithTitle:@"Hide Mirage" action:@selector(hide:) keyEquivalent:@"h"];
    NSMenuItem* hideOthers = [appMenu addItemWithTitle:@"Hide Others"
                                                action:@selector(hideOtherApplications:)
                                         keyEquivalent:@"h"];
    hideOthers.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagOption;
    [appMenu addItemWithTitle:@"Show All" action:@selector(unhideAllApplications:) keyEquivalent:@""];
    [appMenu addItem:NSMenuItem.separatorItem];
    NSMenuItem* quit = [appMenu addItemWithTitle:@"Quit Mirage"
                                          action:@selector(quit:)
                                   keyEquivalent:@"q"];
    quit.target = self;

    NSMenuItem* appItem = [[NSMenuItem alloc] init];
    appItem.submenu = appMenu;
    mainMenu = [[NSMenu alloc] init];
    [mainMenu addItem:appItem];

    NSMenu* windowMenu = [[NSMenu alloc] initWithTitle:@"Window"];
    [windowMenu addItemWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"];
    NSMenuItem* windowItem = [[NSMenuItem alloc] init];
    windowItem.submenu = windowMenu;
    [mainMenu addItem:windowItem];
    NSApp.windowsMenu = windowMenu;

    NSApp.mainMenu = mainMenu;
}

- (void)quit:(id)sender {
    (void)sender;
    // Close the Mirage window the way its close button does, so the editor and the plug-in shut
    // down normally (DPF ends the program when its window closes).
    for (NSWindow* window in NSApp.windows)
        if (window != self.panel && window.isVisible) [window performClose:nil];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(1.5 * NSEC_PER_SEC)),
                   dispatch_get_main_queue(), ^{ [NSApp terminate:nil]; });
}

// ---------------------------------------------------------------- settings window

- (void)showSettings:(id)sender {
    (void)sender;
    if (self.panel == nil) [self buildPanel];
    [self refresh];
    [self.panel makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

- (void)buildPanel {
    NSPanel* panel = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 460, 300)
                                                styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                                  backing:NSBackingStoreBuffered
                                                    defer:YES];
    panel.title = @"Audio/MIDI Settings";
    panel.hidesOnDeactivate = NO;
    panel.releasedWhenClosed = NO;
    panel.delegate = self;

    self.devicePopup = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    self.devicePopup.target = self;
    self.devicePopup.action = @selector(deviceChanged:);
    self.ratePopup = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    self.ratePopup.target = self;
    self.ratePopup.action = @selector(rateChanged:);
    self.bufferPopup = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    self.bufferPopup.target = self;
    self.bufferPopup.action = @selector(bufferChanged:);
    self.statusLabel = [NSTextField labelWithString:@""];
    self.statusLabel.textColor = NSColor.secondaryLabelColor;
    self.statusLabel.font = [NSFont systemFontOfSize:NSFont.smallSystemFontSize];

    NSGridView* audioGrid = [NSGridView gridViewWithViews:@[
        @[ makeLabel(@"Output device:"), self.devicePopup ],
        @[ makeLabel(@"Sample rate:"), self.ratePopup ],
        @[ makeLabel(@"Buffer size:"), self.bufferPopup ],
        @[ [NSGridCell emptyContentView], self.statusLabel ],
    ]];
    audioGrid.rowSpacing = 8;
    audioGrid.columnSpacing = 8;
    [audioGrid columnAtIndex:0].xPlacement = NSGridCellPlacementTrailing;
    [audioGrid columnAtIndex:1].width = 300;

    NSTextField* midiTitle = [NSTextField labelWithString:@"MIDI inputs that play Mirage:"];
    midiTitle.font = [NSFont boldSystemFontOfSize:NSFont.systemFontSize];
    self.midiStack = [NSStackView stackViewWithViews:@[]];
    self.midiStack.orientation = NSUserInterfaceLayoutOrientationVertical;
    self.midiStack.alignment = NSLayoutAttributeLeading;
    self.midiStack.spacing = 4;
    NSButton* rescan = [NSButton buttonWithTitle:@"Rescan MIDI"
                                          target:self
                                          action:@selector(rescanMidi:)];

    NSStackView* content = [NSStackView stackViewWithViews:@[ audioGrid, midiTitle, self.midiStack, rescan ]];
    content.orientation = NSUserInterfaceLayoutOrientationVertical;
    content.alignment = NSLayoutAttributeLeading;
    content.spacing = 12;
    content.edgeInsets = NSEdgeInsetsMake(20, 20, 20, 20);
    [content setCustomSpacing:20 afterView:audioGrid];
    panel.contentView = content;

    [panel center];
    self.panel = panel;
}

- (void)refresh {
    // output devices; menu items rather than addItemWithTitle:, which drops duplicate names
    [self.devicePopup removeAllItems];
    NSMenuItem* systemDefault = [[NSMenuItem alloc] initWithTitle:@"System Default" action:nil keyEquivalent:@""];
    systemDefault.representedObject = @"";
    [self.devicePopup.menu addItem:systemDefault];
    const std::string current = dpf::getAudioOutputDeviceName();
    NSMenuItem* selected = systemDefault;
    for (const std::string& name : dpf::getAudioOutputDeviceNames()) {
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:deviceTitle(name) action:nil keyEquivalent:@""];
        item.representedObject = toNS(name);
        item.toolTip = toNS(name);
        [self.devicePopup.menu addItem:item];
        if (!current.empty() && name == current) selected = item;
    }
    [self.devicePopup selectItem:selected];

    // sample rates the device offers (44.1 kHz and up), and the one in use
    const unsigned rate = dpf::getCurrentSampleRate();
    std::vector<unsigned> rates = dpf::getSupportedSampleRates();
    if (rate != 0 && std::find(rates.begin(), rates.end(), rate) == rates.end()) rates.push_back(rate);
    std::sort(rates.begin(), rates.end());
    [self.ratePopup removeAllItems];
    for (const unsigned r : rates) {
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:rateTitle(r) action:nil keyEquivalent:@""];
        item.tag = r;
        [self.ratePopup.menu addItem:item];
    }
    [self.ratePopup selectItemWithTag:rate];
    self.ratePopup.enabled = rates.size() > 1;

    // buffer sizes
    const unsigned frames = dpf::getBufferSize();
    std::vector<unsigned> sizes = {32, 64, 128, 256, 512, 1024, 2048};
    if (frames != 0 && std::find(sizes.begin(), sizes.end(), frames) == sizes.end()) {
        sizes.push_back(frames);
        std::sort(sizes.begin(), sizes.end());
    }
    [self.bufferPopup removeAllItems];
    for (const unsigned n : sizes) {
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:[NSString stringWithFormat:@"%u samples", n]
                                                      action:nil
                                               keyEquivalent:@""];
        item.tag = n;
        [self.bufferPopup.menu addItem:item];
    }
    [self.bufferPopup selectItemWithTag:frames];
    self.bufferPopup.enabled = dpf::supportsBufferSizeChanges();

    if (rate != 0 && frames != 0)
        self.statusLabel.stringValue = [NSString stringWithFormat:@"Playing at %@, %u-sample buffer (%.1f ms latency)",
                                                                  rateTitle(rate), frames, 1000.0 * frames / rate];
    else
        self.statusLabel.stringValue = @"Audio output is not running.";

    [self refreshMidi];
}

- (void)refreshMidi {
    for (NSView* view in self.midiStack.arrangedSubviews) [view removeFromSuperview];

    const std::vector<std::string> names = dpf::getMIDIInputNames();
    const std::vector<std::string> disabled = dpf::getDisabledMIDIInputs();
    if (names.empty()) {
        NSTextField* none = [NSTextField labelWithString:@"No MIDI inputs found. Connect a keyboard, then click Rescan MIDI."];
        none.textColor = NSColor.secondaryLabelColor;
        [self.midiStack addArrangedSubview:none];
        return;
    }
    for (const std::string& name : names) {
        NSButton* box = [NSButton checkboxWithTitle:toNS(name) target:self action:@selector(midiToggled:)];
        box.state = std::find(disabled.begin(), disabled.end(), name) == disabled.end()
                        ? NSControlStateValueOn
                        : NSControlStateValueOff;
        [self.midiStack addArrangedSubview:box];
    }
}

- (void)showFailure:(NSString*)message {
    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = message;
    alert.informativeText = @"Mirage kept the previous setting.";
    [alert beginSheetModalForWindow:self.panel completionHandler:nil];
}

// ---------------------------------------------------------------- actions

- (void)deviceChanged:(NSPopUpButton*)popup {
    NSString* name = popup.selectedItem.representedObject;
    if (dpf::requestAudioOutputDevice(name.UTF8String)) {
        if (name.length > 0)
            [NSUserDefaults.standardUserDefaults setObject:name forKey:kDeviceKey];
        else
            [NSUserDefaults.standardUserDefaults removeObjectForKey:kDeviceKey];
    } else {
        [self showFailure:[NSString stringWithFormat:@"“%@” could not be opened.", popup.selectedItem.title]];
    }
    [self refresh];
}

- (void)rateChanged:(NSPopUpButton*)popup {
    const unsigned rate = static_cast<unsigned>(popup.selectedItem.tag);
    if (dpf::requestSampleRateChange(rate))
        [NSUserDefaults.standardUserDefaults setInteger:rate forKey:kSampleRateKey];
    else
        [self showFailure:[NSString stringWithFormat:@"The output device does not accept %@.", rateTitle(rate)]];
    [self refresh];
}

- (void)bufferChanged:(NSPopUpButton*)popup {
    const unsigned frames = static_cast<unsigned>(popup.selectedItem.tag);
    if (dpf::requestBufferSizeChange(frames))
        [NSUserDefaults.standardUserDefaults setInteger:frames forKey:kBufferSizeKey];
    else
        [self showFailure:[NSString stringWithFormat:@"A %u-sample buffer could not be used.", frames]];
    [self refresh];
}

- (void)midiToggled:(NSButton*)sender {
    (void)sender;
    // Keep switched-off inputs that are not plugged in right now, so they stay off when they return
    const std::vector<std::string> present = dpf::getMIDIInputNames();
    std::vector<std::string> disabled;
    for (const std::string& name : [self savedDisabledMidi])
        if (std::find(present.begin(), present.end(), name) == present.end()) disabled.push_back(name);
    for (NSView* view in self.midiStack.arrangedSubviews) {
        if (![view isKindOfClass:NSButton.class]) continue;
        NSButton* box = (NSButton*)view;
        if (box.state != NSControlStateValueOn) disabled.push_back(fromNS(box.title));
    }
    dpf::setDisabledMIDIInputs(disabled);
    [self saveDisabledMidi:disabled];
}

- (void)rescanMidi:(id)sender {
    (void)sender;
    // reconnecting picks up inputs plugged in since launch
    dpf::setDisabledMIDIInputs([self savedDisabledMidi]);
    [self refreshMidi];
}

// ---------------------------------------------------------------- CI self-test

// MIRAGE_SELFTEST=1: exercise every setting on the real CoreAudio stack, print the results and
// quit through the menu's Quit path. Used by .github/workflows/mac-standalone.yml.
- (void)selfTest {
    bool ok = dpf::isUsingNativeAudio();
    std::printf("SELFTEST native audio: %d\n", ok);
    const std::vector<std::string> devices = dpf::getAudioOutputDeviceNames();
    for (const std::string& name : devices) std::printf("SELFTEST device: %s\n", name.c_str());
    ok = ok && !devices.empty();
    if (!devices.empty()) {
        const bool picked = dpf::requestAudioOutputDevice(devices.back().c_str());
        std::printf("SELFTEST pick '%s': %d, now '%s'\n", devices.back().c_str(), picked,
                    dpf::getAudioOutputDeviceName().c_str());
        ok = ok && picked && dpf::getAudioOutputDeviceName() == devices.back();
    }
    for (const unsigned rate : dpf::getSupportedSampleRates()) {
        const bool changed = dpf::requestSampleRateChange(rate);
        std::printf("SELFTEST rate %u: %d, now %u\n", rate, changed, dpf::getCurrentSampleRate());
        ok = ok && changed && dpf::getCurrentSampleRate() == rate;
    }
    const bool resized = dpf::requestBufferSizeChange(256);
    std::printf("SELFTEST buffer 256: %d, now %u\n", resized, dpf::getBufferSize());
    ok = ok && resized;
    const bool missing = dpf::requestAudioOutputDevice("No Such Device");
    std::printf("SELFTEST missing device refused: %d, still '%s'\n", !missing,
                dpf::getAudioOutputDeviceName().c_str());
    ok = ok && !missing;
    ok = ok && dpf::requestAudioOutputDevice("");
    std::printf("SELFTEST MIDI inputs: %zu, set disabled: %d\n", dpf::getMIDIInputNames().size(),
                dpf::setDisabledMIDIInputs({}));
    [self showSettings:nil];  // builds and fills the window
    std::printf("SELFTEST settings window: %d\n", self.panel.isVisible);
    ok = ok && self.panel.isVisible;
    std::printf("SELFTEST %s\n", ok ? "OK" : "FAILED");
    std::fflush(stdout);
    [self.panel close];
    [self quit:nil];
}

@end

// ------------------------------------------------------------------------------------------

START_NAMESPACE_DISTRHO

namespace {

void startStandalone()
{
    static bool started = false;
    if (started) return;
    started = true;

    [[MirageSettingsController shared] installMenu];
    // The editor opens while DPF is still setting up the audio client: apply the saved device,
    // rate and buffer once the program is running, when sample rate changes reach the plug-in.
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.3 * NSEC_PER_SEC)),
                   dispatch_get_main_queue(), ^{
                       [[MirageSettingsController shared] applySavedSettings];
                       if (std::getenv("MIRAGE_SELFTEST") != nullptr)
                           [[MirageSettingsController shared] selfTest];
                   });
}

struct Install {
    Install() { g_mirageStandaloneStart = startStandalone; }
} install;

}  // namespace

END_NAMESPACE_DISTRHO
