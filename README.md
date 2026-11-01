# KORE 1 – macOS MIDI + experimental stereo output (v1.0)

**Not a verified final audio driver.** MIDI is carried forward from hardware-tested versions. USB audio remains unverified; previous hardware tests produced only whining with nonzero CAIAQ checkbytes. This package is an integrated prototype, not a native CoreAudio HAL driver.

## Architecture

Mixxx audio -> **BlackHole 2ch** virtual output -> macOS default input -> `capture-blackhole` (CoreAudio AudioQueue) -> `/tmp/kore1-pcm.fifo` (48kHz stereo float32) -> `kore1-bridge` (24-bit S24_3BE CAIAQ framing) -> libusb isochronous OUT endpoint 0x06. CoreMIDI controller and feedback remain in `kore1-bridge`.

BlackHole is an **external dependency**; install separately. The input capture helper requires selecting BlackHole 2ch as the macOS default **input**, and Mixxx must output to BlackHole 2ch. No automatic sample-rate conversion is provided: configure BlackHole and Mixxx at 48 kHz, stereo. Do not select KORE as a macOS audio device: it is not registered as one.

## Build on Apple Silicon

```sh
brew install libusb pkg-config
./build.sh
```

## MIDI only (recommended)

```sh
./run-midi.sh
```

## Experimental audio (only if you accept the risk)

1. Install BlackHole 2ch, set sample rate to 48 kHz in Audio MIDI Setup.
2. Select BlackHole 2ch as macOS **input** and as Mixxx **output**. Ensure mic permission is granted to the Terminal app.
3. Start `./run-audio.sh` in terminal 1.
4. Start `./capture-blackhole` in terminal 2.
5. Start Mixxx at low volume. If you hear whining, immediately stop playback and the processes.

**CAUTION:** unplug headphones or keep volume at minimum for initial tests. Experimental packet framing can create unexpected noise. This is not safe to use for a live DJ set. Audio is capped to -12 dBFS in the bridge.

## Limitations

- Audio has **not** been verified on KORE 1 hardware. The original Linux driver may require RX-triggered scheduling, whereas this implementation uses fixed 176-byte OUT packets, independent of 2-byte IN responses.
- No direct CoreAudio HAL device, installer, daemon or automatic startup.
- `capture-blackhole` reads default input, not a selected UID; verify it is BlackHole, not a microphone.
- PCM stream 0 is assumed to map to headphones; actual output channel mapping is unverified.
- FIFO is unframed raw float32 little-endian. Overflow drops samples, and underrun inserts silence. No adaptive clock drift correction or resampling.
- The two processes must start in the stated order. Only one instance each.
- MIDI mappings for Mixxx are not included; use your existing Mixxx XML/JS mapping.
- macOS compilation cannot be performed in this Linux environment; build on your Mac.

## Reference

Linux `sound/usb/caiaq/audio.c` and `device.c` are the protocol reference. The Linux CAIAQ implementation uses S24_3BE and inserts checkbytes in alignment-2 framing. Earlier bridge source is carried forward; audit its licensing before redistribution.
