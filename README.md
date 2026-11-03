# Native Instruments KORE 1 on macOS

Native USB MIDI/audio bridge for the original **KORE 1** (`17cc:4711`) on Apple
Silicon macOS. Current status: **2026-11-03**. Audio remains experimental.

[Current status and evidence](docs/STATUS.md) · [Setup](README-SETUP.md) ·
[Mixxx controls](docs/MIXXX-FX-MAPPING.md) · [Tests](TESTING.md) ·
[Architecture](ARCHITECTURE.md) · [Protocol](PROTOCOL.md)

## What works

- One native IOUSBHost process handles USB audio, CoreMIDI and LED/LCD feedback.
- Installed CoreAudio output: **KORE 1 Experimental Output**, fixed **48kHz**,
  four float channels: **1/2 Main L/R**, **3/4 Headphones L/R**.
- Independent Mixxx main output and headphone cueing. Current login gains are
  Main **0.2** (-14dB) and Headphones **1** (0dB); physical knobs still apply.
- The **KORE 1 FX + Browser** preset controls filters/effects with eight encoders,
  browses tracks with the jogwheel, and loads Deck A/B with left/right arrows.
  Physical Play/Stop are mapped correctly; Backlight cannot start playback.
- Active/inactive button feedback, fixed encoder/jog illumination, and LCD deck,
  Play/Stop, BPM and loaded-track status are implemented and installed.
- The user confirmed Main/headphone playback and the corrected transport/browser
  mapping. Bounded Main and headphone line captures passed defined checks.

## Open limits

USB deadline failures and service restarts remain unresolved. Short successful
captures do not establish long-run clock lock, production sound quality or
frequency response. Reported output latency is about **171ms**.

KORE audio input is not implemented. MAYA44 recording is an external measurement
path. A reported hollow headset sound may involve its four-pole plug/adapter;
that physical diagnosis remains unconfirmed. Some measurement attempts clipped
or had capture gaps and are explicitly excluded from quality conclusions.

Real LCD title/artist display is **deferred by user choice**: Mixxx 2.5.6 lacks
the Player metadata API used by the mapping. We remain on stable Mixxx 2.5;
no development Mixxx version or library migration was installed.

## Build and setup

Requires macOS 12+, a current Xcode Command Line Tools SDK with IOUSBHost,
Python 3 and Node for checks. Compatibility/libusb tools also need pkg-config
and libusb-1.0. Optional echo diagnostics need NumPy.

```sh
make development-package
make check audio-check native-check coreaudio-check
# Optional, hardware-free chirp-analyzer checks:
make echo-check
```

Builds/checks do not install drivers or open KORE/MAYA44. The generated
[kore1-final-bridge.c](kore1-final-bridge.c) builds both the native executable
(Objective-C mode) and HAL bundle (C mode). Current Mixxx files are
[mixxx/KORE-1.midi.xml](mixxx/KORE-1.midi.xml) and [mixxx/KORE-1.js](mixxx/KORE-1.js).

Fresh installation requires explicit system and login commands; use the
[setup guide](README-SETUP.md). On the development Mac, the HAL is system-wide
and the bridge runs from the user Library folder. Update that deployment with
`python3 automation/install_user_bridge.py --run`; `--defer-restart` stages an
atomic update without stopping current audio. Do not start another USB owner.

The local archive is `build/KORE1-macOS-development.zip`. It contains compiled
components, standalone source, current mapping, guides and a SHA256 manifest.
It is architecture-specific and is not a signed/notarized production release.

## Documentation and source

- [Documentation index](docs/INDEX.md): current guides versus historical evidence.
- [System/development package](docs/DEVELOPMENT-PACKAGE.md): installation, removal,
  manual commands and full build details.
- [Stereo measurements](docs/STEREO-OUTPUT-RESULTS.md) and
  [headphone diagnostics](docs/HEADPHONE-DIAGNOSTICS.md).
- [Remaining work](docs/NEXT-STEPS.md) and [changelog](CHANGELOG.md).
- `audio/`: native USB/control engine and framing; `coreaudio/`: HAL, PCM queues,
  shared ASRC and measurement clients; `mixxx/`: active mapping.
- `src/kore-midi-v10.c`: maintained controller decoder and compatibility bridge;
  generated `audio/controller_engine.h` embeds it in the native owner.
- `tests/` and `automation/`: offline checks, packaging and opt-in measurement tools.
- Root v10 mapping files and `experimental/legacy-libusb/` preserve earlier work.
  They are not the active preset. Build outputs, WAVs and logs stay ignored.

GPL licensing/provenance: [COPYING](COPYING), [NOTICE.md](NOTICE.md).
No proprietary firmware is included; no vendor endorsement is implied.
