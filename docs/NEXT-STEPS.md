# Next development steps

As of November 2, 2026, native silence streaming and repeated near-440 Hz analog
playback are verified on the connected KORE1. See
[TONE-PLAYBACK-RESULTS.md](TONE-PLAYBACK-RESULTS.md) for evidence and limits.

1. **Fix measurement continuity.** AVFoundation ten-second captures contain about
   9.1–9.25 seconds of stored PCM. Establish a capture path with reliable sample
   counts/timestamps before assessing clock accuracy, dropouts or distortion.
2. **Validate audio quality and routing.** Repeat bounded isolated-channel tests
   with the verified capture path. Current headphone-chain evidence maps logical
   output 3 to MAYA44 input 2 and output 4 to input 1; physical jack assignments
   and other output paths are not yet established.
3. **Add a bounded PCM application feed.** Keep native USB scheduling independent
   of file/FIFO blocking, handle partial frames and underflow explicitly, and test
   rate matching. The archived FIFO experiment is not a finished playback feed.
4. **Unify interface ownership.** The native audio prototype and libusb controller
   bridge cannot run together. Preserve working MIDI/LED/LCD while implementing
   a shared transport before claiming simultaneous audio/controller support.
5. **Design CoreAudio integration.** Research current supported APIs, signing and
   deployment requirements. No HAL driver or system audio device is installed or
   implemented by the current prototype.
6. **Investigate KORE audio input capture.** Device-reported inputs and completed
   ISO IN transfers alone do not establish usable captured PCM.

Offline changes and checks do not require hardware. Live tests require a verified
physical setup and explicit authorization; keep silence and tone tests distinct.
Never treat USB completion alone as proof of analog output or audio quality.
