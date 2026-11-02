> Historical automation setup package. The current repository entry points are
> documented in README.md and TESTING.md; the old self-hosted runner setup below
> is optional historical guidance, not required for native audio development.

# NI KORE 1: automated macOS test runner

Target repository: https://github.com/fbalensiefer/NI-KORE-macOS-driver

This is an **additive** package: copy `.github/` and `automation/` into your existing repository. It does not replace the working MIDI source. It does **not** implement or fix the KORE audio driver. It creates a reproducible test harness.

## 1. Prerequisites (one-time on the Apple Silicon Mac)

- macOS, Xcode Command Line Tools: `xcode-select --install`
- Homebrew: `brew install libusb pkg-config ffmpeg`
- NI KORE 1 connected over USB.
- A real **stereo** audio capture device. Important: a 3.5 mm socket may be headphone OUTPUT only, or a mono headset microphone INPUT. Verify it can capture stereo line-level audio. A USB audio interface with stereo line inputs may be necessary.
- A suitable attenuated stereo cable from KORE headphone OUT to audio interface LINE IN; keep KORE output at minimum initially. Never connect headphone output directly to an unverified mic input.
- Do not wear headphones during initial experiments.

## 2. Copy into the repository

```bash
git clone https://github.com/fbalensiefer/NI-KORE-macOS-driver.git
cd NI-KORE-macOS-driver
git switch -c feature/kore-automated-hardware-tests
# Copy the automation/ and .github/ directories from this ZIP into this checkout.
brew install libusb pkg-config ffmpeg
bash automation/local_test.sh
python3 automation/test_analyzer.py
git add automation .github
git commit -m "Add Apple Silicon KORE CI and opt-in loopback measurement"
git push -u origin feature/kore-automated-hardware-tests
```

Open a PR to main; review before merging. If the repository already has workflows, keep those files and add the new workflow filenames only.

## 3. Register a dedicated self-hosted runner

On GitHub: Repository → Settings → Actions → Runners → New self-hosted runner → macOS ARM64.
Follow the **current commands displayed by GitHub** to download/configure the runner. Use a dedicated non-admin macOS account; restrict access to this repository. During configuration, add the label `kore1-hardware`. Install the runner as a service using GitHub's displayed instructions, if desired.

**Security**: a self-hosted runner executes repository workflow code on your Mac. Do not enable untrusted fork PRs or unrestricted third-party workflows on the hardware runner. The provided hardware workflow is manual (`workflow_dispatch`) and uses only read permissions. Keep the machine isolated from personal files and secrets. Review proposed code before execution; do not automatically merge agent PRs.

## 4. Discover your real audio input

```bash
ffmpeg -f avfoundation -list_devices true -i ''
```

Find the index in the **audio devices** section (not the video devices section). Test a short capture manually:

```bash
KORE_AUDIO_INPUT_INDEX=2 bash automation/capture_loopback.sh artifacts/manual.wav
```

Replace `2` with your actual audio device index. Listen to the captured WAV only at low volume; inspect input channels in macOS Audio MIDI Setup. Check that both left/right channels are present.

In GitHub: Repository → Settings → Secrets and variables → Actions → Variables:
- `KORE_AUDIO_INPUT_INDEX`: numeric index of the stereo input device
- `KORE_HARDWARE_TEST_ENABLED`: `YES` **only after** the input and safe signal level have been verified.

These are repository **variables**, not credentials.

## 5. Run a test

Actions → `KORE static CI` runs on pushes/PRs with GitHub-hosted macOS.
Actions → `KORE hardware test (manual)` → Run workflow.

The hardware job deliberately **does not start experimental USB audio output**. It records the connected input for five seconds, expecting an externally started low-level 440 Hz stereo sine. Until we have a bounded, reviewed, safe playback command, this avoids repeating the high-frequency noise from earlier experiments. No physical sound means the measurement correctly fails.

The analyzer accepts a 48 kHz, stereo, 16-bit WAV, measures RMS, peak and approximate tone frequency, and emits `artifacts/audio-report.json`. A pass demonstrates a 440 Hz signal on the capture input; it does **not by itself** prove that the KORE USB protocol is correct unless the physical input is demonstrably connected to the KORE headphone output.

## 6. Next step for actual autonomous development

1. Implement deterministic CAIAQ packet-unit tests based on the original Linux `snd-usb-caiaq` implementation.
2. Build a bounded USB playback test with explicit 2-second maximum, zero/default volume, and kill-on-error.
3. Connect that command to `hardware_test.sh` only after manual safety review.
4. Configure a coding agent to create PRs from failing CI reports; never let untrusted PR code run directly on your hardware runner.
5. Add audio output measurement and regression thresholds once the first verified waveform is obtained.

**Limits**: GitHub Actions schedules/executes tests, but does not itself write code or independently repair the driver. Autonomous coding requires a separately configured agent with repository permissions. The Mac must remain powered on, awake, network-connected and attached to the KORE 1.

## Source code licensing

Retain the repository's original license and copyright notices. Any port of Linux CAIAQ code must preserve applicable GPL obligations and attribution.
