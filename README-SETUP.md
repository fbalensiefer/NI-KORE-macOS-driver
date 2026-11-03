# Setup — current native MIDI/audio integration

Current reference: [status](docs/STATUS.md), [package guide](docs/DEVELOPMENT-PACKAGE.md).
The old automated-runner handoff is preserved in
[historical runner setup](docs/history/AUTOMATED-RUNNER-SETUP.md).

## Build without touching hardware

```sh
make development-package
make check audio-check native-check coreaudio-check
```

Use macOS 12+ with a current CLT SDK including IOUSBHost, Python 3, Node,
pkg-config and libusb-1.0. Optional `make echo-check` requires NumPy.

## Fresh system installation

```sh
./build/kore1-final-bridge --install-system
./build/kore1-final-bridge --system-status
./build/kore1-final-bridge --install-login
```

The explicit system install requests administrator authorization and restarts
CoreAudio, briefly interrupting Mac audio. It preserves previous files and does
not change audio defaults. The login installer uses Main gain 0.2 and headphone
gain 1. Keep physical volume low when first connecting headphones.

If an administrator dialog is invisible, do not keep issuing concurrent installs.
The user-local bridge option below still requires the system HAL to be installed.
A previous CoreAudio initialization problem cleared after a Mac reboot; a missing
or stuck device query is not proof that USB/audio framing is correct.

## Update the current development-Mac deployment

The existing agent runs a user-local bridge alongside the system HAL:

```sh
make bridge
python3 automation/install_user_bridge.py --run
# Or replace the file now, retaining the running process until its next restart:
python3 automation/install_user_bridge.py --run --defer-restart
```

The helper preserves the existing known agent arguments and makes backups. It
requires that agent to exist; deferred replacement requires its user-local path.
Do not use an older system bridge's `--install-login` to update this deployment:
that changes the program path back to the older executable.

## Mixxx 2.5.6

Copy both `mixxx/KORE-1.js` and `mixxx/KORE-1.midi.xml` into Mixxx's user controller
folder (Preferences -> Controllers -> mapping folder). On the sandboxed Mac it is
`~/Library/Containers/org.mixxx.mixxx/Data/Library/Application Support/Mixxx/controllers`.
Select **KORE 1 FX + Browser** for input **KORE 1 Bridge**, enable it and route
feedback to **KORE 1 Feedback**. Reload the mapping after replacing files, or
restart Mixxx when convenient; do not interrupt a performance for an update.

Configure Core Audio at **48000 Hz** using **KORE 1 Experimental Output**:

| Mixxx output | Device channels |
|---|---|
| Main | 1–2 |
| Headphones | 3–4 |

[Control layout](docs/MIXXX-FX-MAPPING.md): Control/Sound select A/B, jog browses,
left/right load A/B only when stopped, Play/Stop are the physical transport,
Listen is PFL, Esc is cue. Encoders/softkeys operate selected-deck filters/FX.
Choose an FX preset and enable deck routing before expecting an empty slot to act.
Button feedback is bright/dim; encoder/jog lighting stays on. LCD title/artist is
explicitly deferred on 2.5; deck, play state, BPM and loaded state remain available.

## Manual operation and removal

Run only one KORE USB owner. Stop/remove the login agent before a manual native
probe or libusb bridge. Do not start manual PCM while the agent holds its lease.
For explicit manual application playback, include the intended gains: the CLI
otherwise keeps its very quiet 0.00025 default.

```sh
# Only after stopping the login service:
./build/kore1-final-bridge --run --coreaudio-pcm --continuous --mixxx-feedback --main-gain .2 --headphone-gain 1
```

Removal commands are `--uninstall-login` and `--uninstall-system`; the latter
requires administrator authorization and restarts CoreAudio. User-local backups
remain in `~/Library/Application Support/KORE1/Backups`, system backups in
`/Library/Application Support/KORE1/Backups`. Consult the package guide before
restoring an agent or binary so the intended path and version remain aligned.
