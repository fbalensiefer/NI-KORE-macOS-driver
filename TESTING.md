# Build and verification — 2026-11-03

Current claims and deployment: [STATUS](docs/STATUS.md). This guide separates
software checks, explicitly requested hardware tests and user observations.
No hardware testing is implied by a documentation update.

## Offline checks

```sh
make development-package
make check audio-check native-check coreaudio-check
# Optional NumPy diagnostics:
make echo-check
```

Requirements: macOS 12+, current CLT SDK/IOUSBHost, Python 3, Node, pkg-config and
libusb. `echo-check` additionally requires NumPy. The convenience command
`bash automation/local_test.sh` runs the primary offline suite.

| Target | What it checks |
|---|---|
| check | Generated-source consistency, CLI guards, repository/archive integrity, Mixxx XML/JS and synthetic analysis |
| audio-check | CAIAQ byte/layout vectors and legacy async callback/CLI guards |
| native-check | Real native callbacks with fake transport: buffer ownership, ordering, deadlines, cancellation, MIDI and wheel decoding |
| coreaudio-check | Local HAL bundle/factory ABI, properties, ring/PCM packing, four-channel ASRC simulation, capture accounting and stereo negatives |
| echo-check | Bounded renderer modes/levels, chirp timing, injected delayed replica, clipping and metadata/gap refusal |

C/Objective-C checks use ASan/UBSan where specified in Makefile. No check target
installs a driver, opens KORE/MAYA44 or changes audio defaults. Generated files
must match maintained inputs; archived files must match their recorded hashes.
For documentation-only edits, repository/link checks and package verification
are sufficient unless a new code change or failure justifies broader testing.

## Controller regression coverage

- Real IO decoding: note press/release, timeout rearm and independent feedback queue.
- Wheel CC28: initial baseline, wrap, reverse, split delta and ambiguous jump.
- Play note50 / Stop51; Backlight37 and touch notes do not start playback.
- Left/right load stopped A/B regardless of selected deck; cue release returns
  to the original deck after selection changes.
- Bounded FX values, bright/dim feedback for external control changes, fixed ring
  illumination, duplicate suppression and connection cleanup.
- Optional Player metadata, unload clearing and ASCII transliteration. On Mixxx
  2.5, the tested fallback is loaded status, not real track names.

The user confirmed the physical transport/browser mapping. Software tests do
not independently calibrate every front-panel position or LED brightness.

## Read-only discovery

```sh
./build/kore1-native-probe --discover
./build/kore1-final-bridge --system-status
./build/kore-hal-capture --list
```

Registry discovery does not open a USB user client. CoreAudio queries require
access to the audio service; sandbox rejection or a host initialization timeout
is not an audio-framing result.

## Explicit hardware tests

Use only a currently confirmed cable/gain setup and the already authorized scope.
Do not run isolated probes/libusb while the shared login bridge owns interface 0.
MAYA44 capture needs microphone permission and a verified line input. These tools
are opt-in; do not treat archived unbounded commands as routine checks.

With the login bridge stopped and other KORE owners absent, bounded silence is:

```sh
./build/kore1-native-probe --run --stream-silence --duration-ms 3000
```

A standalone native oscillator remains capped to 0.001 and limited to its bounded
session. That cap is distinct from application PCM gains, which allow 0..1.
No success from USB completion alone establishes audible playback or quality.

## Installed system path

Installation/removal commands are in [setup](README-SETUP.md), not test prerequisites
for offline checks. `kore-coreaudio-system-test --run` tests the actual installed
HAL renderer/custom-property path without USB. It requires the exclusive PCM
consumer lease, so stop the bridge consumer first and restore it afterward.
Do not run it concurrently with the login agent.

For an authorized KORE-to-MAYA44 loopback using the existing login service:

```sh
# Main 1/2 connected to MAYA44:
python3 automation/coreaudio_loopback.py --run-tone --stereo-test --use-login-bridge --signal-level .1
# Only after connecting the physical headphone output to MAYA44:
python3 automation/coreaudio_loopback.py --run-tone --stereo-test --headphones --use-login-bridge --signal-level .1
# Optional NumPy matched-chirp check with that same headphone connection:
python3 automation/coreaudio_loopback.py --run-tone --echo-test --headphones --use-login-bridge --signal-level .1
```

`--signal-level` is the generated signal level, separate from the running bridge's
Main/headphone gains and physical volume. Current helper default is 0.1; earlier
records often used 1. The renderer itself retains a level-1 default for its system
HAL test. A positive stereo test on the wrong physical output is not valid.

Without `--use-login-bridge`, the helper starts its own USB owner; first stop the
existing agent, and restore it after the test. It never changes a running service's
gain. All captures are bounded to ten seconds and preserve every MAYA44 channel.

## Acceptance limits

Require complete metadata, exact stored-frame accounting, no timestamp gaps,
no invalid callbacks and no clipping before interpreting audio results. Stereo
analysis checks independent signatures and solo/dual windows; echo analysis
searches delayed matched correlation from 5..250ms with a -40dB threshold.
These are diagnostic criteria, not production distortion, frequency-response or
clock-accuracy specifications. A single passed run does not erase prior failures.

Direct HAL capture improved the old AVFoundation gap problem but did not prevent
all later gaps. Some full-level and Mixxx ReplayGain-boosted probes clipped.
Service PID/run count and newly appended error-log bytes must stay unchanged
inside installed-agent test intervals. USB restarts outside those intervals remain
open evidence. Raw WAV/log artifacts are ignored, not uploaded to Git.

Evidence: [stereo results](docs/STEREO-OUTPUT-RESULTS.md),
[headphone investigation](docs/HEADPHONE-DIAGNOSTICS.md),
[dated initial integration](docs/NATIVE-INTEGRATION-VALIDATION.md).
