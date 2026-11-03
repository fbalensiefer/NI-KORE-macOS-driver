# Consolidated status — 2026-11-03

This is the current reference. Dated experiments retain the claims and defaults
that applied when measured; they do not override this page or the setup guide.

## Feature and evidence summary

| Area | Current state | Evidence/limit |
|---|---|---|
| USB owner | Native IOUSBHost, shared MIDI/audio | Single interface-0 owner; legacy probes must run separately |
| CoreAudio | Installed output HAL 0.2.0; 48kHz, four float channels | Actual format query and bounded analog loopbacks passed |
| Main | Channels 1/2, software gain 0.2 | Reboot follow-up: L -7.89 / R -8.56dBFS, contiguous ten-second capture, no clipping |
| Headphones | Channels 3/4, gain 1, independent PFL | Level-0.1 line probe around -10dBFS; no clipping in valid capture |
| Resampling | Four channels share occupancy-controlled linear ASRC | 15 simulated minutes at up to +/-500ppm; long-run hardware qualification open |
| Controller | Relative encoders CC20–27, jog CC28, notes36–67 | User confirmed corrected Play/Backlight and browser operation |
| Track loading | Left -> A, right -> B; Enter -> selected deck | Playing target is protected; subsequent user acceptance recorded |
| FX | Selected-deck QuickEffect and two FX units | No initial forced effect/volume/routing changes |
| Feedback | Bright/dim states; fixed encoder/jog lights | Installed, software tests pass; user accepted the update |
| LCD | Deck, Play/Stop, BPM, TRACK LOADED / NO TRACK | Real title/artist deferred on stable Mixxx 2.5.6 |
| KORE inputs | Not implemented | MAYA44 input capture does not add KORE input support |

## Current deployment on the development Mac

- HAL: `/Library/Audio/Plug-Ins/HAL/KoreAudio.driver`, bundle version **0.2.0**.
- Device UID: `org.nikore.experimental.output`; AudioObjectID is dynamic.
- Executable: `~/Library/Application Support/KORE1/kore1-final-bridge`.
- Agent: `~/Library/LaunchAgents/org.nikore.bridge.plist` (`org.nikore.bridge`).
- Arguments: `--run --coreaudio-pcm --continuous --mixxx-feedback --main-gain 0.2 --headphone-gain 1`.
- Latest installed next-start executable SHA256:
  `2cfe72f42ae554d4ec1ecfd02d83c1fecbcf39302a39b88608dcf72427f3dbec`.
- The last update used atomic deferred replacement: that verifies the file on
  disk, not which binary an already running process has mapped. Mapping reload
  activates script LED initialization; native defaults apply on service restart.
- Active preset: **KORE 1 FX + Browser**, files `KORE-1.midi.xml` / `KORE-1.js`
  in Mixxx's sandboxed user `controllers` directory. The application-bundle v10.1
  preset and archived root mappings are no longer the active configuration.

The user-local deployment avoids the repeatedly invisible administrator dialog
for bridge-only updates. It retains the installed system HAL and audio gains.
Agent, preset and binary backups were made. Re-running an older system binary's
`--install-login` would point the agent back at that older binary; update the
system package first or continue using the user-local helper.

## Decisions and unresolved findings

1. Keep **stable Mixxx 2.5.6**. The user declined the separate 2.7-alpha metadata
   test. Its checked download was removed without installation or launch.
2. Keep **independent headphone cueing**; do not mirror Main onto headphones.
3. Keep measured Main/headphone gains separate; raising digital gain is not a
   remedy for an unverified headset contact problem or ADC clipping.
4. The user confirmed a four-pole headset through an adapter. A compatible
   splitter/conventional TRS headphone comparison is still needed. Neither a
   cable fault nor a fully qualified headphone-load response has been established.
5. USB scheduling deadline failures/restarts still occur. A startup CoreAudio
   initialization block cleared after a Mac reboot; its cause was not isolated.
6. Direct HAL capture improves accounting but later gap/clipping failures still
   occurred. Reject invalid runs rather than presenting them as quality passes.
7. Full-band ASRC evaluation, sustained clock accuracy, KORE capture input and
   production packaging remain open. Existing licensing review requirements in
   NOTICE.md remain unchanged.

## Strongest recorded checkpoints

- Main after reboot: `coreaudio-loopback-20261102-212917-381219`, 441000 capture
  frames, no clipping, unchanged service during the measured interval.
- Headphone low-level stereo: `...215351-436229`, no gaps/clipping; channel
  separation recognized. Actual physical volume had been turned up, so earlier
  analog levels are not a fixed-gain calibration.
- Headphone chirps: `...215709-303461`, strongest delayed matched correlation
  around -57dB in the 5..250ms window; not proof that every audible artifact is absent.
- Clipped Mixxx chirp attempts are excluded; one generated track had +15.03dB
  ReplayGain. They do not establish clean Mixxx program-material quality.

See [stereo results](STEREO-OUTPUT-RESULTS.md),
[headphone investigation](HEADPHONE-DIAGNOSTICS.md),
[mapping/feedback guide](MIXXX-FX-MAPPING.md), and [tests](../TESTING.md).
No new hardware measurements were performed for this documentation consolidation.
