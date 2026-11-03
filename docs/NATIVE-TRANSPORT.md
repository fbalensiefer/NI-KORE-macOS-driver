> Status reference (2026-11-03): Native diagnostic transport guide/milestone. For current shared ownership, continuous-mode pools, gains and installed operation use the architecture/setup guides.
> See [current status](STATUS.md) and [documentation index](INDEX.md).

# Native transport prototype: preserve trailing zero-length time slots

## Finding and hypothesis

Hardware evidence in the complete local logs shows:

| Original libusb layout | Result |
| --- | --- |
| Uniform96, uniform176, alternating96/176 | Completed |
| Leading zero, interior zero | Completed, including the zero descriptor |
| Trailing zero | Native invalid argument |
| 0,176,176,0,176,0,176,0 (704 bytes) | Rejected in RX mode and repeated synthetic probe |
| 0,176,176,0,176,0,0,176 (704 bytes) | Completed, all eight descriptors |

Thus the tested native path accepts zero-length packets, with a reproducible
correlation between rejection and a final zero. The exact kernel validation check
is unknown. The hypothesis is that a compacted final zero points at the end of
the mapped buffer and violates an offset bound. This is not yet a proven cause.

The existing libusb Darwin path derives offsets implicitly from packet lengths.
The installed Apple SDK provides a public alternative: IOUSBHost.framework,
IOUSBHostPipe's transaction-list API (macOS 12+). Each transaction has its own
requestCount and offset. The native prototype changes host metadata only: zero
transactions can reference offset 0; nonempty PCM stays packed in order. Eight
transactions remain eight microframes, including a final zero. A backing-buffer
guard byte is optional and is never added to any requestCount or USB payload.

References inspected: local macOS 26.5 SDK IOUSBHostPipe.h, IOUSBHostInterface.h,
IOUSBHostObject.h and IOUSBHostDefinitions.h; Apple's
[IOUSBHost framework documentation](https://developer.apple.com/documentation/iousbhost/).
The transaction API allows an explicit offset and schedules firstFrameNumber=0
on the next available XHCI frame. The prototype is not a Linux start-frame port.

## Implemented

`audio/native_probe.m` is a separate native transport prototype. It:

- Defaults to dry-run. Hardware access requires explicit `--run`.
- Opens only KORE1 17cc:4711, interface 0, without device capture or seizure.
- Requires a verified high-speed link and validates alt 1's ISO endpoint interval
  and 512-byte capacity. Refuses unknown properties instead of guessing timing.
- Opens bulk and ISO pipes through IOUSBHost; libusb is not linked.
- Arms bulk IN before commands; validates device spec and AUDIO_PARAMS ACK.
  GET_DEVICE_INFO has up to three retries, with synchronous abort/drain before reuse.
- Starts one ISO IN batch; submits one encoded-silence OUT batch from completion.
- Uses the shared tested CAIAQ packer; includes no tone, FIFO, MIDI or LCD code.
- Keeps transaction arrays and buffers alive until callbacks/abort finish.
- Uses one serial callback queue; main-thread waits have a one-second limit.
- Stops on errors and logs native NSError domain/code and per-packet completions.
- Aborts pipes synchronously, drains the queue, then destroys the interface.
  If abort fails, exits without freeing potentially live backing storage.

The `observed` profile sends the exact previously rejected descriptor sequence.
`--layout rx` instead uses successful first-IN actual lengths without changing
order. All-zero RX batches fail closed in this one-shot prototype. The one-shot mode does not expose a CoreAudio device. A bounded continuous
RX-driven silence mode has since been implemented and hardware tested; see
[NATIVE-STREAM-RESULTS.md](NATIVE-STREAM-RESULTS.md). The
working libusb MIDI bridge remains unchanged and cannot run concurrently because
both transports require exclusive ownership of interface 0.

`audio/native_plan.h` separates native transaction offsets from PCM payload offsets.
This prevents a metadata workaround from changing wire payloads or packet order.
The prototype uses no installed driver, entitlement, system library patch, root
privileges, endpoint policy changes or device reset.

## Build and offline verification

```sh
make native-probe
make check audio-check native-check
```

Requires macOS 12+, a current Xcode/CLT SDK with IOUSBHost.framework, and the
existing project dependencies for the libusb tests. Builds use warnings as errors.
Native tests verify 1020 combinations (255 nonempty masks, two offset policies,
two guard settings), exact descriptor lengths/order, packed PCM offsets and
untouched guard storage. ASan/UBSan production callback tests use mock pipes to
exercise native IN-to-OUT submission, packet completion validation, injected
native rejection, and synchronous cleanup. Ten native CLI cases do not initialize
IOKit or access hardware. The mocks cannot establish framework/kernel acceptance.

Software verification passed locally. No native hardware access or analog playback
was performed by the agent. This is a prepared implementation of a hypothesis,
not a verified solution to the USB rejection or an operational audio driver.

## Next hardware experiment (prepared, not executed)

Stop all bridge processes, disconnect speakers/headphones, minimize output gain.
Encoded silence can still be misinterpreted by unverified hardware framing.
Run without sudo; if interface ownership fails, preserve that error rather than
seizing or resetting the device.

```sh
./build/kore1-native-probe --run --layout observed --zero-offset start --buffer-guard 1 > /tmp/kore-native-observed.log 2>&1
```

This sends exactly one 704-byte payload using eight unchanged requests:
`0,176,176,0,176,0,176,0`. The backing allocation is 705 bytes; the extra byte is
not transmitted. Success requires every transaction status and completeCount to
match its request, not just successful enqueueing. Failure of device-info or
AUDIO_PARAMS prevents ISO startup. The command exits after completion or error.

If the baseline succeeds, compare one change at a time in separate invocations:
`--zero-offset start --buffer-guard 0`, then `--zero-offset compact --buffer-guard 0`.
These isolate backing capacity from zero-offset choice within the modern API.
Do not label a baseline success proof of the offset hypothesis: the API, buffer
capacity and offset policy all differ from the original libusb experiment.

After native acceptance is verified, integrate a bounded RX-driven transfer pool
with explicit frame scheduling and shutdown tests. Only then use opt-in low-level
single-channel PCM and an analog recording to validate DAC routing/playback.

## Discovery correction

The first hardware attempt stopped at interface discovery, before AUDIO_PARAMS
or ISO submission. A read-only registry check confirmed an IOUSBHostInterface
for 17cc:4711/interface 0. The framework-generated compound matching dictionary
had not found it. Discovery now enumerates IOUSBHostInterface services and checks
numeric VID, PID and interface-number properties explicitly. It refuses zero or
multiple matches and releases all unselected registry references.

A second prototype bug was corrected: this host reports `USBSpeed=3` but legacy
`Device Speed=2` for the same high-speed KORE link. Comparing the former to the
legacy enum would incorrectly reject the device. The speed guard now reads
`UsbLinkSpeed=480000000` bit/s from the selected interface/ancestor instead.

`make native-check` passes with new identity-filter regression assertions.
`./build/kore1-native-probe --discover` was executed successfully: exactly one
KORE interface was found and its 480-Mbit/s link was verified. This command only
reads registry properties: it opens no USB user client and sends no USB commands.
The native OUT workaround still has no hardware result; retry the original
single-batch `--run` command to reach that test. No system configuration change,
reset or elevated USB access is involved in the discovery fix.


## Continuous silence milestone

`--run --stream-silence --duration-ms 3000` uses 32 IN / 64 OUT slots, explicit
contiguous frame scheduling and a fixed 32 ms OUT lead. Repeated authorized
hardware runs and early-stop/reopen tests passed. See
[NATIVE-STREAM-RESULTS.md](NATIVE-STREAM-RESULTS.md) for exact counts, the initial
scheduling failure, offline regression coverage and limits. No tone was enabled.
