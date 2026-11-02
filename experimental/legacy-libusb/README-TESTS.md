# CAIAQ audio initialization / ISO diagnostic patch

This is an **experimental diagnostic snapshot**, not a verified audio driver.

Changes:
- Corrects AUDIO_PARAMS command to 6 bytes: `09 01 02 B0 00 01` at 48 kHz / 24 bit / 176 bpp.
- In RX-matched OUT mode, skips batches containing any zero-length ISO packet descriptor; this is a diagnostic guard, not a final scheduling solution.
- Does **not** alter fixed-length OUT mode, which may still report `LIBUSB_ERROR_INVALID_PARAM` for other reasons.
- Keeps existing MIDI/LED/LCD logic from the bundled source snapshot.

Run `sh test-audio-offline.sh` to run source assertions and compile on macOS (requires `libusb`, `pkg-config`).
Run `sh test-audio-hardware.sh` for the short live diagnostic. Stop with Ctrl+C. Inspect `logs/audio-*.log` for firmware ACK, IN packet lengths, OUT submit errors, and statistics.

**Important:** The bundled source comes from the existing experimental repository update snapshot, not a fresh read of your current Mac working tree. Compare it with your current `kore1-bridge.c` before replacing anything, especially if you've added local features since that snapshot. Back up your file first. Avoid headphones/speakers during diagnostic runs.
