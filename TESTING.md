# Testing and validation

## Static checks (any host with Python 3)

```bash
make check
```

## Native compile (macOS only)

```bash
brew install libusb pkg-config
make clean all
```

## KORE 1 hardware smoke tests

Run exactly one bridge process at a time.

1. `./build/kore-midi-v10 --led-list` (no USB device required).
2. `./build/kore-midi-v10 --led-test play --led-level 35` (verified previously on KORE 1).
3. `./build/kore-midi-v10 --lcd-test --lcd-contrast 45 --lcd-backlight 65` (verified previously on KORE 1).
4. `./build/kore-midi-v10 --erp-threshold 8 --erp4-spike-guard --verbose` and test all eight encoders and buttons.
5. For experimental integration, start with `--mixxx-feedback` and verify MIDI output routing to `KORE 1 Feedback` independently of input mapping.

## Known limits

- Not validated on KORE 2.
- No CI macOS compiler/hardware validation yet.
- Not all 32 button labels verified.
- Jog-wheel raw input detected but MIDI output not complete.
- Mixxx feedback routing and live LCD updates require real-world validation.
