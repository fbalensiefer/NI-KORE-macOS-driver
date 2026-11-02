# KORE 1 native MIDI/audio bridge and CoreAudio integration

`kore1-final-bridge.c` contains the native USB transport, controller decoding and
filters, CoreMIDI source and feedback destination, LED/LCD implementation,
CoreAudio PCM client, HAL output implementation, and explicit installation commands.
It is generated from maintained modules. No project-local includes are needed to
compile it outside this repository. Commands prefixed with `./build/` refer to
the source checkout; omit that prefix when running inside the extracted zip.

Apple's public IOUSBHost interface is Objective-C. The requested `.c` filename is
retained; compile the executable with `-x objective-c`. The same file builds the
HAL bundle as C with `-DKORE_HAL_PLUGIN`. The HAL plug-in must live inside the
CoreAudio service, while USB/MIDI runs in a user process. One source builds both;
a single process cannot replace the required CoreAudio plug-in host boundary.

## Build

```sh
make bridge coreaudio-prototype
make check native-check coreaudio-check
make development-package
```

Requires macOS 12+, Xcode Command Line Tools, Python 3 and Node for checks. The
legacy diagnostic binaries included by `development-package` also require
Homebrew `pkg-config` and `libusb`. The final native bridge itself has no libusb
dependency. Binaries match the build machine's architecture (currently arm64).
The zip is `build/KORE1-macOS-development.zip`; it includes both built components,
tools, Mixxx mappings, standalone source, licensing and a SHA-256 manifest.
Builds/checks do not install a plug-in or access USB/audio hardware.

Standalone compilation from the generated file:

```sh
cc -std=c11 -Wall -Wextra -Werror -O2 -x objective-c -fobjc-arc -fblocks \
  -mmacosx-version-min=12.0 -o kore1-final-bridge kore1-final-bridge.c \
  -framework Foundation -framework CoreAudio -framework CoreMIDI \
  -framework IOKit -framework IOUSBHost
mkdir -p KoreAudio.driver/Contents/MacOS
cc -std=c11 -Wall -Wextra -Werror -O2 -fPIC -bundle -DKORE_HAL_PLUGIN \
  -o KoreAudio.driver/Contents/MacOS/KoreAudio kore1-final-bridge.c \
  -framework CoreAudio -framework CoreFoundation
./kore1-final-bridge --emit-driver-plist > KoreAudio.driver/Contents/Info.plist
codesign --force --sign - KoreAudio.driver
```

Maintain `audio/bridge_main.m`, `audio/native_probe.m`, `coreaudio/`,
`src/kore-midi-v10.c`, and the generator/installer modules. `make` regenerates
controller/resource headers and the standalone file. The preserved standalone
legacy controller still builds independently for comparison.

## Native MIDI and audio together

One IOUSBHost interface owner receives controller bulk reports and schedules the
RX-matched ISO audio stream. ISO callbacks, MIDI decoding, LED/LCD writes, and
CoreAudio prefetch each have their own serial queue. Bounded controller queues
fail explicitly instead of growing without limit. Idle bulk timeouts rearm; device
and transfer failures stop the stream. Shutdown cancels timers, drains queues,
aborts pipes and retains buffers until outstanding callbacks finish.

Dry-run is the default, including for `--coreaudio-pcm`:

```sh
./build/kore1-final-bridge --dry-run --coreaudio-pcm --mixxx-feedback
./build/kore1-final-bridge --run --stream-silence --mixxx-feedback --duration-ms 3000
./build/kore1-final-bridge --run --coreaudio-pcm --mixxx-feedback --duration-ms 3000
```

Default sessions are bounded to three seconds and retain the 32 IN / 64 OUT
diagnostic pools with 32ms OUT lead. Continuous operation uses bounded 128 IN /
256 OUT pools and 128ms OUT lead to tolerate callback scheduling stalls. The
HAL reports 6,144 frames of output latency plus 2,048 frames of safety offset
(about 171ms total at 48kHz). This favors scheduling tolerance over DJ latency.

`--continuous` explicitly requests
ongoing operation, stopped by Ctrl+C/SIGTERM. It cannot be combined with a bounded
duration or diagnostic tone. To run the actual output bridge after installation:

```sh
./build/kore1-final-bridge --run --coreaudio-pcm --continuous --mixxx-feedback
```

`./run-midi.sh` starts continuous MIDI only. `./run-audio.sh` remains a bounded
native silence diagnostic. Do not run any other KORE controller/probe process at
the same time as the final bridge: they would contend for interface 0.

The CoreAudio device exposes four interleaved float output channels at fixed
48kHz: **1/2 Main Left/Right**, **3/4 Headphones Left/Right**. Select Main 1/2
and Headphones 3/4 separately in Mixxx Sound Hardware. The current user's Mixxx
configuration is backed up before adding these routes while the app is closed.

The standalone CLI keeps its quiet .00025 gain default. `--pcm-gain 0..1` sets
both pairs; `--main-gain` and `--headphone-gain` override their respective pair,
regardless of argument order. The login installer explicitly selects Main **0.2**
(-13.98 dB) and Headphones **1** (0 dB). These are software gains, not calibrated
analog volume settings. Main 0.2 was measured through KORE Main R -> MAYA44 CH1,
L -> CH2 at roughly -8 dBFS with no capture clipping. Headphone unity gain is
verified digitally; its new analog level has not been remeasured with this Main
cabling. Keep the hardware headphone volume low when reconnecting headphones.

The adaptive rate matcher follows reservoir occupancy and interpolates all four
channels at the same phase, without changing USB frame order. Reported latency
remains about 171ms. Audio input, long-run clock qualification and full-band
resampling quality remain unfinished. Earlier USB deadline failures are unresolved.

## Install the system HAL plug-in

Installation is explicit, requires the Mac administrator password in a native
macOS dialog, and briefly interrupts audio by restarting CoreAudio:

```sh
./build/kore1-final-bridge --install-system
./build/kore1-final-bridge --system-status
```

The installer verifies the bundle signature and identifier, stages the bundle,
preserves previous copies under `/Library/Application Support/KORE1/Backups`,
installs root-owned files at:

- `/Library/Audio/Plug-Ins/HAL/KoreAudio.driver`
- `/Library/Application Support/KORE1/kore1-final-bridge`
- `/Library/Application Support/KORE1/KoreAudio.driver` (installation source copy)

It does not change the default output/system audio selections, disable SIP, or
install a kernel extension. Select **KORE 1 Experimental Output** explicitly in
an audio application. The virtual output can enumerate while the KORE is absent;
actual playback requires the native bridge and connected hardware.

To start the bridge automatically in the current user's GUI login session:

```sh
./build/kore1-final-bridge --install-login
```

This starts `org.nikore.bridge` immediately and writes
`~/Library/LaunchAgents/org.nikore.bridge.plist`. It retries failed USB/device
opens at a throttled interval. Logs are `~/Library/Logs/KORE1-bridge*.log`.
Stop this agent before manual USB or consumer-lease tests:

```sh
./build/kore1-final-bridge --uninstall-login
```

To remove the system output device (bundle is preserved as a backup):

```sh
./build/kore1-final-bridge --uninstall-login
./build/kore1-final-bridge --uninstall-system
```

`--uninstall-system` restarts CoreAudio and leaves the support directory/backups.
To restore an older version, copy its saved bundle into a directory adjacent to
its matching bridge binary and run that binary's `--install-system` command.

## System verification

After installation and before launching the USB consumer, run:

```sh
./build/kore-coreaudio-system-test --run
```

This renders a bounded 440Hz signal to the virtual device, consumes PCM through
real HAL custom properties, checks channel isolation/sample count, releases the
consumer lease and confirms default devices are unchanged. It opens no USB
interface. It must not run concurrently with the bridge's PCM consumer.

A full system loopback can be repeated with
`python3 automation/coreaudio_loopback.py --run-tone`; this supervises the
continuous bridge and stops it after the bounded render, with ten seconds of
MAYA44 capture and a concurrent LCD feedback update.

Hardware validation additionally requires the connected KORE/MAYA44 low-gain
loopback setup described in TESTING.md. Offline success does not establish system
HAL loading, sustained USB playback, or analog quality. See the dated validation
record for the most recent actual results.

## Mixxx

Copy both `mixxx/KORE-1.midi.xml` and `mixxx/KORE-1.js` to Mixxx's user controller
mapping directory (open from Preferences → Controllers). Reload Mixxx, enable
input `KORE 1 Bridge`, and choose the KORE 1 mapping. These are controller presets,
not `.mid` music sequences. Existing root v10 mappings remain available.

| Incoming MIDI, channel 1 | Action |
| --- | --- |
| Relative CC20 / CC21 | Deck A / B volume |
| Relative CC22 / CC23 | Deck A / B pregain |
| Relative CC24 / CC25 | Deck A / B pitch rate |
| Relative CC26 / CC27 | Crossfader / master gain |
| Notes36 / 38 | Select A / B for LCD feedback |
| Notes37 / 40 | Toggle A / B playback |
| Notes39 / 41, including Note Off | A / B momentary cue |

Relative values 1–63 increment, 65–127 decrement; zero is idle. Other buttons and
analog CCs are available for custom mappings. Metadata is optional if
`engine.getPlayer` is unavailable. See [Mixxx scripting](https://github.com/mixxxdj/mixxx/wiki/midi-scripting)
and the [official API declarations](https://github.com/mixxxdj/mixxx/blob/main/res/controllers/engine-api.d.ts).

Feedback must route to the separate `KORE 1 Feedback` MIDI destination. Pairing
these endpoints depends on Mixxx's controller configuration. End-to-end Mixxx UI
routing remains a manual check; offline tests cover generated messages and mapping
behavior. Selecting the new output device for Mixxx audio also requires the
installed HAL bundle and running bridge.

## Stereo channel verification

`python3 automation/coreaudio_loopback.py --run-tone --stereo-test --use-login-bridge`
checks Main 1/2 on the running service without opening another USB owner.
Add `--headphones` to test 3/4 after moving the loopback cable to that output. It emits separate
440Hz left / 660Hz right / dual windows and records all MAYA44 inputs. See
[stereo measurements](STEREO-OUTPUT-RESULTS.md). Without `--run-tone`, this helper
stays hardware-free. An explicit gain option applies only to a separately supervised
bridge after stopping the login agent; it never changes a running service's gain.

Mixxx XML route names and zero-based channel indices follow the upstream
[AudioOutput serializer](https://github.com/mixxxdj/mixxx/blob/main/src/soundio/soundmanagerutil.cpp).
