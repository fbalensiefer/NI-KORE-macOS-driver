# Build and verification

## Offline (no device access)

On macOS 12+ with a current CLT SDK, libusb and pkg-config installed:

```sh
make all audio native-probe hal-capture coreaudio-prototype
make check audio-check native-check coreaudio-check
# Equivalent convenience script:
bash automation/local_test.sh
```

`make check` runs Python repository/patch checks, synthetic capture analysis and
an optional Node syntax check. `audio-check` and `native-check` compile and run
actual C/Objective-C framing and callback tests with ASan/UBSan. The suite covers
PCM byte order, channel isolation, variable packet lengths, explicit native
transaction offsets, pool ownership, errors, deadlines and delayed cancellation.
No build/check target runs hardware. Historical source assertions are kept
separate from the current production-code tests.

## Read-only discovery

```sh
./build/kore1-native-probe --discover
```

Reads registry identity and link speed without opening a USB user client.

## Explicit hardware tests

Use one KORE process at a time. Native audio and the libusb MIDI bridge cannot
currently share interface 0. With speakers/headphones disconnected and output
gain minimized, the native silence command is bounded to three seconds:

```sh
./build/kore1-native-probe --run --stream-silence --duration-ms 3000
```

Tone and recording tests require a confirmed KORE-to-line-input cable setup,
low gain, explicit authorization and microphone permission for the launching
application. The helper now uses direct HAL capture by default, selecting MAYA44 by UID/name
and validating sample counts and timestamps. AVFoundation is a comparison backend:

```sh
./build/kore-hal-capture --list
python3 automation/native_loopback.py --run-tone --channel 3
```

This is opt-in hardware playback: one second of capped tone inside a three-second
native session, recorded on all four MAYA44 channels. The direct capture removes the observed missing samples; analog noise and clock
calibration still limit audio-quality conclusions. See
[measured results](docs/TONE-PLAYBACK-RESULTS.md).

Controller checks use `build/kore-midi-v10`: `--led-list` is offline;
`--led-test`, `--lcd-test` and normal MIDI operation access hardware. Preserve
known mappings and verify feedback destination routing separately from MIDI input.
Do not run archived unbounded hardware scripts as routine tests.


`coreaudio-check` loads the local signed bundle through CFPlugIn and tests its
actual ABI, property protocol, threaded PCM queue and packer. It does not install
or register a system audio device. Per user instruction the prototype remains
local and uninstalled; see [CoreAudio status](docs/COREAUDIO-PROTOTYPE.md).
