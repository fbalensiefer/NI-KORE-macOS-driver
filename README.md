# Native Instruments KORE 1 on macOS

An open-source controller bridge and experimental native USB audio transport for
the **original KORE 1** (`17cc:4711`) on Apple Silicon macOS.

**Current milestone — November 2, 2026:** repeated native silence streaming and
recorded low-level analog tone playback through the KORE headphone connection.
This is not yet a general-purpose macOS audio driver: the CoreAudio output plug-in and PCM feed are local prototypes, not installed
or verified in the system HAL. KORE audio-input capture is not implemented.

| Function | Status |
| --- | --- |
| Controller → CoreMIDI | Existing encoder, button and analog mappings preserved |
| LED/LCD feedback | Existing opt-in controller implementation preserved |
| Native USB audio | RX-driven variable lengths, including trailing-zero packets |
| Bounded silence | Repeated three-second hardware runs; stop/reopen tested |
| Analog playback | Near-440 Hz recorded from logical channels 3/4 via MAYA44 |
| Capture / clock measurements | Gap-free direct HAL capture verified; short tests suggest about +200 ppm KORE/host offset |
| CoreAudio / PCM feed | Local output plug-in and bounded bridge prototype; real host testing pending |
| KORE audio input | Not implemented |

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
CC40–42 for analog controls. Mixxx mappings are the root `KORE-1-Mixxx-v10.*` files.
See [architecture](ARCHITECTURE.md), [protocol](PROTOCOL.md) and [testing](TESTING.md).

## Native audio diagnostics

The native transport and libusb controller bridge both require exclusive access
to interface 0. Run only one KORE process at a time; simultaneous MIDI/audio via
this native prototype is not implemented.

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
