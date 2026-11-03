# Native Instruments KORE 1 on macOS

An open-source controller bridge and experimental native USB audio transport for
the **original KORE 1** (`17cc:4711`) on Apple Silicon macOS.

**Current integration:** a shared native USB MIDI/audio executable and a CoreAudio
output HAL plug-in now build from the same standalone
[kore1-final-bridge.c](kore1-final-bridge.c). Explicit commands install the system
bundle and start the bridge at login. The bridge runs CoreMIDI, controller feedback
and RX-matched audio concurrently through one USB interface owner.

The new integration has passed offline callback, queue, controller, HAL ABI and
rate-matching tests. Its live system/hardware validation must be checked against
[the validation record](docs/NATIVE-INTEGRATION-VALIDATION.md). The device exposes Main on channels 1/2 and Headphones on 3/4. The login bridge
uses separate software gains: Main 0.2 (-14 dB), Headphones 1 (0 dB);
the standalone CLI retains its quiet .00025 default. Reported output latency is
about 171ms. Application playback gain is separate from the diagnostic tone cap.
KORE audio input and full-level audio
qualification are not implemented.

The stereo headphone path has now been recorded with independent 440/660Hz
left/right signals and a dual-channel window. Both channels were detected with
continuous capture. After a Mac restart, the installed four-channel Main path
passed a ten-second loopback test; USB scheduling restarts remain unresolved; see [stereo results and remaining measurement limits](docs/STEREO-OUTPUT-RESULTS.md).

## Build, install and run

```sh
make development-package
make check audio-check native-check coreaudio-check
./build/kore1-final-bridge --install-system
./build/kore1-final-bridge --system-status
./build/kore1-final-bridge --run --coreaudio-pcm --continuous --mixxx-feedback
```

Installation requires a macOS administrator dialog and restarts CoreAudio. It
preserves existing bundles and does not change audio defaults. Optional automatic
startup: `./build/kore1-final-bridge --install-login`. Run one KORE process at a time.
See [installation, removal, standalone compilation and Mixxx setup](docs/DEVELOPMENT-PACKAGE.md).

The complete local archive is `build/KORE1-macOS-development.zip`. Current Mixxx
files are [mixxx/KORE-1.midi.xml](mixxx/KORE-1.midi.xml) and
[mixxx/KORE-1.js](mixxx/KORE-1.js). Use `./run-midi.sh` for continuous MIDI alone.

## Build

Requires macOS 12 or later, a current Xcode Command Line Tools SDK including
IOUSBHost.framework, and a C/Objective-C compiler. The libusb/controller builds
also require `pkg-config` and `libusb-1.0` (for example, installed with Homebrew).

```sh
make all audio native-probe
make check audio-check native-check
```

Builds and checks do **not** open hardware. Tests include deterministic PCM vectors,
channel isolation and tone timing, native transaction layouts, callback ownership,
late cancellation, bounded transfer pools, and synthetic capture analysis. Native
C/Objective-C tests use AddressSanitizer and UndefinedBehaviorSanitizer.

## Controller bridge

```sh
./build/kore-midi-v10 --mixxx-feedback
```

Creates MIDI source `KORE 1 Bridge` and opt-in destination `KORE 1 Feedback`.
Default mappings: relative CC20–27 for encoders, notes36–67 for buttons,
CC40–42 for analog controls. Current Mixxx mappings are in `mixxx/`; the root `KORE-1-Mixxx-v10.*` files are preserved for compatibility.
See [architecture](ARCHITECTURE.md), [protocol](PROTOCOL.md) and [testing](TESTING.md).

## Native audio diagnostics

The isolated native probe and legacy libusb tools require exclusive interface 0
access. Run only one KORE process at a time. Use the final bridge above for
simultaneous MIDI/audio; the probe remains a separate diagnostic tool.

Hardware-free option validation and read-only registry discovery:

```sh
./build/kore1-native-probe --dry-run --stream-silence
./build/kore1-native-probe --discover
```

With speakers/headphones disconnected and output gain minimized, this explicit
hardware command runs bounded encoded silence:

```sh
./build/kore1-native-probe --run --stream-silence --duration-ms 3000
```

Tone output is opt-in, single-channel, capped and limited to one second inside a
three-second session. Follow the [recorded playback notes](docs/TONE-PLAYBACK-RESULTS.md)
and use a verified line-input capture connection before enabling it. The native
transport is experimental: successful USB requests alone do not establish analog
playback, and current recordings do not establish production audio quality.

## Documentation and source layout

- [Native transport and discovery](docs/NATIVE-TRANSPORT.md)
- [Continuous streaming tests and scheduling limits](docs/NATIVE-STREAM-RESULTS.md)
- [Analog playback measurements and capture limitations](docs/TONE-PLAYBACK-RESULTS.md)
- [Linux parity, legacy submission failure and investigation](docs/AUDIO-VALIDATION.md)
- [Gap-free capture and clock measurements](docs/CAPTURE-CONTINUITY.md)
- [Local CoreAudio output prototype](docs/COREAUDIO-PROTOTYPE.md)
- [Adaptive rate matching and its limits](docs/RATE-MATCHING.md)
- [Next development steps](docs/NEXT-STEPS.md)
- `audio/`: shared framing, native IOUSBHost transport, discovery and capped tone generator.
- `src/`: controller bridge; `tests/` and `automation/`: offline checks and explicit hardware tools.
- `kore1-bridge-audio.c`: tested libusb diagnostic bridge; native transport is the preferred audio development path.
- `kore1-bridge.c`, `build.sh`, `run-midi.sh`: retained compatibility bridge/build entry points.
- [Historical experiment archive](experimental/legacy-libusb/README.md): preserved snapshots, not recommended hardware commands.
- `build/`, `artifacts/`, captures, logs and the local Actions runner are ignored.

GPL licensing and attribution are retained; see [COPYING](COPYING) and
[NOTICE.md](NOTICE.md). No proprietary firmware is included.

Headphone hollow-sound investigation: low-level line captures passed stereo and
short-chirp checks; actual headset/adapter behavior is still pending. See
[measurements and limits](docs/HEADPHONE-DIAGNOSTICS.md). Independent cueing and
installed playback gains are unchanged by these diagnostic additions.

The current Mixxx preset uses the eight encoders for selected-deck filters/FX,
the jogwheel for library browsing, and the real Play/Stop buttons. Backlight is
not a transport control. See [the FX/browser mapping guide](docs/MIXXX-FX-MAPPING.md)
for the layout, user-local bridge installation and verification limits.
