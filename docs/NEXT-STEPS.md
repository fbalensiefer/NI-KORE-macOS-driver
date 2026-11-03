# Remaining work — 2026-11-03

Implemented integration and completed milestones are in [STATUS](STATUS.md).
CoreAudio is installed; one native owner already handles MIDI/audio together.
The earlier offline-only installation restriction was superseded by explicit
installation authorization and is not the current plan.

1. **Fix sustained USB scheduling failures.** Preserve evidence of deadline
   misses and service restarts; distinguish host stalls, device clocks and callback
   latency. Do not hide misses with timeline rebasing or silently dropped packets.
2. **Qualify sustained PCM/ASRC and audio quality.** Short captures and simulated
   clock offsets are not long-run hardware lock, distortion or full-band DSP tests.
   Validate the linear interpolator and latency before claiming production quality.
3. **Finish loaded-headphone diagnosis.** The user confirmed a TRRS headset and
   adapter. Compare a proper TRS headphone/compatible splitter. Treat a contact
   fault as a hypothesis, not an established repair; account for physical gain.
4. **Keep measurement rejection strict.** Direct HAL capture still occasionally
   reports gaps; full-level and ReplayGain-boosted probes clipped. Record test
   level and physical routing, and exclude invalid runs.
5. **Implement KORE audio input.** First validate its framing/check state and
   clock handling. MAYA44 input is only the external measurement path.
6. **Review release packaging/provenance.** Current builds are local, architecture-
   specific development artifacts; signing/notarization and the existing licensing
   review in NOTICE.md remain distinct from successful build checks.

## Explicitly deferred

The user chose to remain on stable **Mixxx 2.5.6** and defer real LCD title/artist
metadata. Do not upgrade Mixxx or migrate its library as an assumed next step.
The prepared 2.7-alpha download was removed without installation/launch. Keep
independent headphone cueing, the current FX/browser layout and protected loading.

## Verification to retain

User confirmation covers working Main/headphone playback and corrected MIDI
transport/browser operation. LED changes were accepted after installation;
individual light positions/brightness are not a calibrated hardware specification.
Recheck any changed physical mapping and distinguish software regression tests
from actual user/hardware observations. See [mapping guide](MIXXX-FX-MAPPING.md).
