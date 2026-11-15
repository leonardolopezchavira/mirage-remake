# Mirage

A recreation of the **Ensoniq Mirage** sampler (1984), with an editor modelled on the
DMS-8 rack unit's front panel, for macOS (Apple Silicon and Intel): a
standalone app, `Mirage.app`, plus **Audio Unit, VST3 and CLAP** plug-ins.

> **Unofficial project.** Not affiliated with Ensoniq or its successors.

## How it works

Instead of re-implementing the Mirage's behaviour, this project emulates its hardware and runs
the **original boot ROM and operating system**:

| Part | Chip | Emulation |
|---|---|---|
| CPU | Motorola 6809E, 2 MHz | `src/emu/Cpu6809.*`, cycle-counted |
| Timers, keypad, display | MOS 6522 VIA | `src/emu/Via6522.*` |
| MIDI | Motorola 6850 ACIA | `src/emu/Acia6850.h`, paced at 31250 baud |
| Floppy | WD1772 + 3.5" drive | `src/emu/Wd1772.*`, Mirage disk format (80 × 5632 bytes) |
| Sound | Ensoniq ES5503 DOC, 32 oscillators, 128 KB sample RAM | `src/emu/Es5503.*` |
| Filters | 8 × Curtis CEM3328 4-pole low-pass | `src/emu/Cem3328.h` |

So every Mirage behaviour, from the parameters and the keypad to voice allocation, envelopes,
the LFO, MIDI and disk handling, comes from Ensoniq's own code. The OS on the disk you insert
decides what you're playing, exactly as on the instrument: stock OS 3.2, MASOS or SoundProcess.

### The filters

At power-on the boot ROM tunes each of the eight analog filters. It drives each one into
self-oscillation through the DAC, times the waveform with the A/D converter, and stores a
per-filter offset. The filter model therefore self-oscillates, with the pitch following the
cutoff DAC exponentially at **24 codes per octave**. That figure was measured from the OS
itself: with filter tracking at 4 ("1 octave per octave"), the cutoff moves 2 codes per semitone.
Each filter gets a small simulated component tolerance, and the ROM's tuning compensates for it
just as it does on real hardware.

## Using it

The app boots from the built-in OS 3.2 disk (piano, voices and strings) in about a second.
Behind the scenes that is roughly 6 seconds of machine time, run faster than real time.

- **Play** from MIDI (any channel, OS 3.2 defaults to omni) or the on-screen keyboard.
- **Disk drive:** ◀ ▶ step through the factory library (A1–C10), the system disks and your
  own disk folder. **Open…** inserts a disk image from anywhere: raw `.img` (450 560 bytes)
  or HxC `.hfe` (v1). `.edm` files are not supported; use the `.img` of the same disk.
- **Your disk folder:** `~/Library/Application Support/Mirage/Disks`. `.img` and `.hfe`
  files placed there appear in the ◀ ▶ list (read when the app or plug-in opens). Then use the Mirage's own keys:
  **LOAD LOWER** (or **LOAD UPPER**), then 1–3, then **ENTER**.
- **Edit:** **PARAM**, two digits, **VALUE**, then ▲ ▼. Holding ▲ or ▼ (on the panel or the
  computer's arrow keys) repeats at about 7 steps per second, the fastest the OS accepts
  presses (an addition: the hardware steps once per press). The parameter card is in
  `docs/` (see the Mirage manuals).
- **Tuning:** Master Tune (parameter 21) stays at its default of 50. The factory library plays
  within ±3 cents of A440, because the emulated machine (CPU and sound chip together) runs
  13.5 cents slower than the nominal 8 MHz; at exactly 8 MHz the factory disks play about
  13.5 cents sharp. Sounds you sample are recorded and played on the same clock, so the clock
  doesn't change them: as on the hardware, the OS plays a fresh sample about 3 cents above
  the recorded pitch (440 Hz in, about 440.8 Hz out).
- **FACTORY TUNING** (the switch above the power switch) turns the clock change off (LED
  dark): the machine then runs at exactly 8 MHz, like the hardware, which suits third-party
  libraries tuned on real Mirages (e.g. Sound Cells, about 20 cents flat with factory tuning
  on). It is a host-automatable parameter in the plug-ins, and the app remembers it.
- Computer keys in the editor: digits, Enter, Esc (Cancel), ↑ ↓, P (Param), V (Value),
  U (Load Upper), L (Load Lower), R (power off and on). Holding U and L together loads both
  halves, as pressing both LOAD keys does on the hardware.
- **POWER** (an addition): click the power switch to turn the Mirage off and on. It then
  boots from the disk in the drive, the hardware's way of loading another operating system:
  inserting a disk never replaces the OS that is running. Use it after inserting MASOS, the
  formatter disk or any other OS disk. The app and plug-ins remember that disk and boot from
  it the next time they start, like a Mirage switched off with the disk still in. To go back
  to the built-in OS 3.2, pick `os32` with ◀ ▶ and click POWER again. (If the remembered
  disk has been moved or deleted, the built-in OS 3.2 starts instead.)
- **New disks:** **New** (next to Open…) creates a blank formatted disk with OS 3.2,
  `My Disk 1.img`, `My Disk 2.img`… in `~/Library/Application Support/Mirage/Disks`, and
  inserts it. Save into it with the Mirage's own commands: **PARAM 1 1** (lower sound) or
  **PARAM 1 2** (upper), then the slot **1–3**, then **ENTER**. Each disk holds three lower
  and three upper sounds, and boots on its own (it carries the OS). The blank template
  (`resources/disks/Template/Blank.img`) was made by Ensoniq's own formatter disk (`fmt2`)
  running in the emulator.
- **Saving:** when the Mirage writes to a disk, disks in your folder are updated in place;
  bundled disks and `.hfe` files are never changed: the written image is saved as a copy in
  your folder, which then appears in the drive's list.
- **Fast disk** (on by default): while the drive is running, the machine runs 8× faster, so
  loading takes about a second instead of 10. Turn it off for authentic loading times.

### Install (macOS)

Download `Mirage-macOS-universal.zip` from the
[mac-latest release](https://github.com/leonardolopezchavira/mirage-remake/releases/tag/mac-latest),
copy `Mirage.app` to `/Applications` and the plug-ins to `~/Library/Audio/Plug-Ins/`
(`Components`, `VST3`, `CLAP`), then remove the quarantine flag once:

```sh
xattr -dr com.apple.quarantine /Applications/Mirage.app ~/Library/Audio/Plug-Ins/Components/Mirage.component \
  ~/Library/Audio/Plug-Ins/VST3/Mirage.vst3 ~/Library/Audio/Plug-Ins/CLAP/Mirage.clap
```

### Sampling

1. **Choose the input** (standalone app): **Mirage → Settings… (⌘,)**, then *Sampling input*
   (any audio device, including interfaces) and *Input channel* (a single input, or a pair mixed
   to mono). macOS asks once for microphone permission. In a DAW, the plug-in samples
   whatever the host sends to its audio input. The **INPUT** meter on the panel shows the
   signal arriving.
2. **Line or mic:** parameter 75 (on = line level, off = mic with the Mirage's compressor).
   The input filter is parameter 74, the threshold 76, the sampling rate 73.
3. **SAMPLE LOWER** (or UPPER). "SL" flashes, then the display becomes the Mirage's level
   meter. Adjust the source level until the top bar barely flickers.
4. **ENTER**, then play the sound. Recording starts when the signal crosses the threshold,
   lasts about 2 seconds, and ends with "SF". Play it back on that half of the keyboard
   (the original pitch is on the E below middle C).

As on the hardware, the input passes through voice 7's filter (the input filter) into the
8-bit converter, paced by the VIA timer at the sampling rate.

### The built-in editor

Click **Editor** (by the disk drive) to show every parameter of the selected half on one
page in place of the on-screen keyboard: the program (27–38), both envelopes with graphs
(40–59), and the selected wavesample (60–72) with its waveform. Drag a bar to change a
value, or use the scroll wheel for single steps; click a switch to flip it. On the
waveform, drag the red **S** and **E** markers (upper half) to set the sample start and end,
and the white **L** markers (lower half) to move the loop. The view shows a margin around
the sample, dimmed, so the markers have room to move; the Mirage moves the loop to the new
end when the sample's end changes, as it does on the front panel. **Lower/Upper**, **Program 1–4** and
**Wavesample 1–8** select what you edit (and play), as the Mirage's own keys do. The
**keymap** under the program shows which wavesample plays which keys in the selected
program (starting from its initial wavesample, ▸): click a band to select that
wavesample, drag a white line to move its top key. **Key map** (in the editor's header)
switches to a full key map of all 16 wavesamples, laid out like BoxedEar Visage's: one row
per wavesample (U8–U1, L8–L1), filled from the bottom C up to its top key. Click a row to
select it, then click or drag in it (or use ‹ ›) to set its top key. Values
appear as the Mirage's display shows them, and edits on the front panel show up in the
editor straight away. Click **Editor** again to bring the keyboard back.

The editor works with **OS 3.2** (the built-in disk `os32`, and factory disks A1–C9; C10
carries a different OS the editor doesn't recognise yet) and
with **MASOS_BE**; it connects by itself to whichever is running.

- **OS 3.2:** OS 3.2 keeps each half's sound in memory in the same layout as MASOS's SysEx
  dump, so the editor reads every value directly. Edits are written into memory; the few
  that OS 3.2 has to recalculate (27 initial wavesample, 60–65 sample and loop, 72 top
  key), and the selection of half, program and wavesample, are made with front-panel key
  presses, so the OS applies its own checks, as when you press the keys yourself. Those
  take about a second each and the display shows the parameter while they happen.
- **MASOS_BE:** through BoxedEar's MIDI protocol: **Open…** `MASOS_BE-v1.0.0.hfe`, click
  **POWER**. (MASOS v2 shows the values but ignores edits.)

Load your sounds as usual; the editor follows. Edits change the sound in memory, as on the
hardware: save with **PARAM 1 1** / **1 2**, a slot and **ENTER** to keep them. It works in
the plug-ins too.

### Importing an audio file

**Import…** in the editor's header samples a WAV or AIFF file (any rate, mono or stereo,
8–32-bit or float) into the half chosen with **Lower / Upper**. Nothing is copied in
digitally: the file is played into the Mirage's sampling input while **SAMPLE** and
**ENTER** are pressed for you, so it goes through the input filter (VCF 7, parameter 74), the
8-bit converter and the OS's own sampling routine, with the current settings (73 sampling
time, 75 line/mic, 76 threshold, 77 multisample), exactly as a sound arriving at the input
jack would. Recording starts where the file crosses the threshold and lasts as long as a
normal sample (about 2 seconds at the default rate); the file is played at a fixed level
(peak −7 dB). Then play, loop and save it as usual (**PARAM 1 1** or **1 2**, a slot,
**ENTER**).

This needs **OS 3.2** to be running (the editor works there too): under MASOS the emulated
Mirage doesn't sample yet (MASOS shows a message and records nothing), so the import is
refused.

### Editors (BoxedEar Visage) and MIDI out

The standalone app creates a virtual MIDI port named **Mirage Remake**, as both a source
and a destination. It is the emulated Mirage's two MIDI sockets: whatever the OS sends
through its MIDI OUT (SysEx dumps, parameter changes, program and wavesample status, MIDI
echo) comes out of the source, and the destination plays into its MIDI IN alongside the
keyboards chosen in Settings. Any editor, librarian or DAW on the Mac can use it.

To use [BoxedEar Visage](https://boxedear.com/software/visage.html):
1. Download Visage and the MASOS_BE disk from BoxedEar (they are BoxedEar's and are not
   included here).
2. In Mirage, **Open…** `MASOS_BE-v1.0.0.hfe`, then click **POWER** (or press R) so the
   Mirage boots from it, as a hardware Mirage does when switched on with the disk in. Load
   your sounds from your sound disks as usual afterwards.
3. In Visage, choose **Mirage Remake** for both MIDI input and MIDI output.

MASOS_BE sends its configuration and both program dumps at boot and answers the requests
in BoxedEar's published MIDI spec. Stock OS 3.2 sends only notes and program changes.
The port exists in the standalone app only. In a DAW, use the DAW's own MIDI routing (the
plug-ins have no MIDI output yet).

## Not done yet
- **Sampling under MASOS** (v2 or MASOS_BE) doesn't work in the emulator yet: after SAMPLE,
  MASOS waits for keys instead of reading the input and ends with a message. OS 3.2 samples.
- **The DSK-8 keyboard controller** (R6500/11) is not emulated. Play from MIDI, as with the
  DMS-8 rack.
- **Plug-in state** stores the inserted disk's path only, not the full machine state. A sound
  edited in a project but not saved to disk comes back as it was on the disk.

## Building

```sh
git clone --recursive https://github.com/leonardolopezchavira/mirage-remake
cmake -S mirage-remake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/mirage_engine_check     # boots, plays a chord, checks speed
```

`build/mirage_cli` runs the machine headless, which is handy for testing, for example:

```sh
./build/mirage_cli resources/rom/mirage.bin disk resources/disks/OS/os32.img run 8 show \
    key PARAM key 3 key 8 key VALUE show midi 90,3c,64 wav note.wav 2 filters
```

## Credits

- Memory map, I/O layout and ES5503 details: MAME's `enmirage.cpp` and `es5503.cpp`
  (R. Belmont, tim lindner; BSD-3-Clause).
- ROM disassembly: Tim Victor, Gordon JC Pearce, and Alessandro Fasan's
  [AlfaSoniQMirage](https://github.com/alfa95139/AlfaSoniQMirage).
- Disk images and manuals: the [ZAPPPP/Mirage](https://github.com/ZAPPPP/Mirage) collection.
- Plug-in framework: [DPF](https://github.com/DISTRHO/DPF). The standalone settings window
  comes from [sq80-remake](https://github.com/leonardolopezchavira/sq80-remake).
