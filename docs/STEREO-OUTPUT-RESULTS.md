# Stereo output validation — 2026-11-02

The KORE headphone output was connected to the MAYA44 analog inputs. The test
selected only the installed KORE CoreAudio device and retained the existing audio
defaults. The cable pinout is unknown, so these results identify virtual-channel
to capture-channel paths without asserting physical headphone tip/ring labeling.

## Test and evidence

The bounded five-second pattern contains 0.5s silence, one second of left-only
440Hz, 0.5s silence, one second of right-only 660Hz, 0.5s silence, one second of
both tones, then 0.5s silence. Each tone has 5ms fades. Ten seconds of four-channel
MAYA44 input are recorded with sample/timestamp accounting. A MIDI LCD update runs
concurrently.

| USB PCM gain | Virtual channel | MAYA44 capture input | Tone peak estimate | Separation from other input | Both versus solo |
| --- | --- | --- | --- | --- | --- |
| .00025 (default) | Left, 440Hz | 2 | -79.28dBFS | 17.65dB | -0.91dB |
| .00025 (default) | Right, 660Hz | 1 | -79.24dBFS | 19.25dB | -0.76dB |
| .001 (bounded test) | Left, 440Hz | 2 | -68.09dBFS | 26.50dB | -0.16dB |
| .001 (bounded test) | Right, 660Hz | 1 | -68.01dBFS | 32.47dB | -0.18dB |
| .00025 (final installed build) | Left, 440Hz | 2 | -71.29dBFS | 23.84dB | -0.21dB |
| .00025 (final installed build) | Right, 660Hz | 1 | -71.15dBFS | 29.36dB | -0.21dB |

All three recordings contain exactly 441,000 frames at 44.1kHz, zero timestamp gaps,
zero invalid capture callbacks, and no clipped samples. Both solo paths and the
dual-channel window were detected. No active-window amplitude dips below 70% of
the median were detected at the analyzer's 250ms window/125ms hop resolution.
This does not rule out shorter glitches.

The default-gain test used the installed login service directly, without starting
a second USB owner. Its PID/run count and error-log size stayed unchanged during
the test. The higher-gain run briefly stopped that agent, used the rebuilt final
bridge under bounded supervision, and restored the agent at the previous default
gain afterward. The higher-gain bridge consumed 240,128 frames, had no PCM
overruns, reported `failed=0`, MIDI backlog zero and feedback error zero, and exited
cleanly. One queue starvation followed the finite render stopping.

Local ignored evidence:

- `artifacts/coreaudio-loopback-20261102-202230-968034/` — installed service, .00025.
- `artifacts/coreaudio-loopback-20261102-202658-667671/` — supervised bridge, .001.
- `artifacts/coreaudio-loopback-20261102-204733-838625/` — final installed build, .00025.

The second measurement's stronger signal yields appreciably higher apparent
separation. The first low-level result therefore cannot be treated as a reliable
DAC crosstalk rating. The remaining noise, unknown cable pinout and uncalibrated
ADC path still prevent a full stereo-quality qualification. No automatic channel
swap was made. At .001, the two measured solo levels differ by about 0.08dB across
this whole loopback path, not just the KORE DAC. The final installed build also
passed both solo paths and the dual window with no new service errors or restart.
The same default digital gain yielded a higher recorded level after system
restarts; analog/ADC gain settings were not calibrated or continuously monitored.
The cause of that level difference is open. Do not infer fixed analog gain or
absolute crosstalk specifications from comparisons between these sessions.

## Reproduce

With the installed login bridge running at the default gain:

```sh
python3 automation/coreaudio_loopback.py --run-tone --stereo-test --use-login-bridge
```

The helper verifies that the service stays in the same process and writes no new
errors. The recording is refused if capture continuity is invalid. The analyzer's
15dB separation threshold recognizes the stereo signature; it is not a production
quality target. Reports explicitly mark audio quality as unqualified.

For the .001 test, stop the login agent, run the supervised test, then restore the
agent even if a test fails:

```sh
./build/kore1-final-bridge --uninstall-login
python3 automation/coreaudio_loopback.py --run-tone --stereo-test --pcm-gain .001
./build/kore1-final-bridge --install-login
```

Use `try/finally` when automating that sequence. Keep speakers/headphones
unplugged and output gain low. The helper never changes the gain of an already
running login service. `--pcm-gain` is accepted only with `--coreaudio-pcm`, must be
finite and between 0 and .001, and leaves the default at .00025.

## Offline verification

- A signed24 wire decoder checks exact independent left/right samples after real
  CAIAQ packing, including zero-length slots, inactive output streams, both bounded
  gains and non-finite input suppression. No digital channel mixing was found.
- The bounded signal generator is checked for channel-specific silence, frequency,
  active duration, fade windows and amplitude.
- Synthetic recordings verify swapped capture paths and reject missing channels,
  duplicated mono, an injected dropout and discontinuous capture metadata.

The [Linux CAIAQ driver](https://github.com/torvalds/linux/blob/v6.12/sound/usb/caiaq/audio.c)
specifies signed24 big-endian PCM and playback byte position 4 for alignment 2;
these conventions remain unchanged. Physical channel labeling, full-level quality,
settled clock accuracy and lower output latency remain separate validation work.

## Login-service restart correction

The system update exposed a launchd registration race when restart attempts
coincided: `bootout` can return while the old service is still registered as
`SIGTERMed`, making immediate `bootstrap` fail with EIO. The installer now waits
up to five seconds for the registration to disappear before bootstrapping.
Two consecutive live re-install/start attempts both reached native stereo-stream
readiness successfully. The normal PCM gain remains .00025.

## Mixxx inaudible-output correction (2026-11-02)

Mixxx successfully opened the virtual device, but the installed bridge still used
0.00025 (-72 dB) gain. The diagnostic tone amplitude cap also incorrectly limited
application PCM gain to 0.001. Playback now has its own finite 0..1 gain and
signed-24-bit saturation; diagnostic oscillators retain their 0.001 cap. The CLI
keeps its quiet default; the login installer explicitly selects 0.1 (-20 dB).
The local, closed Mixxx configuration was backed up and set to 48000 Hz. Its log
showed successful 44100 Hz opens, so that rate setting alone was not proven to
cause silence.

Two bounded stereo loopbacks with the headphone output connected to MAYA44 passed:

| Gain | Artifact suffix | Left/right measured peak dBFS | Capture |
|---|---|---|---|
| 0.01 | 20261102-210351-905037 | -50.95 / -50.72 | 441000 frames, contiguous, no clipping |
| 0.1 | 20261102-210417-317504 | -31.00 / -30.72 | 441000 frames, contiguous, no clipping |

The 20 dB input change produced approximately 20 dB measured change. At 0.1,
solo/dual levels agreed within 0.01 dB. This verifies bounded system-renderer
playback, not actual Mixxx program material or long-run USB scheduling stability.
The existing service has still shown deadline failures and automatic restarts.

The updated binary was installed and its SHA256 matched the build. The login
agent was regenerated with gain 0.1. Its first post-installation test
(`20261102-210525-339641`) failed: left solo/dual difference 3.31 dB and suspect
low-amplitude windows on both channels, despite contiguous capture and no
service restart. Do not discard that failure or infer its cause from timing alone.
The repeat (`20261102-210547-725372`) passed: left/right -31.00/-30.72 dBFS,
solo/dual within 0.003 dB, zero suspect windows, no clipping, all 441000 frames,
and unchanged PID 53412/run count 1. Installation is verified; sustained audio
quality and actual Mixxx playback still require validation.
