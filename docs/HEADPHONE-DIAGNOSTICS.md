> Status reference (2026-11-03): Current investigation record with rejected and valid captures. The headset adapter hypothesis remains unconfirmed; no full headphone-load quality claim is made.
> See [current status](STATUS.md) and [documentation index](INDEX.md).

# Headphone level and hollow-sound investigation

Measurements were taken on 2026-11-02; the record was completed on 2026-11-03.
The user moved the headphone output to MAYA44 inputs 1/2, turned up its analog
volume and reported that the hollow/echo-like sound had been heard directly on
a headset, not only through MAYA44 monitoring. The user confirmed a four-pole
headset plug (three insulating rings) used through an adapter. The adapter pinout,
headset model and behavior under a proper headphone load remain unverified.

## Signal path

Main and headphones already use the same PCM conversion and shared four-channel
ASRC phase. Only their source channels and explicit software gains differ:
Main channels 1/2 at gain 0.2; headphones 3/4 at gain 1. No echo, spatializer or
headphone-specific EQ exists in the bridge. Independent Mixxx cueing is retained
at the user's request. No additional software amplification or channel mirroring
was installed during this investigation.

## Evidence

| Artifact suffix | Test | Result |
|---|---|---|
| 215118-544600 | Headphone stereo, full test signal | ADC clipping: 138744 samples; reject quality conclusions |
| 215309-480985 | Same at signal level 0.1 | No clipping, but 804-frame capture timestamp gap; reject |
| 215351-436229 | Repeat at level 0.1 | Contiguous 441000 frames, no clipping; L -10.25 / R -10.14 dBFS; L -> input 2, R -> input 1 |
| 215525-987914 | Main-only excitation, physical headphone capture | Expected negative control: searched tones around -111/-114 dBFS, no convincing Main leakage; the positive stereo analyzer correctly failed |
| 215709-303461 | Headphone 50ms chirps, level 0.1 | Contiguous, no clipping; strongest delayed matched correlation -57.0/-57.6 dB relative to main peak in the 5..250ms search window |

The artifact directories above have prefix `coreaudio-loopback-20261102-` and
remain ignored local recordings. Results do not certify distortion, frequency
response, loaded headphone behavior or absolute analog voltage. The chirp
correlation threshold is -40 dB; clock mismatch and filters affect its limit.
A low result is not proof that every possible echo or audible artifact is absent.

Actual Mixxx playback of a generated chirp track was also attempted. Its track
properties showed ReplayGain +15.0288 dB, so the file's nominal 0.1 amplitude was
not a reliable output-level bound. `mixxx-headphone-echo-20261102-215913` clipped
799 samples. A tagged-FLAC attempt (`mixxx-headphone-flat-20261102-220351`) still
clipped 754 samples; that attempt did not establish controlled gain or clean
Mixxx audio quality. Another capture (`...220234`) missed the GUI playback and
contained no convincing probe. All three are excluded as positive quality tests.
Mixxx's persisted headDelay and main delay were zero and split cue was disabled.

## Next physical check

The TRRS plug/adapter is a plausible cause of poor direct headset sound, not a
confirmed diagnosis. Some three-pole outputs do not correctly contact four-pole
headset plugs. Test a conventional stereo headphone with a TRS plug (two
insulating rings), or use a headset-compatible splitter: TRRS headset socket ->
headphone TRS plug -> stereo 3.5-to-6.3mm adapter -> KORE. The splitter must match
the headset wiring; a microphone-only TRRS adapter is unsuitable. Start with low
KORE analog volume. This check cannot be performed by software or replaced by
increasing digital gain. Do not change L/R polarity or apply compensation without
measurement under the actual headset load.

Manufacturer references: [RØDE headset-to-mixer connection](https://help.rode.com/hc/en-us/articles/17810033911183-How-to-Connect-the-NTH-100M-to-a-Mixer-for-Monitoring-and-Microphone-Audio),
[Sony plug-type troubleshooting](https://www.sony.ie/electronics/support/articles/00122966).

## Reproduction

The helper now defaults to signal level 0.1 (-20 dB) independently of the login
bridge gains. Existing earlier captures often used full-level test samples;
record the signal-level metadata when comparing results. No test opens hardware
without `--run-tone`.

```sh
# Optional NumPy dependency for matched-chirp analysis; offline controls only:
make echo-check

# Only with the authorized, connected MAYA44 loopback setup:
python3 automation/coreaudio_loopback.py --run-tone --stereo-test --headphones --use-login-bridge --signal-level .1
python3 automation/coreaudio_loopback.py --run-tone --echo-test --headphones --use-login-bridge --signal-level .1
```

The echo helper checks for NumPy before opening devices. Its analyzer rejects
capture gaps and malformed metadata, and marks clipped captures as failed.
Synthetic tests detect a known 80ms replica and reject clipping/capture gaps.
The separate renderer accepts `--level 0..1`; it retains its default level 1
for compatibility with the offline/system HAL integration test.
