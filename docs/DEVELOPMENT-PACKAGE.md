# KORE 1 macOS development package

This package collects the controller bridge, experimental USB audio transports,
local CoreAudio output plug-in, and Mixxx mappings. It is a development milestone,
not a finished general-purpose audio driver. Built binaries target the build
machine's architecture; the current package is Apple Silicon. macOS 12+ is required.

## Build and run

From the source checkout, run `./build.sh` or `make development-package`.
Requires Xcode Command Line Tools, `pkg-config` and `libusb-1.0`.
`build/KORE1-macOS-development.zip` contains compiled binaries, the signed local
plug-in, standalone bridge source, mappings and license notices. No system driver
is installed, no audio service restarted, and no default audio device changed.
The libusb bridge binaries require the same Homebrew libusb runtime as the build
machine; this zip is not a self-contained installer for other Macs.

`kore1-final-bridge.c` is a generated standalone C translation unit: CoreMIDI,
controller decoding, filters, LED/LCD feedback, CAIAQ framing and the experimental
libusb audio paths. It has no project-local include dependencies. Rebuild with:

```sh
cc -std=c11 -Wall -Wextra -Werror -O2 \
  $(pkg-config --cflags libusb-1.0) -o kore1-final-bridge kore1-final-bridge.c \
  $(pkg-config --libs libusb-1.0) \
  -framework CoreMIDI -framework CoreFoundation -lpthread -lm
```

Maintain `kore1-bridge-audio.c` and `audio/caiaq.h`; regenerate with
`python3 automation/generate_bridge.py`. `--check` verifies reproducibility.
“Final” names the requested consolidated file; it does not certify audio quality.

Start the controller with `./build/kore1-final-bridge --mixxx-feedback` in the
checkout, or `./kore1-final-bridge --mixxx-feedback` inside the extracted package.
No audio output is enabled by default. `--help` lists the opt-in audio diagnostics.
The libusb audio modes remain experimental and have not demonstrated the native
transport's successful variable-packet analog playback. Do not use them as a
Mixxx sound card.

## Native audio and CoreAudio

`kore1-native-probe` uses Apple's IOUSBHost API and must be compiled as Objective-C;
a pure C source cannot directly replace that framework implementation. Its tested
RX-matched playback remains a separate executable. Only one process may own the
KORE interface at a time: do not run the native audio probe and MIDI bridge together.

From the checkout, a bounded silence test is:

```sh
./build/kore1-native-probe --dry-run --stream-silence
./build/kore1-native-probe --run --stream-silence --duration-ms 3000
```

Run without arguments to preview the native configuration without device access. Disconnect speakers/headphones and minimize
output gain. Tone and recording tests require the separately documented loopback
setup in TESTING.md; packaging and offline checks never run these tests.

`KoreAudio.driver` is an ad-hoc signed local output-only plug-in prototype.
It is included for offline loading and property tests, and has not been installed
or tested in the system HAL. Do not copy it into a system plug-in directory as part
of this build. `kore-coreaudio-render` requires an installed device and is supplied
for future explicit integration testing. KORE audio input, sustained full-level
playback quality, settled clock tracking, and simultaneous native MIDI/audio remain
unfinished. Gap-free MAYA44 loopback capture was verified in earlier short tests;
it does not establish production playback quality.

## Mixxx

Copy both `mixxx/KORE-1.midi.xml` and `mixxx/KORE-1.js` to Mixxx's user controller
mapping directory (open it from Mixxx Preferences → Controllers). Reload Mixxx,
enable input `KORE 1 Bridge`, and select the KORE 1 mapping. These are controller
preset files, not `.mid` music sequences. Existing root v10 mappings are preserved.

| Incoming MIDI, channel 1 | Action |
| --- | --- |
| Relative CC20 / CC21 | Deck A / B volume |
| Relative CC22 / CC23 | Deck A / B pregain |
| Relative CC24 / CC25 | Deck A / B pitch rate |
| Relative CC26 / CC27 | Crossfader / master gain |
| Notes36 / 38 | Select A / B for LCD feedback |
| Notes37 / 40 | Toggle A / B playback |
| Notes39 / 41, including Note Off | A / B momentary cue |

Relative values 1–63 increment, 65–127 decrement, and zero is idle. Other reported
buttons, jog and analog CCs are available for custom mappings but unassigned here.
Metadata is optional if `engine.getPlayer` is unavailable. API references:
[Mixxx scripting](https://github.com/mixxxdj/mixxx/wiki/midi-scripting) and
[official declarations](https://github.com/mixxxdj/mixxx/blob/main/res/controllers/engine-api.d.ts).

The bridge feedback sink is a separate CoreMIDI endpoint, `KORE 1 Feedback`.
Mixxx must route the mapping's MIDI output there for LEDs/LCD to work. Endpoint
pairing can depend on Mixxx's controller configuration; input control works without
feedback. End-to-end mapping selection and feedback routing in Mixxx remain a
manual check; offline mocks verify message generation and controller behavior.
Use another supported sound card for Mixxx until native/CoreAudio integration is
finished.
