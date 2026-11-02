# Adaptive PCM rate matching — 2026-11-02

## Implemented, without installing CoreAudio

The local PCM bridge now has an occupancy-controlled asynchronous sample-rate
matcher in `coreaudio/rate_match.h`. It adapts host-paced PCM to the KORE-paced
consumption rate. USB descriptors, zero-length time slots, frame scheduling and
AUDIO_PARAMS stay unchanged. Existing silence and native tone modes are unchanged.

The new code uses two source frames for linear interpolation, a fractional phase
accumulator, and a PI controller limited to +/-1000 ppm. Occupancy is low-pass
filtered on roughly one-second timescales to reduce block-arrival modulation.
An initial one-second calibration estimates the actual startup reservoir, so USB
startup buffering is not mistaken for sustained clock error. During calibration,
ratio remains 1; there is no claim that a three-second run reaches clock lock.

The source primes only after at least 2050 frames are queued. Starvation causes a
counted fade to silence and re-priming, with a fresh calibration and no integrator
wind-up. Input non-finite values are sanitized, normalized input is limited, and
the existing .00025 gain/ceiling remains applied by the native signed24 packer.
One producer/one consumer queue ownership is preserved. There are no allocations,
locks, disk operations or property IPC in the resampling/USB callback path.

The virtual device's safety-offset declaration now includes the requested
2048-frame PCM reservoir. Exact end-to-end HAL latency remains unverified because
the user chose not to install the output plug-in. Actual startup occupancy can
differ from the requested minimum and is calibrated and logged.

## Offline verification

```sh
make check audio-check native-check coreaudio-check
```

`tests/test_rate_match.c` executes the actual rate matcher for five 180-second
simulations: +200, -200, +500, -500 and 0 ppm. Total simulated time is 15 minutes,
with a 512-frame block producer. These simulations run without sleeping or hardware.
All five cases maintain bounded occupancy without starvation or ring overflow.
After settling, recorded queue ranges across cases are about 1512..2058 frames,
well below the 8192-frame capacity and away from empty.

The instantaneous ratio varies with producer block phase; this is not a claim of
an exact constant correction. Tests check bounded ratio and occupancy, input/output
accounting, independent fractional interpolation vectors, finite stereo output,
empty-input behavior, starvation and recovery. Tests run under ASan/UBSan.
The existing native transport, local bundle ABI and PCM safety-cap tests pass.

These are block-arrival simulations, not arbitrary scheduler-load tests. This is
a basic linear-interpolation prototype, not a production band-limited resampler:
high-frequency amplitude/alias behavior needs further DSP validation or a better
conversion filter before claiming full-band audio quality.

## Live test path and findings

`--pcm-test-source` feeds the same PCM/rate-matching path with a host-clock-paced
48k stereo source. It requires `--run`, is bounded by the same 100..3000-ms parser,
selects logical output 3, and generates only a one-second 440-Hz test inside a
silence envelope. The test uses a .00025 output gain/ceiling. It does not install
or require the virtual device. It excludes silence, native tone and CoreAudio
source selections; synthetic and CoreAudio sources cannot be selected together.

The existing tone/recording authorization was used for two bounded runs:

| Run | Result |
| --- | --- |
| Before startup calibration | Transport/capture passed, but initial ratio rose to about +812 ppm; recorded tone about 440.427 Hz |
| After startup calibration | Clean transport/capture; ratio about -29.8 ppm at three seconds, no queue starvation/underflow/overflow; recorded tone about 440.077 Hz |

The first run exposed real startup bias and is retained as evidence, not a passing
clock-accuracy result. The corrected run consumed 143837 input frames, completed
2963 OUT batches and closed cleanly. Capture stored 441000 frames/channel at 44100
Hz with zero timestamp gaps or invalid callbacks. The controller is only beginning
to settle during this short run, so the residual frequency offset is expected and
must not be represented as validated long-duration hardware correction.

Latest hardware evidence is local under
`artifacts/native-tone-ch3-20261102-162142-189780/`: WAV, timing, quality, driver log
and run metadata. Derived records/hashes are in `RATE-MATCH-MEASUREMENTS.json`.
The quality analyzer now explicitly labels the raw USB-payload prediction as a
prediction **without resampling**, since an active rate matcher changes that tone
relationship. Capture remains noise-limited at the authorized signal level.

To reproduce an explicitly authorized bounded tone/capture test:

```sh
python3 automation/native_loopback.py --run-tone --channel 3 --source pcm-host-test
```

## Still pending

- Long-duration hardware settling and full-band resampling quality.
- Real HAL host loading, app format/IO compatibility and property IPC verification.
- A production drift/latency strategy tied to validated device timing.
- Production installation, combined MIDI/audio ownership and KORE input PCM.

The CoreAudio output plug-in remains local and uninstalled, per user instruction.
No CoreAudio restart, system audio default change or push was performed.
