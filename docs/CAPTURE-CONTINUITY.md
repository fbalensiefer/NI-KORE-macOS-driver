# Capture continuity and host-relative clock measurements — 2026-11-02

## Capture gap issue addressed

The old FFmpeg/AVFoundation ten-second sessions stored only about 9.1–9.25 seconds
of PCM. A new direct CoreAudio HAL recorder (`coreaudio/capture.m`) bypasses that
path. It selects MAYA44 by stable UID/name rather than a transient AVFoundation
index and preserves all input channels at the device's current nominal rate.
It changes no default device, gain, sample rate or hardware buffer setting.

The input IOProc copies float32 buffers into preallocated RAM. It allocates
nothing and performs no file IO during capture. Samples are interleaved and
written as signed32 WAV after AudioDeviceStop/DestroyIOProcID. It handles
interleaved or multiple input buffers, trims only the final block to the exact
requested frame count and rejects inconsistent byte/frame layouts. Recording
is explicitly opt-in and bounded to ten seconds. A watchdog handles absent IO.

Every callback's sample and host timestamps are checked. Missing/overlapping
sample-time progression or invalid timestamp flags make the result fail. Host
clock rate is estimated from the sample-time delta over the corresponding host
time span. This is a relative clock check, not an absolute reference calibration.

Three live direct-HAL captures passed (one baseline and two KORE tone sessions):

- Four channels, nominal 44100 Hz, exactly 441000 stored frames/channel.
- Exactly ten seconds of stored PCM, 862 callbacks each.
- Zero sample-time gaps, zero invalid callbacks, clean start/stop/destroy.
- 441344 frames observed including the final partially retained buffer.

Thus the original missing-sample problem is absent on the replacement path for
these runs. This does not diagnose the exact AVFoundation/FFmpeg loss mechanism
or certify continuity under arbitrary host load.

The loopback helper now defaults to this capture backend. AVFoundation remains
available explicitly with `--capture-backend avfoundation --input-index N` for
comparison. The helper will not start a tone unless capture reports ready.

## Analog and clock results

Both new tone sessions used the already authorized .00025-peak single-channel
440-Hz test on KORE logical output 3. MAYA44 input 2 was strongest. Playback and
capture processes exited successfully. A coarse spectral search reports 440.0 Hz;
a finer short-window fit estimates 440.09 Hz at the nominal capture sample rate.

| Quantity | First gap-free tone capture | Repeat |
| --- | ---: | ---: |
| MAYA44 sample rate relative to host timestamps | 44099.7153 Hz | 44099.8082 Hz |
| MAYA44 rate error relative to host | -6.45 ppm | -4.35 ppm |
| Host-corrected analog tone estimate | 440.0872 Hz | 440.0881 Hz |
| Relative tone error from nominal 440 | +198.1 ppm | +200.2 ppm |
| USB-payload inferred KORE frame rate | 48009.3458 Hz | 48009.3458 Hz |
| Tone predicted by that payload rate | 440.0857 Hz | 440.0857 Hz |
| Tone RMS component | -82.84 dBFS | -82.79 dBFS |
| Broadband residual RMS after tone/DC removal | -76.62 dBFS | -76.67 dBFS |
| Clipped samples | 0 | 0 |

The payload rate is a mathematical deduction: completed IN bytes / 16 transport
bytes per stereo frame per stream, divided by completed one-millisecond USB
batches. Actual lengths and alignment-2 framing underpin that deduction. Finite
176-byte packet quantization gives about 76.5 ppm uncertainty over a three-second
count; it is not a direct calibrated oscillator measurement. Its agreement with
the analog tone fit supports a small host-relative device-clock offset.

The .01-Hz search grid is not a claim of .01-Hz absolute measurement accuracy.
The fit uses half a second of a low-level tone. Residual noise exceeds tone RMS
by about 6.2 dB; these tests cannot establish a useful DAC distortion or dynamic
range specification. Neither gain nor digital amplitude was raised automatically.
The observed clock difference is relevant to future long-running CoreAudio rate
matching and must not be hidden by changing the USB packet lengths.

## Reproduce

```sh
make hal-capture native-probe
./build/kore-hal-capture --list
# Explicit recording; new output filenames required:
./build/kore-hal-capture --record artifacts/capture.wav --report artifacts/timing.json --seconds 10
# Explicit authorized tone + capture, defaults to the direct HAL backend:
python3 automation/native_loopback.py --run-tone --channel 3
```

Quality analysis requires a contiguous-capture timing report and matching WAV
frame count/rate. It refuses discontinuous metadata:

```sh
python3 automation/measure_native_quality.py PATH/maya44-4ch.wav --timing PATH/timing.json --driver-log PATH/driver.log --report PATH/quality.json
```

The new offline fixture injects a known small tone-frequency offset and verifies
the estimator and gap-refusal behavior. Sample accounting, final-block trimming
and malformed buffers are also tested under ASan/UBSan.

Raw captures/logs stay in ignored artifacts folders. Small derived timing/quality
records and hashes are in `CAPTURE-MEASUREMENTS.json`. See
[COREAUDIO-PROTOTYPE.md](COREAUDIO-PROTOTYPE.md) for the local, uninstalled output
plug-in and the remaining host-integration and drift-management work.
