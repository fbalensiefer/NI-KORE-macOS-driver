# Current architecture — 2026-11-03

```text
Mixxx / CoreAudio application
  | 48kHz, four interleaved float channels
  v
KoreAudio.driver (isolated CoreAudio driver host)
  | SPSC PCM ring, PID consumer lease, kpc4 CFData property
  v
kore1-final-bridge: prefetch -> local ring -> shared four-channel ASRC
  | signed24 big-endian, paired CAIAQ streams
  v
KORE USB interface 0, alt 1: ISO IN 0x82 -> matched OUT 0x06
  | Main 1/2 / Headphones 3/4
  + bulk IN 0x81 -> controller decoding -> CoreMIDI "KORE 1 Bridge" -> Mixxx
  + bulk OUT 0x01 <- LED/LCD queue <- "KORE 1 Feedback" <- Mixxx mapping
```

One native process owns the interface. Separate serial queues handle ISO/bulk IN,
MIDI decoding/filter expiry, LED/LCD bulk OUT and PCM property prefetch. Display
transfers never sleep on the ISO queue. CoreMIDI callbacks enqueue feedback.
Property IPC, disk access and allocation are outside audio/USB PCM processing.

Continuous mode uses 128 IN / 256 OUT slots with a 128ms OUT scheduling lead;
bounded diagnostics retain 32 / 64 and 32ms. OUT lengths follow IN completion
layouts, including empty microframe slots, with explicit valid buffer offsets.
The frame timeline remains contiguous; late deadlines fail rather than silently
rebase it. Reconnect selection of USB configuration 1 occurs only if unconfigured.
Outstanding callbacks must drain or their buffers remain retained until exit.
These mechanisms do not eliminate the observed long-run deadline failures.

## CoreAudio and PCM

`coreaudio/plugin.c` exposes one four-channel, output-only 48kHz float32 stream.
The HAL uses an 8192-frame SPSC ring. The `kown` custom property grants one live
PID consumer; `kpc4` returns up to 512 four-channel frames as host-marshalled
CFData. Its distinct selector prevents old stereo clients from misreading frames.
No shared public file/socket transports PCM. CoreAudio may host the driver in an
isolated helper process, rather than directly in coreaudiod.

`coreaudio/client.h` prefetches into a native local ring. `rate_match.h` interpolates
all channels with the same fractional phase and bounded occupancy control, fades
and re-priming. `pcm_source.h` consumes one frame for both paired USB streams,
with independent Main/headphone gains and finite saturated signed24 conversion.
Diagnostic oscillator amplitude limits are separate from application playback.
Reported device latency/safety offset totals roughly 171ms. Input is not exposed.

## Controller and feedback

`src/kore-midi-v10.c` maintains the decoder. Generation removes libusb ownership
and wraps it in `audio/controller_engine.h` for the native bridge. It emits
relative CC20–27, wrapped-counter CC28, notes36–67 and analog CC40–42.
Touch notes are distinct from physical softkeys; the active mapping ignores them.

`mixxx/KORE-1.js` subscribes to Mixxx transport/PFL/FX states, sends cached
channel-16 LED brightness CCs, and uses 7-bit SysEx for LCD text. Input notes and
LED state indices are different tables. Encoder/jog lights have fixed brightness.
The user chose stable Mixxx 2.5: no Player title/artist API is available there;
LCD loaded-state fallback is intentional, not metadata inference.

## Source, installation and compatibility

`automation/generate_bridge.py` produces `kore1-final-bridge.c`. Compile as
Objective-C for the executable, or C with `KORE_HAL_PLUGIN` for the HAL. Edit
maintained modular sources and regenerate; check targets verify reproducibility.
Explicit system installation stages files outside protected Documents, preserves
backups and restarts CoreAudio. Builds do not install. The development Mac uses
an installed HAL plus a user-local login bridge; atomic deferred replacement is
available without interrupting the current process. See [setup](README-SETUP.md).

Libusb bridges, native probes and historical sources remain separate diagnostics;
never run them concurrently against the same device as the final bridge.
