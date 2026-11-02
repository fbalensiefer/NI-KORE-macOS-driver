# First recorded KORE1 analog playback — 2026-11-02

## What was verified

The user confirmed that KORE headphone OUT was connected to MAYA44 line input,
with speakers/headphones disconnected and low gain, and explicitly authorized
bounded low-level tones and recording. The agent ran the native RX-driven bridge
with one logical output selected at a time. Four-channel MAYA44 recordings show a
repeatable near-440 Hz component when logical output 3 is selected, strongest on
MAYA44 capture channel 2. Output 4 produces a component strongest on capture
channel 1. This is recorded evidence of analog output through the confirmed KORE
connection, rather than an inference from USB completion or the old phone test.

It is an experimental playback milestone, not proof of production-quality audio,
a calibrated physical left/right mapping, all four output jacks, ADC capture, or
a working CoreAudio device. These mappings apply to this connection only.

## Implementation and safety

`audio/tone.h` generates signed24 BE 440 Hz using a fixed 48000-Hz PCM timebase.
It preserves per-stream byte positions across variable and zero-length packets,
starting at Linux's byte-4 playback offset. The selected logical channel alone
has nonzero PCM. Its timeline is 0.5 s silence, 1 s tone, then silence, with 5 ms
fades. Default peak amplitude is .00025, approximately -72 dBFS; the hard ceiling
is .001 (-60 dBFS). No automatic amplitude escalation was performed.

Native CLI: `--stream-tone 1..4` explicitly enables the tone, requires a fixed
3000-ms session, and excludes silence/synthetic layouts. `--tone-amplitude` is
rejected without a tone mode. Silence remains the default; the existing silence
command does not generate tones. The native bridge still owns interface 0
exclusively and leaves the existing controller implementation separate.

`automation/native_loopback.py` checks the current AVFoundation device listing
for MAYA44 USB+ at the explicitly selected index, opens a ten-second four-channel
44100-Hz signed32 capture, waits for capture readiness, then starts the bounded
tone process. It refuses to start a tone if capture is not ready. Each run has a
unique ignored artifacts folder, with device listing, recording, driver/capture
logs and metadata. No recordings or generated binaries are committed.

The initial capture readiness attempt timed out before any tone started. The
user granted microphone access in macOS; capture then worked. No TCC setting was
changed programmatically.

## Measurements

All tests used amplitude .00025. Each successful driver session generated 48000
selected-channel tone samples and completed 2963 OUT batches before clean
shutdown. All captured channels were retained; no stereo downmix was used.

| Logical KORE output | Strongest capture channel | 440-Hz component RMS | Neighbor contrast | Result |
| --- | ---: | ---: | ---: | --- |
| 1 | 1 | -107.29 dBFS | 9.69 dB | No convincing tone at this level |
| 2 | 1 | -110.28 dBFS | 13.23 dB | No convincing tone at this level |
| 3, first run | 2 | -84.75 dBFS | 18.86 dB | Detected |
| 4 | 1 | -84.35 dBFS | 15.09 dB | Detected |
| 3, repeat before cleanup fix | 2 | -85.57 dBFS | 17.24 dB | Detected; driver cleanup exited 1 |
| 3, repeat after cleanup fix | 2 | -85.86 dBFS | 18.34 dB | Detected, clean driver/capture exits |

The final clean repeat's pre-tone 440-Hz baseline on input 2 was -130.30 dBFS.
A local frequency search found a peak at approximately 440.75 Hz; the first
channel-3 run peaked at approximately 440.5 Hz. These are rough capture-based
measurements, not proof of sample-clock accuracy. A weak component on another
capture channel may be crosstalk and is not an additional independent output.

`automation/analyze_native_capture.py` searches half-second windows, measures
coherent 440-Hz amplitude against nearby frequencies and the initial baseline,
and reports each channel separately. Synthetic positive and noise-only negative
controls test it. The thresholds are diagnostics rather than an audio quality
standard. Derived per-run results and WAV SHA256 hashes are retained in
[TONE-MEASUREMENTS.json](TONE-MEASUREMENTS.json).

## Shutdown bug found and fixed

One repeat recorded the tone but failed closed during cleanup: synchronous native
abort returned before every completion callback had reached the serial queue.
The buffers were retained until process exit, so no pending buffer was freed.
Shutdown now waits up to 500 ms for late callbacks to release their slot ownership.
Each late cancellation wakes the drain. Timeout still retains backing storage
rather than releasing live objects. A deferred-abort mock recreates the ordering
and tests this fix under ASan/UBSan. The post-fix recorded repeat closed cleanly.

## Validation and remaining limitations

`make check audio-check native-check` passed. New tone tests decode actual packed
0/176-byte sequences for each logical channel and verify signed24 representation,
48-kHz frame continuity, single-channel isolation, approximately 440 cycles in
the one-second gate, amplitude/fades and silent guard periods. CLI coverage is
now 28 native cases. Existing silence/layout/controller tests continue to pass.

The ten-second AVFoundation sessions contain only about 9.1–9.25 seconds of stored
PCM samples despite their ten-second timestamp span. This capture discontinuity
prevents reliable conclusions about tone duration, clock accuracy, dropouts or
waveform purity. Improve the capture path before claiming audio quality. The
low recorded level also limits measurements; no gain change was made by the agent.

Latest clean evidence (local files):

- `artifacts/native-tone-ch3-20261102-152003/maya44-4ch.wav`
- Same folder: `driver.log`, `capture.log`, `run.json`, `analysis.json`.

Next: establish continuous capture with trustworthy sample/timestamp accounting,
then verify duration, distortion and channel routing with additional bounded
measurements. A PCM application feed and CoreAudio integration can now be designed
on top of the native transport, but neither is implemented by this milestone.
