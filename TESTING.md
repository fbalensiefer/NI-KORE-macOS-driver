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
Node-based Mixxx behavior and syntax checks, final CLI validation and generation checks. `audio-check` and `native-check` compile and run
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
share interface 0. The final native bridge combines MIDI/audio in one owner. With speakers/headphones disconnected and output
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
or register a system audio device. The original offline-only instruction was superseded by an explicit system-install request; see [current integration validation](docs/NATIVE-INTEGRATION-VALIDATION.md).

## Standalone bridge and Mixxx package

`make development-package` compiles the generated C bridge and all native/CoreAudio
components, then assembles a local zip. `make check` verifies source generation,
Mixxx XML cue press/release routes, and mocked encoder/button/feedback/shutdown
behavior. The package is architecture-specific and requires Homebrew libusb for
the libusb executables. System installation remains an explicit command, never a build side effect.
Mixxx application routing needs a manual UI check. See
[installation and live system testing](docs/DEVELOPMENT-PACKAGE.md).

## Explicit installed CoreAudio test

```sh
./build/kore1-final-bridge --install-system
./build/kore1-final-bridge --system-status
./build/kore-coreaudio-system-test --run
```

The last command renders into the virtual output and validates the real HAL
property PCM path with no USB access. Run it before starting the bridge's PCM
consumer, since the lease is exclusive. Tools running in a sandbox that blocks
`com.apple.audio.audiohald` cannot query or test the installed device.

Shared hardware silence validation:

```sh
./build/kore1-final-bridge --run --stream-silence --mixxx-feedback --duration-ms 3000
```

This also covers first-connection configuration selection, AUTO_MSG reports,
CoreMIDI endpoint creation and feedback initialization. Native MIDI unit tests
replace only final CoreMIDI delivery with a recorder and exercise the real bulk
callbacks, controller decoding, idle timeout rearm, single receive-buffer owner,
independent feedback queue and cancellation. CoreAudio tests cover box/device
publication and required model metadata as well as PCM/clock properties.

Full installed-path test (requires the authorized cable/gain setup):

```sh
python3 automation/coreaudio_loopback.py --run-tone
```

It starts timestamp-accounted capture before the supervised bridge, renders one
second of capped tone through the virtual device, sends a concurrent LCD update,
stops the USB owner, and analyzes the complete ten-second recording.
