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
- **Edit:** **PARAM**, two digits, **VALUE**, then ▲ ▼ or digits. The parameter card is in
  `docs/` (see the Mirage manuals).
- **Tuning:** Master Tune (parameter 21) stays at its default of 50 and everything plays at
  A440: the factory library within ±3 cents, and a sampled 440 Hz tone plays back at 440 Hz. To get there, the emulated machine (CPU and sound chip together) runs 13.5 cents
  slower than the nominal 8 MHz. At exactly 8 MHz the factory disks play about 13.5 cents
  sharp, while sampled sounds, recorded and played on the same clock, are unaffected by the
  clock either way. The **FACTORY TUNING** switch above the power switch turns the clock
  change off (LED dark): the machine then runs at exactly 8 MHz, like the hardware, which
  suits third-party libraries tuned on real Mirages (e.g. Sound Cells, about 20 cents flat
  with factory tuning on). It is a host-automatable parameter in the plug-ins, and the app
  remembers it. Their small remaining offset (the OS plays a fresh sample 3.3 cents above
  the recorded pitch) is removed by feeding the input 3.3 cents slower while sampling.
- Computer keys in the editor: digits, Enter, Esc (Cancel), ↑ ↓, P (Param), V (Value).
- **Saving:** when the Mirage writes to a disk (save sound / config / format), the image is
  saved to `~/Library/Application Support/Mirage/Disks`. Copies of bundled disks go there too.
  Those disks then appear in the drive's list.
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

## Not done yet
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
