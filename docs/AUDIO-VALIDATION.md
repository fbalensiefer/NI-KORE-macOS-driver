# Audio validation milestone — 2026-11-02

## Build and offline verification

On macOS with Xcode command line tools, pkg-config and libusb already installed:

```sh
make audio
make check audio-check
```

The new target is `build/kore1-bridge-audio`, built from `kore1-bridge-audio.c`
with C11, warnings as errors. `audio/caiaq.h` is the shared framing and layout
implementation. `audio-check` runs production C functions and callbacks under
AddressSanitizer and UndefinedBehaviorSanitizer. No USB, MIDI or capture device
is opened by tests. Tests link the existing macOS frameworks and libusb.

## Audit and baseline

Inspected root C variants, src controller bridge, build scripts, documentation,
Python tests, automation, historical backup snapshots, archive inventory and Git
history through cb8c298. No AGENTS.md was present. The RX-matched source adds
zero descriptor preservation to the single176 variant; it includes the 96-byte,
FIFO, RX-paced, single-channel and sweep experiments. This feature comparison,
not the filename version, selected it as the baseline. It compiled locally with
five unused-code warnings before changes. The new target compiles without warnings.

Existing root `kore1-bridge.c` and README had user changes; there were numerous
untracked experiments and analog recordings. They were preserved. The working
MIDI/LED/LCD implementation and default build remain separate. The new bridge
copies that existing implementation; no controller algorithm was redesigned.
The existing Makefile's check target silently skipped misplaced repository tests.
Paths were corrected. The old patch test expected an obsolete seven-byte command;
it now tests its historical patch fixture and verifies that the six-byte command
is preserved. No historical patch utility or experimental evidence was removed.

## Authoritative comparison

Pinned references (read source, not secondary descriptions):

- [Linux v6.12 audio.c](https://github.com/torvalds/linux/blob/v6.12/sound/usb/caiaq/audio.c)
- [Linux v6.12 device.c](https://github.com/torvalds/linux/blob/v6.12/sound/usb/caiaq/device.c)
- [Linux v6.12 control.c](https://github.com/torvalds/linux/blob/v6.12/sound/usb/caiaq/control.c)
- [libusb v1.0.30 Darwin backend](https://github.com/libusb/libusb/blob/v1.0.30/libusb/os/darwin_usb.c)
- [libusb v1.0.30 transfer core](https://github.com/libusb/libusb/blob/v1.0.30/libusb/io.c)
- [Apple WriteIsochPipeAsync](https://developer.apple.com/documentation/iokit/iousbinterfaceinterface192/1559174-writeisochpipeasync)

Installed libusb is 1.0.30, arm64 Homebrew bottle. Linux v6.12 is a pinned
reference, not a claim about the latest kernel. It uses device.h/audio.h naming.

Verified Linux facts: mode 0 emits one PCM byte per stream, inserts checkbytes
after four PCM bytes, and resets check phase each packet. PCM is S24_3BE;
per-stream cursors persist. pcm_prepare initializes playback cursor to byte 4;
the new bridge uses that initial offset too. stream_start arms 32 IN URBs.
read_completed retains successful zero lengths, fills nonempty OUT packets and
submits only when some payload exists. Failed-IN handling in that reference uses
separate frame/outframe indices; the bridge instead skips a whole errored batch.
The KORE startup switch has no DAC-specific command; its control table lists LEDs,
not DAC volume, headphone enable or routing. This does not prove hardware state.

| Area | Earlier bridge | New target |
| --- | --- | --- |
| Handshake | `09 01 02 B0 00 01` | Preserved; ACK required |
| Framing | Inlined Linux-style loop | Shared validated packer, independent byte-vector tests |
| Initial PCM cursor | 0 | 4, matching Linux prepare |
| Fixed96 silence | Required tone or FIFO | Explicit `--audio-fixed96-silence` |
| RX mixed packets | Retried submit failures continuously | Preserve zeros; validate full layout; stop on first failure |
| Failed IN descriptor | Could be replaced by zero | Stop IN error; never infer timing from failed descriptors |
| Completion error | Could continue playing | Stop and return failure |
| Shutdown timeout | Could close context with callbacks pending | Retain live USB objects until process exit, return failure |
| Tone | Stereo default; legacy rate compensation | Selected channel, 48k phase, configurable capped amplitude |
| Duration | Open-ended | All ISO diagnostic modes default to 5s, range 100..30000ms |

Linux's ALSA prepare/trigger and capture panic tracking are not fully ported.
The new bridge is a diagnostic transport, not a completed ALSA/CoreAudio port.
It does not decode input PCM or the alignment-2 IN panic bits. Linux read_in_urb ignores capture payloads shorter than bpp; OUT filling does
not apply that threshold. The bridge only counts this comparison and does not
implement the capture decoder.

## INVALID_PARAM investigation

**Verified:** the historical mixed transfer sums lengths correctly and compacts
its buffer. For 0/176 lengths, every packet fits the reported 512-byte endpoint
capacity. libusb's transfer core does not reject zeros; Darwin copies each length
to frReqCount and calls WriteIsochPipeAsync. It translates kIOReturnBadArgument
into LIBUSB_ERROR_INVALID_PARAM. Darwin schedules at least four bus frames ahead
and advances its per-endpoint scheduling cursor even when native submission fails.

**Offline result:** all 255 nonempty 8-packet 0/176 patterns pass the new layout
checks and the production scheduler mock. An all-zero batch is skipped. Busy
slots cannot be reused; rejected submissions release ownership and stop. These
tests do not execute IOKit and cannot demonstrate native zero-packet support.

**Unresolved:** no native error trace from the historical failures is available.
Zero lengths, scheduling horizon, host-controller/IOKit constraints and endpoint
state are not distinguishable from the old error alone. It would be unsupported
to claim the root cause is fixed or that macOS rejects all zero-length packets.
Repeated failed submissions could compound a scheduling-horizon problem because
of the backend cursor behavior; this is a hypothesis, not the established cause.
The new failure log records the exact eight lengths and total and stops immediately.
Use LIBUSB_DEBUG=4 in a separately authorized hardware run to capture the native
backend error. No zero descriptors are padded or discarded to hide the problem.

## Packing arithmetic and previous recordings

96 bytes carry 36 PCM bytes per stream: six stereo frames. 176 carry 66: eleven
frames. At 8000 packets/s this means 48000 vs 88000 frames/s. This is mathematical
transport capacity, not a playback test. The observed 0/176 ratio can average
48000 frames/s; why the firmware chooses that pattern remains unverified.
The README-single176 calculation of 44 PCM bytes per stream was incorrect;
no phase compensation based on that calculation is used in the new target.

The MAYA44 phone recording established that the recording input works. It did
not demonstrate KORE output. Previous KORE silence measurements, physical output
mapping, cable configuration, headphone knob/mute state and DAC function remain
hardware questions. Successful USB completions do not establish audible output.

## Explicit modes and safe commands

Offline option validation (safe to run without hardware):

```sh
./build/kore1-bridge-audio --audio-fixed96-silence --dry-run
```

The following commands access hardware. They were **prepared, not executed**.
Run one bridge process only. For initial OUT diagnostics disconnect speakers and
headphones and set output gain to minimum. Even encoded silence cannot guarantee
physical silence if the device interprets the protocol differently.

Initialization only, no ISO IN or OUT:

```sh
./build/kore1-bridge-audio --audio-init-only
```

ISO IN monitoring only (no audio file capture, no OUT):

```sh
./build/kore1-bridge-audio --audio-init --audio-linux-urbs --audio-duration-ms 3000
```

Recommended first OUT test, fixed96 encoded silence, automatic 3s stop:

```sh
./build/kore1-bridge-audio --audio-fixed96-silence --audio-duration-ms 3000 --audio-linux-trace
```

RX-synchronized encoded silence with zeros preserved, automatic 3s stop:

```sh
LIBUSB_DEBUG=4 ./build/kore1-bridge-audio --audio-rx-matched-zero --audio-linux-urbs --audio-duration-ms 3000
```

Do not proceed to tone after an error. For a later separately authorized analog
test, `--headphone-tone --audio-out-96 --audio-tone-channel 1` selects one logical
channel, with 1s silence, 1.2s tone, then silence. Select 1..4 individually; these
are logical stream/channel indices, not verified physical jack assignments.
`--audio-max-amplitude` defaults to .00025 (about -72 dBFS), cannot exceed .001
(-60 dBFS), and applies to FIFO too. Legacy sweeps remain opt-in, bounded and
capped; they are not the next recommended test. A cap is not a physical SPL limit.
The legacy pattern flag is a tone alias; it does not implement the old advertised
440/880 sequence. The HP sample counter now counts generated channel samples.

## Verification performed and remaining work

`make check audio-check` passed locally. The C suite verifies literal silence
and signed24 vectors, independent stream markers, 96/176/variable packet lengths,
zero and mixed layouts, capacity checks, packet-local check phase, continuous
440Hz for each logical channel, amplitude bounds, busy-slot ownership, cancellation
callback release, injected submission failure and invalid RX lengths under ASan/UBSan.
The CLI suite verifies 14 cases using --dry-run. Existing audio patch assertions,
RX model, C payload extraction, PCM models and synthetic recording analyzer passed.
No physical USB access, playback, recording or system modification was performed.

Native IOKit compatibility, queue timing, device-disconnect/shutdown behavior and
analog output require hardware verification. FIFO partial-frame underflow behavior
is inherited and not certified for DAW playback. No CoreAudio device was installed
or implemented; choose its architecture only after an analog recording verifies
raw playback. This milestone improves validation and failure behavior; it does
not yet satisfy the ultimate reliable playback objective.


## Hardware follow-up and isolated layout probes

User-supplied hardware results, November 2:

- Fixed96 silence: 23992 OUT packets, 2999 successful rearms, zero reported
  transfer errors, approximately three seconds. This verifies transport for that
  run, not audible output.
- `/tmp/kore-rxmatched.log`: first OUT submission failed at approximately
  0.684 seconds process time with native `invalid argument`. Exact layout:
  `0,176,176,0,176,0,176,0`, total 704. IN had completed eight descriptors with
  four nonempty packets. OUT completed none. Cleanup released the interface and
  closed the context without a cancellation timeout.

The first-submit failure rules out accumulated failed OUT retries as its initiating
cause for this run. It does not distinguish zero placement from variable lengths
or another native constraint. The Apple legacy userspace
[WriteIsochPipeAsync wrapper](https://github.com/opensource-apple/IOUSBFamily/blob/master/IOUSBLib/Classes/IOUSBInterfaceClass.cpp)
sums frReqCount and forwards the frame list; it has no explicit zero-length
rejection there. This historical implementation is not proof of the behavior of
the current macOS kernel or its compatibility layer. The official Apple source
inventory checked during this investigation contained empty implementation files;
no modern kernel constraint was established from it.

A new `--audio-layout-probe PROFILE` submits **exactly one** eight-packet batch
of synthetic encoded silence after the first successful IN completion. It stops
on completion, error or deadline. Profiles preserve eight time slots. They do not
replace RX-matched scheduling and must not be used as a playback clock. No tone,
FIFO, checkbyte override or other OUT mode can be combined with a probe. A timeout
without a successful completion returns failure. The exact selected layout is
logged before submission; rejection is never retried. Log output is now line
buffered so initialization messages precede native submission errors.

| Profile | Eight descriptor lengths | Purpose |
| --- | --- | --- |
| uniform96 | 96,96,96,96,96,96,96,96 | Known-size control with RX-triggered startup |
| uniform176 | 176,176,176,176,176,176,176,176 | Isolate size from mixed layout |
| leading-zero | 0,176,176,176,176,176,176,176 | Zero in first descriptor |
| trailing-zero | 176,176,176,176,176,176,176,0 | Zero in last descriptor |
| interior-zero | 176,176,176,0,176,176,176,176 | Zero inside descriptor list |
| observed | 0,176,176,0,176,0,176,0 | Exact hardware-rejected layout |
| variable-nonzero | 96,176,96,176,96,176,96,176 | Variable lengths without zeros |

Build with `make audio`; `make check audio-check` remains hardware-free. The
28 CLI cases include every probe and incompatible combinations. Production
callback mocks verify each layout, exactly one submission, completion stop,
rejection without retry, and that an OUT failure in an IN callback prevents IN
rearm. The earlier callback performed one extra IN rearm after requesting a stop;
this is fixed and regression tested. This did not cause the initial OUT rejection.

Prepared hardware commands, **not executed by the agent**: disconnect speakers
and headphones and minimize gain. Run one profile at a time, beginning with the
controls. Each command below sends at most one millisecond of encoded silence
at interval 1, eight microframes; the 1000ms deadline is a completion timeout.

```sh
LIBUSB_DEBUG=4 ./build/kore1-bridge-audio --audio-layout-probe uniform96 --audio-duration-ms 1000 > /tmp/kore-layout-uniform96.log 2>&1
LIBUSB_DEBUG=4 ./build/kore1-bridge-audio --audio-layout-probe uniform176 --audio-duration-ms 1000 > /tmp/kore-layout-uniform176.log 2>&1
```

If both controls complete, run the nonzero variation and individual zero-position
probes separately, substituting `variable-nonzero`, `leading-zero`, `trailing-zero`,
`interior-zero` or `observed` for the profile and corresponding log filename.
Do not proceed to tones. If a control fails, preserve that log before interpreting
zero-placement results. If controls pass and only zero layouts fail, that supports
a zero-descriptor restriction on this host/endpoint, rather than a universal macOS
rule. If only leading-zero fails, first-descriptor placement becomes the next
hypothesis. If variable-nonzero fails too, variable-size handling remains suspect.


## Probe results: final descriptor hypothesis

Read complete local logs supplied by the user's hardware runs:

| Profile / mode | Completed OUT descriptors | Bytes | Result |
| --- | ---: | ---: | --- |
| uniform96 | 8 | 768 | Completed, zero reported errors |
| uniform176 | 8 | 1408 | Completed, zero reported errors |
| variable-nonzero | 8 | 1088 | Completed, zero reported errors |
| trailing-zero | 0 | 0 | Native invalid argument on submission |
| interior-zero | 8 | 1232 | Completed, zero reported errors, including one zero descriptor |
| leading-zero | 8 | 1232 | Completed, zero reported errors, including one zero descriptor |
| Original RX-matched batch: 0,176,176,0,176,0,176,0 | 0 | 0 | Native invalid argument on submission |

Leading and interior zeros are demonstrably accepted for these runs. Trailing
zero is the distinguishing feature of the rejected layouts so far. This supports
a final-descriptor constraint on the tested path, but single runs do not prove
a deterministic kernel rule, and the original failing batch also has more zeros.
No further physical audio output or DAC functionality has been established.

New profile `observed-last-data` swaps only the last two descriptors of the
original rejected batch: `0,176,176,0,176,0,0,176`. Total payload, descriptor count
and number of zeros remain 704 bytes, eight descriptors and four zeros. It tests
the final-descriptor hypothesis with the original smaller payload. This synthetic
reordering is a diagnostic only; applying it to playback would change timing.
There is no automatic reordering, padding or removal of RX-derived descriptors.

`make check audio-check` passed after adding the profile, including ASan/UBSan,
the production scheduler mock and 29 CLI cases. No new hardware run was performed
by the agent. With speakers/headphones disconnected and gain minimized, the
prepared one-batch test is:

```sh
LIBUSB_DEBUG=4 ./build/kore1-bridge-audio --audio-layout-probe observed-last-data --audio-duration-ms 1000 > /tmp/kore-layout-observed-last-data.log 2>&1
```

If it completes, repeat the matching `observed` profile in a separate process
before treating last-descriptor position as reproducible. A timing-preserving
scheduler adaptation still needs design and native testing; success of a reordered
probe would not make reordering a valid audio implementation.
