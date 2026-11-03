> Status reference (2026-11-03): Dated isolated-transport measurements. Shared continuous playback now uses different pools/lead and has documented scheduling failures.
> See [current status](STATUS.md) and [documentation index](INDEX.md).

# Native RX-driven silence stream — 2026-11-02

## Implementation

`audio/native_probe.m` now supports `--stream-silence --duration-ms N` in
addition to the existing one-batch probes. Hardware access still requires
`--run`; dry-run and offline tests do not open a device. N is strictly limited
to 100..3000 ms, default 3000. There is no tone or FIFO path in this executable.

The stream preallocates 32 IN and 64 OUT slots, each holding eight transaction
descriptors and a 4097-byte buffer. An IN completion supplies exactly eight
actual lengths for the corresponding OUT submission, in their original order.
Zero-length transactions reference offset 0. All-zero batches retain all eight
empty time slots; their buffer remains nonempty. No PCM bytes or microframes
are added, removed, reordered or duplicated. Native backing capacity is independent
of the wire payload. Synthetic layout overrides are excluded from stream mode.

IN starts four USB frames ahead and keeps 32 one-millisecond batches in flight.
OUT starts 32 frames ahead of the first processed IN completion, then uses
contiguous frame numbers, one batch per millisecond. High-speed and interval 1
are required and verified. The resulting fixed buffering lead trades latency
for tolerance of userspace callback jitter; it does not change sample rate or
packetization. The OUT timeline is never silently rebased. A missed deadline,
unexpected callback order, invalid packet, submission failure or exhausted pool
stops the stream. Outstanding transfers are synchronously aborted and callback
ownership is drained before releasing buffers/interface.

All scheduling, buffers and counters are owned by the serial callback queue.
A monotonic deadline and dispatch timer stop new submissions; main waits are
bounded. SIGINT/SIGTERM stop early. Final statistics distinguish completed batches
from callbacks arriving during shutdown. The latter are labeled `cancelled` and
can include already-completed requests whose callbacks arrived after stopping.
They are expected at the bounded deadline and are not counted as stream errors.
Shutdown takes several milliseconds beyond the submission deadline.

The device-info retry path now tags bulk callbacks with a generation so a late
callback from an aborted earlier attempt cannot affect the current attempt.
Initial GET_DEVICE_INFO timeouts are still observed and recovered by retries.
The driver does not infer audio readiness without the subsequent AUDIO_PARAMS ACK.

## Offline verification

```sh
make check audio-check native-check
```

Passed locally, including warnings-as-errors builds and AddressSanitizer /
UndefinedBehaviorSanitizer callback tests. A new deferred mock executes the actual
production stream code for 200 ordered batches, asserting unchanged lengths and
empty time slots, contiguous frame numbers, no in-flight buffer reuse, bounded
pool use, all-zero batches, explicit pool exhaustion, rejected submissions,
deadline stop and cancellation cleanup. Another regression replays an obsolete
bulk callback after a new generation. Native CLI coverage now includes 18 cases.
The existing PCM, native layout and controller repository checks continue to pass.

## Authorized live tests

The user explicitly authorized bounded silence tests, confirmed disconnected
speakers/headphones and minimized output gain. The agent executed these tests.
No tones, analog recording, CoreAudio installation or system-library changes
were performed. Opening the USB user client required execution outside the
Codex filesystem sandbox; no sudo or root device capture was used.

| Test | Result | Completed OUT batches | Completed bytes |
| --- | --- | ---: | ---: |
| Initial 500 ms, 4 ms OUT lead | Passed | 491 | 377168 |
| Initial 3000 ms, 4 ms lead | Stopped at about 63 ms: `0xe00002ee` | 36 | 27632 |
| 3000 ms, 32 ms lead, run 1 | Passed, clean deadline shutdown | 2962 | 2275328 |
| 3000 ms, 32 ms lead, run 2 | Passed, clean deadline shutdown | 2963 | 2276032 |
| 3000 ms, 32 ms lead, run 3 | Passed, clean deadline shutdown | 2963 | 2276032 |
| SIGTERM after about 200 ms of streaming | Clean early shutdown, expected exit 1 | 228 | 175120 |
| Reopen after SIGTERM, 3000 ms | Passed, clean deadline shutdown | 2963 | 2276032 |
| Final 500 ms after bulk-generation fix | Passed | 463 | 355696 |

Apple's installed IOReturn.h identifies `0xe00002ee` as kIOReturnIsoTooOld:
the explicit request missed its scheduling window. This differs from the original
trailing-zero invalid-argument rejection. Increasing the fixed lead to 32 ms and
OUT capacity to 64 slots resolved that failure in the subsequent runs. It does
not prove immunity to arbitrary host load, disconnects or longer sessions.

The three-second reopen run completed 23704 OUT descriptors without a reported
packet error. Its RX side supplied 1363 batches ending in zero. Peak OUT occupancy
was 34/64. At shutdown, 33 OUT callbacks and 32 IN callbacks were handled after
stopping; no pending buffers were freed prematurely. The subsequent final build
passed a 500 ms test with 225 trailing-zero RX batches and peak OUT occupancy 34/64.
All-zero batches were exercised offline; none appeared in these live runs.

Logs remain local in `/tmp/kore-native-stream-*.log`; they are not committed.
The successful run following the initial failure is `3000ms-2.log`, then `-3.log`
and `-4.log`. Early-stop, reopening and final results are in `signal.log`,
`reopen.log` and `final.log` respectively, with the same prefix.

## Reproduce and next milestone

Stop all other KORE bridge processes before using the native transport. Its
interface ownership is exclusive; MIDI remains in the separate existing bridge.
Under the user's existing silence-test authorization:

```sh
./build/kore1-native-probe --run --stream-silence --duration-ms 3000
```

Expected: firmware ACK, `failed=0 deadline_reached=1`, thousands of completed OUT
batches, and `NATIVE transport closed`. Do not interpret synthetic silence or
successful transfers as proof that the DAC or headphone output plays valid PCM.

This milestone establishes repeated three-second native silence transport on the
connected KORE1. It does not expose a CoreAudio device or certify DAW playback.
The next step is separately authorized capped single-channel PCM plus a verified
MAYA44 capture, preserving all capture channels and checking its current device
selection. Tone generation remains disabled pending that authorization.


## Subsequent analog playback milestone

The user subsequently authorized tones and MAYA44 recording. A capped native
tone path and repeated recordings now demonstrate near-440 Hz analog output on
the confirmed headphone connection for logical channels 3/4. See
[TONE-PLAYBACK-RESULTS.md](TONE-PLAYBACK-RESULTS.md) for exact results, the cleanup
race discovered during recording and fixed, and capture-quality limitations.
