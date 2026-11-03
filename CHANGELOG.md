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

## 2026-11-02 — consolidated development package

- Generate a standalone C MIDI/libusb audio bridge from maintained sources;
  `make bridge` compiles it and `--check` verifies reproducibility.
- Add a local zip with compiled bridges, native audio tools, CoreAudio prototype,
  Mixxx mappings, source and licensing, plus SHA-256 provenance manifest.
- Fix cue Note Off routing in the new Mixxx mapping, guard optional metadata API,
  and disconnect metadata callbacks during shutdown. Preserve original v10 files.
- Update build/run entry points and document transport exclusivity and unfinished
  CoreAudio integration. Packaging never installs a HAL plug-in or opens USB.

## 2026-11-02 — shared native MIDI/audio and installation implementation

- Replace the generated libusb diagnostic bridge with the native IOUSBHost
  transport plus preserved controller engine, using one USB interface owner.
- Isolate ISO, MIDI, LED/LCD and PCM prefetch work; rearm idle controller reads,
  bound report backlog and drain cancellation callbacks.
- Build executable and CoreAudio HAL plug-in from one standalone translation unit.
- Add explicit continuous operation, system install/removal/status, per-user login
  startup/removal, backup preservation and embedded bundle metadata.
- Add actual system HAL PCM integration test, final CLI safety checks and native
  MIDI callback regression tests. Live validation is recorded separately.

## 2026-11-02 — stereo headphone validation

- Add bounded left/right/dual 440/660Hz CoreAudio test patterns and all-channel
  stereo loopback analysis, including capture continuity and negative controls.
- Test the installed login service directly without a competing USB consumer.
- Add explicit finite PCM gain 0..0.001 (default .00025) for controlled measurements,
  preserving the original ceiling and normal startup level.
- Verify exact independent signed24 wire samples and record both real stereo
  measurements. Cable pinout and full quality/clock qualification remain open.

- Fix login-service reinstallation: wait for launchd bootout completion before
  bootstrap, including removal of an orphan service registration.

### Mixxx playback gain correction

- Separate application PCM gain (0..1, finite and saturated) from the capped
  diagnostic oscillator; keep the CLI quiet default and set login playback to 0.1.
- Verify bounded stereo loopbacks at 0.01 and 0.1 through the KORE headphone output
  and MAYA44 capture, with contiguous capture and no clipping.

### Four-channel Main and headphone output

- Expose Main L/R on CoreAudio channels 1/2 and Headphones L/R on 3/4 at 48kHz.
- Carry all four channels through the HAL IPC, ring, shared ASRC and native USB
  packer; reject legacy stereo IPC instead of misreading its frame layout.
- Add independent Main/headphone gain options. Login defaults: Main 0.2,
  Headphones 1. Keep diagnostic tone caps and the quiet standalone CLI default.
- Verify Main stereo at gains 0.1/0.2 against the user's known MAYA44 wiring;
  document capture-start timeout and unverified analog headphone unity level.

### Headphone diagnostics

- Add independent test-signal level and default loopback probes to 0.1 to reduce
  capture clipping; keep installed playback gain and independent cue routing.
- Add optional NumPy matched-chirp analysis and offline injected-echo, clipping,
  metadata and capture-gap controls, with dependency preflight before hardware.
- Record passed low-level headphone measurements and rejected/clipped Mixxx
  attempts; document the unconfirmed four-pole headset adapter hypothesis.

### FX/browser MIDI mapping

- Correct physical Play to note 50 and Stop to 51; Backlight 37 and knob-touch
  notes have no transport action. Select decks with Control/Sound or left/right.
- Map eight encoders and softkeys to selected-deck QuickEffect and FX controls;
  add protected track loading, PFL and cue release tracking across deck changes.
- Emit wheel CC28 from KORE's wrapped 8-bit IO counter; add native decoder and
  mapping regression tests, and install the preset in the user controller folder.

- Follow-up: left/right arrow buttons now directly load the selected library track
  into Deck A/B and select that deck, while protecting a playing destination.

### LED feedback and honest LCD fallback

- Observe selected-deck transport, PFL, FX and routing changes for bright/dim
  button feedback, including changes made in Mixxx; light momentary actions.
- Keep all eight encoder rings and jog illumination on; initialize these in the
  native bridge and the mapping. Cache unchanged feedback and clean up listeners.
- Show loaded status on Mixxx 2.5 instead of falsely implying NO TRACK. Real
  title/artist requires the later Player API; the user chose to defer that change.
- Add atomic user-bridge updates that defer restart to avoid interrupting audio.
