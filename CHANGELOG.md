# Changelog

## 0.10.0 — 2026-11-01

- Baseline repository for KORE 1 macOS CoreMIDI bridge.
- Preserve v6 encoder filter and ERP4 spike guard.
- Include USB LED and LCD diagnostics previously validated on KORE 1.
- Add experimental CoreMIDI feedback destination and Mixxx JavaScript/XML mapping.
- Add documentation, build target, static tests and licensing/provenance notes.

Prior development occurred as standalone `kore-midi-v3` through `v9` prototypes and is not represented as separate Git commits here.

## Unreleased — 2026-11-02

- Add native IOUSBHost transport, explicit zero-length transaction offsets,
  bounded RX-driven silence streaming and fixed scheduling lead.
- Verify repeated three-second streams, early cancellation, reopening and
  low-level analog tone playback through the confirmed KORE/MAYA44 connection.
- Add single-channel signed24 tone generation with fades and a hard amplitude cap.
- Fix native discovery, speed validation, stale command callbacks and late
  cancellation draining; retain existing controller code and compatibility bridge.
- Add deterministic PCM/layout, deferred callback, sanitizer and capture-analyzer
  regression checks. Document remaining capture discontinuities and missing
  CoreAudio, application PCM feed and audio-input support.
- Organize historical experiments with a path/hash manifest; keep binaries,
  recordings, logs and the local Actions runner out of version control.

### Follow-up: capture continuity and local CoreAudio prototype

- Replace the measurement default with direct HAL input capture using preallocated
  storage, exact sample counts and timestamp continuity checks. Repeated ten-second
  captures contain all 441000 frames with zero gaps.
- Add short host-relative clock and residual-noise analysis; KORE tone/payload
  measurements agree on an approximately +200 ppm device clock offset.
- Build and test a local signed AudioServerPlugIn output bundle and capped native
  PCM feed; leave it uninstalled per user instruction. Sustained drift handling
  and actual CoreAudio host integration are still pending.

### Follow-up: adaptive rate-matching prototype

- Add bounded PCM interpolation and occupancy control, startup calibration, fades
  and starvation recovery without changing USB packet lengths/timing.
- Verify 15 simulated minutes over +/-500 ppm and a capped host-paced PCM hardware
  test with gap-free capture. Keep CoreAudio uninstalled and distinguish these
  results from sustained hardware clock lock or full-band audio quality.
