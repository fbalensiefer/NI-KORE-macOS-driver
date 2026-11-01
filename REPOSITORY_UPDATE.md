# Experimental CAIAQ audio / MIDI update

This is an additive update for `fbalensiefer/NI-KORE-macOS-driver`. It deliberately leaves `src/`, `mixxx/`, the existing `Makefile`, and the existing README unchanged.

The experimental integrated bridge is under `experimental/caiaq-audio-v1/`. The original MIDI driver remains the recommended working version.

## Install with Git

```bash
git clone https://github.com/fbalensiefer/NI-KORE-macOS-driver.git
cd NI-KORE-macOS-driver
git switch -c feature/experimental-caiaq-audio-v1
# Copy the experimental/ directory and REPOSITORY_UPDATE.md from this archive into the repository.
git add experimental/caiaq-audio-v1 REPOSITORY_UPDATE.md
git commit -m "Add experimental CAIAQ stereo audio bridge with CoreMIDI"
git push -u origin feature/experimental-caiaq-audio-v1
```

Then create a pull request in GitHub from that branch into `main`.

## Warning

USB audio playback remains **hardware-unverified**. This is NOT a working native CoreAudio device driver. The optional audio path requires BlackHole 2ch, and prior tests produced high-pitched noise. Keep volume minimal. No audio input from KORE is implemented. The standalone original MIDI mapping stays untouched.

## Build

```bash
cd experimental/caiaq-audio-v1
brew install libusb pkg-config
./build.sh
./run-midi.sh
```

Use `run-audio.sh` only after reviewing `experimental/caiaq-audio-v1/README.md`.
