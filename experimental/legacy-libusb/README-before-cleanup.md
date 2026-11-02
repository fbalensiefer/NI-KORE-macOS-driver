# KORE 1 offline PCM audit

This is a **non-hardware** diagnostic. It inspects known framing patterns in the current C source and calculates sample rates. It does **not** claim Linux parity or audible output.

```bash
cd ~/Documents/NI-KORE-macOS-driver
python3 audit_pcm.py kore1-bridge-single176.c
```

Important: 96 bytes/microframe corresponds to 48k stereo frames/s per stream under the assumed two-stream, alignment-2 layout. A constant 176 bytes/microframe corresponds to 88k frames/s per stream and is not a correct 48kHz continuous payload under that model. Actual Linux scheduling and USB descriptors must be verified before hardware changes.

Do not run another tone test solely based on this calculation.

## Validated audio diagnostic target

Build and run hardware-free checks with `make audio check audio-check`. The new
`build/kore1-bridge-audio` preserves the existing controller implementation and
adds bounded fixed-96 silence, validated PCM/ISO layouts and failure diagnostics.
See [protocol findings and safe hardware commands](docs/AUDIO-VALIDATION.md).
Hardware playback and the cause of intermittent IOKit submission failures remain
unverified. No hardware test is started by the build or checks.
