# KORE 1 Mixxx Bridge

**Native Instruments KORE 1 → macOS CoreMIDI → Mixxx**, with experimental MIDI feedback for LEDs and the built-in LCD.

> **Status: v0.10.0 experimental.** This is an independent community project, not affiliated with or endorsed by Native Instruments or Mixxx. Hardware writes are opt-in. No macOS build or end-to-end Mixxx feedback test has been performed in the packaging environment.

## Current status

| Feature | Status | Notes |
|---|---|---|
| USB device discovery | Tested on KORE 1 | VID:PID `17cc:4711`, interface 0 / alternate setting 1 |
| 8 endless encoders | Tested on KORE 1 | Relative MIDI CC 20–27; v6 adaptive filtering preserved |
| Digital buttons | Tested (subset) | 32 potential bits → Note On/Off 36–67; physical labels not fully verified |
| Large jog wheel | USB channel identified | `READ_IO` byte 4 (`ABS_MISC`); **not yet mapped to relative MIDI output** |
| 3 analog channels | Implemented, not mapped | CC 40–42; hardware controls not fully identified |
| LED self-test | Tested on KORE 1 | `--led-test play` and `F1` confirmed by tester; other positions sourced from Linux caiaq |
| LCD test frame | Tested on KORE 1 | 128×64, USB command `0x08`, contrast/backlight, test text |
| Live Mixxx LED/LCD feedback | Experimental | Mapping and CoreMIDI feedback destination implemented, routing not end-to-end verified |
| Dynamic Mixxx track info | Experimental | Title/artist via 7-bit SysEx; BPM/Play via CC |

## Requirements

- macOS with Apple CoreMIDI, an original **KORE 1** (not KORE 2).
- Xcode Command Line Tools (`xcode-select --install`).
- Homebrew `libusb` and `pkg-config` (`brew install libusb pkg-config`).
- Mixxx 2.4+ for the included experimental mapping.

## Build

```bash
make
```

Equivalent standalone compile command:

```bash
cc -std=c11 -Wall -Wextra -O2 -o kore-midi-v10 src/kore-midi-v10.c \
  $(pkg-config --cflags --libs libusb-1.0) \
  -framework CoreMIDI -framework CoreFoundation -lpthread
```

The executable is written to `build/kore-midi-v10` when using `make`.

## Quick start

1. Quit any other app/process that has claimed the KORE USB interface.
2. Connect KORE 1 to the Mac.
3. Start the bridge:

```bash
./build/kore-midi-v10 --erp-threshold 8 --erp4-spike-guard --verbose
```

4. In Mixxx, select the MIDI input **KORE 1 Bridge**. To test the included mapping, copy both files from `mixxx/` into the Mixxx controllers directory (normally `~/.mixxx/controllers/`; actual location varies by Mixxx install), then choose/enable the preset.
5. Set up the **MIDI output routing** from Mixxx to `KORE 1 Feedback` before enabling display feedback. Input selection alone **does not** guarantee that Mixxx routes output to the separate destination.

### Full-feedback mode (experimental)

```bash
./build/kore-midi-v10 --erp-threshold 8 --erp4-spike-guard \
  --mixxx-feedback --verbose
```

`--mixxx-feedback` creates a virtual source **KORE 1 Bridge** (hardware → DAW) and a virtual destination **KORE 1 Feedback** (DAW → hardware). It initializes the LCD/LEDs when activated. **Only start this mode when KORE 1 is connected and you intend to test USB output.**

### Safe, isolated hardware tests

List LED names without connecting the hardware:

```bash
./build/kore-midi-v10 --led-list
```

Test one LED:

```bash
./build/kore-midi-v10 --led-test play --led-level 35 --led-duration-ms 1200
```

Display test:

```bash
./build/kore-midi-v10 --lcd-test --lcd-contrast 45 \
  --lcd-backlight 65 --lcd-duration-ms 10000
```

Both test modes are explicitly opted into. **LED tests replace all 32 LED values and clear them afterward.** Do not run multiple bridges simultaneously. Avoid abrupt USB removal during an active write.

## Mixxx controls (provisional)

- Encoder CC 20–27 → example deck volume/gain/rate/crossfader/master gain assignments.
- Note 36 / 38 → select displayed deck A / B.
- Note 37 / 40 → deck A / B Play.
- Note 39 / 41 → deck A / B Cue.

These are **example mappings**, not a final physical button legend. Adjust the mapping to your preferred workflow and test each binding before a live set.

## Feedback protocol

The bridge accepts MIDI messages on `KORE 1 Feedback`:

- Channel 16 (`0xBF`) CC 0–31: LED brightness by hardware state index, MIDI 0–127 scaled to device range.
- CC 40: display deck (0 = A, 1 = B).
- CC 41: play state.
- CC 42 and 43: BPM × 10 split into high/low 7-bit parts.
- SysEx: `F0 7D 4B 01 <deck> <field> <printable ASCII bytes> F7`, field 1 = title, 2 = artist. `0x7D` is for noncommercial development use; this is **not** a manufacturer-approved protocol.

Refer to [docs/PROTOCOL.md](docs/PROTOCOL.md) for hardware packet information and [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the MIDI data flow.

## Troubleshooting

**`libusb.h` not found:** `brew install libusb pkg-config` and verify `pkg-config --cflags libusb-1.0`.

**`KORE 1 (17CC:4711) not found`:** confirm device model, cable and enumeration, and disconnect other software that owns the USB interface.

**`claim interface 0` fails:** another process may have the device open. Stop other bridge instances.

**No control in Mixxx:** make sure `KORE 1 Bridge` is enabled as MIDI input and the XML and JS mapping files are installed together.

**No LCD/LED feedback:** check `--mixxx-feedback`, verify output routing specifically to `KORE 1 Feedback`, and inspect `--verbose`. Routing is the main unvalidated integration point.

**Non-ASCII title text:** current script replaces such characters with `?`; 5×7 rendering truncates long text.

## Repository structure

```text
src/                 macOS C USB/CoreMIDI bridge
mixxx/               JS + MIDI XML Mixxx controller mapping
docs/                protocol, architecture, safety, roadmap
tests/               static tests usable without hardware
.github/workflows/   source and mapping lint on GitHub Actions
```

## Tests

```bash
make check
```

Checks XML parsing, JavaScript syntax (if Node.js is available), and documented protocol constants. Hardware and macOS compilation require a Mac connected to KORE 1. See [docs/TESTING.md](docs/TESTING.md).

## Roadmap

1. Complete all 32 physical button labels and map jog wheel movement to relative MIDI CC.
2. Verify full Mixxx MIDI output routing and bidirectional LED feedback.
3. Live track-title/artist/BPM refresh and optimized LCD updates.
4. Optional joystick/analog discovery and a configurable MIDI layout.
5. Robust reconnect/error recovery and distributable macOS binary.

## Credits and licensing

This project builds on public documentation and code from [OpenKoreBridge](https://github.com/OpenKoreBridge/OpenKoreBridge) and the Linux `snd-usb-caiaq` driver (GPL-2.0-or-later), especially ERP decoding, LED indexing and LCD packet sequences. The included source may contain derivative portions. **Before publication, confirm that contributors agree to distributing the resulting work under GPL-2.0-or-later and retain all applicable upstream copyright notices.** See [NOTICE.md](NOTICE.md). The current project distribution includes the GPL v2 text in `COPYING`; this does not establish ownership of third-party code.

Mixxx is a separate project and is not bundled here. Device and software trademarks remain with their owners.
