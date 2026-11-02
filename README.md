# Native Instruments KORE 1 on macOS — MIDI bridge and experimental USB audio

> **Status (2 November 2026): MIDI/controller functionality has been tested; USB audio is NOT working as a verified analog PCM output.** This is a reverse-engineering research project, not a production audio driver or CoreAudio device. Do not use it for live performance. Start with speakers/headphones disconnected or at minimum gain.

## Purpose

Bring the legacy Native Instruments KORE 1 controller (USB VID `17cc`, PID `4711`) to modern macOS, including Apple Silicon. The bridge uses **libusb** for the proprietary CAIAQ USB protocol and **CoreMIDI** for virtual MIDI. The longer-term objective is reliable analog audio output and, eventually, an appropriate CoreAudio integration. The latter does **not** exist today.

## Current capabilities

- **MIDI/controller:** virtual CoreMIDI source, encoders/buttons/analog input, experimental Mixxx feedback, LEDs and LCD. Refer to the existing source and mappings for exact option support.
- **Audio control path:** device-info query, accepted `AUDIO_PARAMS` handshake, and isochronous IN/OUT diagnostics.
- **Experimental audio path:** a CoreAudio/BlackHole capture helper and float32 FIFO-to-24-bit CAIAQ encoder exist, but **audible output is not valid PCM**. These tools are research prototypes only.

## Architecture

`KORE 1 ↔ USB bulk endpoints 0x01/0x81 ↔ libusb bridge ↔ CoreMIDI ↔ DJ software`

Experimental audio: `Mixxx → BlackHole 2ch (48 kHz) → capture-blackhole → /tmp/kore1-pcm.fifo (stereo float32 LE) → bridge → CAIAQ 24-bit samples/checkbytes → USB isochronous OUT 0x06 → KORE 1`. USB isochronous IN is `0x82`. BlackHole is a separate dependency. This is **not** a macOS audio output device.

## Build

On macOS with Xcode Command Line Tools and Homebrew:

```sh
brew install libusb pkg-config
./build.sh
```

The repository also has a MIDI-only Makefile target (`make`) and tests (`make check`). The integrated bridge build uses `build.sh`; inspect `./kore1-bridge --help` or source options before running experimental audio. Prefer `./run-midi.sh` for normal controller use.

**Source provenance:** The published `kore1-bridge.c` may lag behind the hardware-tested local working tree. This documentation package intentionally does not overwrite that source. See `docs/REPOSITORY-SYNC.md` before merging the current Mac file. The `experimental/` ZIP is a *historical v0.23 snapshot*, not a verified replacement for the newest local file.

## Findings and research status

Read **[docs/AUDIO-FINDINGS.md](docs/AUDIO-FINDINGS.md)** for measured USB descriptors, firmware handshake, framing, test outcomes, and what is **not** established. Read **[docs/PROTOCOL.md](docs/PROTOCOL.md)** for the Linux CAIAQ reference and **[docs/NEXT-STEPS.md](docs/NEXT-STEPS.md)** for reproducible, conservative follow-up work.

## Limitations and safety

- No working analog PCM playback; test tones have produced whining/high-frequency artifacts.
- No native CoreAudio HAL plugin, installer, system audio device, automatic clock-drift correction, or proven headphone routing.
- The FIFO has no framing, resampling, or robust synchronization; underruns insert silence.
- macOS/Apple Silicon compilation and hardware testing must be performed on the actual Mac; CI or static review is not proof of audio function.
- Some diagnostic flags generate non-Linux-like traffic. Never assume a successful USB transfer means that the DAC accepted PCM.

## References and licensing

Primary behavioral references: [Linux CAIAQ audio.c](https://github.com/torvalds/linux/blob/master/sound/usb/caiaq/audio.c) and [device.c](https://github.com/torvalds/linux/blob/master/sound/usb/caiaq/device.c). Respect the licenses of any adapted third-party source; review original attribution before redistributing code. This documentation distinguishes observed measurements from hypotheses.

## Validated audio diagnostic target

Build and run hardware-free checks with `make audio check audio-check`. The new
`build/kore1-bridge-audio` preserves the existing controller implementation and
adds bounded fixed-96 silence, validated PCM/ISO layouts and failure diagnostics.
See [protocol findings and safe hardware commands](docs/AUDIO-VALIDATION.md).
Hardware playback and the cause of intermittent IOKit submission failures remain
unverified. No hardware test is started by the build or checks.
