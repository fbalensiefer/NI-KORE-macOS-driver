> Status reference (2026-11-03): Initial installed stereo milestone. The current device is four-channel, the login bridge is user-local, and later gains/mapping/feedback changed.
> See [current status](STATUS.md) and [documentation index](INDEX.md).

# Native MIDI/audio and system HAL validation — 2026-11-02

## Verified live

- A freshly reconnected KORE enumerated as a device without interface services.
  Native open now selects USB configuration 1 only when no configuration is active.
  The test then opened interface 0 at 480Mb/s without first running libusb.
- A three-second encoded-silence run completed with native MIDI and LED/LCD
  initialization enabled: 2,996 IN batches, 2,963 completed OUT batches, 10
  controller reports, no reported transfer failure, MIDI backlog or feedback error.
  Outstanding tail transfers were synchronously cancelled at the bounded stop.
- The HAL bundle and native executable were installed as root-owned files through
  the final executable's explicit installer. Previous bundles are preserved in
  `/Library/Application Support/KORE1/Backups`. CoreAudio was restarted.
- The system exposes `org.nikore.experimental.output`, named **KORE 1 Experimental
  Output**, fixed at 48kHz stereo. Dynamic AudioObjectIDs vary between runs.
- The real system HAL test rendered and retrieved 120,320 PCM frames through
  host-marshalled custom properties. Left-channel nonzero frames: 47,959;
  right-channel nonzero frames: zero; peak 1.0 before USB attenuation. Render,
  lease release and teardown succeeded; default output/system devices were unchanged.
- A complete application → system HAL → native PCM prefetch/rate matcher → USB
  KORE headphone → MAYA44 loopback completed. The shared bridge also processed
  32 controller reports and 207 controller OUT commands, including a MIDI SysEx
  LCD update during playback. MIDI backlog and feedback errors were zero.
- The bridge completed 2,989 OUT batches, consumed all 120,320 rendered PCM frames,
  and exited cleanly on SIGTERM. One queue underflow/starvation occurred when the
  bounded producer stopped; continuous idle output returned to silence. No local
  PCM overruns occurred. This is not a sustained-load/clock-lock qualification.
- MAYA44 direct HAL capture stored exactly 441,000 four-channel frames over ten
  seconds at 44.1kHz, with 862 callbacks, zero timestamp gaps, zero invalid
  callbacks, and clean stop/destroy.

Local evidence is ignored under
`artifacts/coreaudio-loopback-20261102-195829-407382/`. It includes bridge/render
logs, WAV, capture timestamp report and machine-readable analysis. The analysis
found the tone in capture channel 2: about 440.117Hz after host-rate correction,
fundamental about -82.86dBFS, residual about -76.39dBFS, no clipping. These numbers
support signal-path verification, not calibrated clock accuracy or DAC distortion.
The short rate-matcher startup/stop transient and low signal-to-noise ratio limit
frequency and quality conclusions.

## Continuous-service scheduling correction

The first installed login-agent run exposed a missed USB frame deadline after
85.5 seconds with the former 32ms runway. It exited cleanly and launchd restarted
it, producing a playback gap. The final continuous mode increases the bounded
pools to 128 IN / 256 OUT slots and OUT lead to 128ms; bounded diagnostics retain
the original pools. The HAL latency reflects this buffering (6,144 latency frames
plus 2,048 safety frames, about 171ms at 48kHz).

The production callback test now injects a 40ms delivery delay after streaming
begins and verifies 200 ordered continuous-mode batches without failure or frame
rebasing. The updated installed service then ran continuously for 131.4 seconds, including
bounded CoreAudio playback and an LCD update. Its PID and launchd run count
remained unchanged, with zero new error-log bytes. This verifies the longer
window but does not establish unlimited scheduling tolerance. The error log
retains the earlier 32ms-runway failure as historical evidence.

## Offline checks

`make check audio-check native-check coreaudio-check` covers:

- Standalone source regeneration and bounded CLI parsing, including explicit
  continuous operation and mutually exclusive signal modes.
- Actual native callback paths, transaction ordering/zero slots, queue exhaustion,
  submission rejection, cancellation and buffer lifetime with ASan/UBSan.
- Native MIDI report decoding into recorded note-on/note-off events, one bulk IN
  owner, idle timeout rearm, delivery while the feedback queue is blocked, shutdown.
- Actual HAL factory loading, box/device/stream publication, model metadata,
  timestamp period meeting the installed SDK's 10,923-frame minimum, custom-property
  lease behavior, PCM packing and concurrent ring use.
- Adaptive resampling simulations (15 simulated minutes over five clock offsets),
  capture sample accounting, gap refusal and synthetic tone-analysis controls.
- Mixxx mapping XML and mocked controls, cue release, feedback bounds and cleanup.

Both compile modes were also built from the generated file copied outside the
repository. The executable needs `-x objective-c` for the public IOUSBHost API;
the HAL mode remains C. Neither requires project-local headers at compilation.

## Limits

This verifies simultaneous native MIDI/audio and system CoreAudio integration at
the development gain ceiling. Physical knob/button manipulation and Mixxx GUI
endpoint routing were not exercised. CoreAudio currently exposes the verified
stereo logical output pair 3/4, with .00025 peak attenuation. KORE audio input,
full four-channel CoreAudio output, full-level listening/distortion measurements,
long-run drift lock, unplug/sleep recovery under load and resampling quality
qualification remain separate work. Do not interpret this integration milestone
as certification of a production sound card driver.

The user authorized installation in the later request, superseding the earlier
instruction to keep the plug-in local. No SIP, driver-signing policy or audio
default setting was weakened/changed to make this work.

## Installed startup

The final executable is installed at
`/Library/Application Support/KORE1/kore1-final-bridge`; its SHA-256 matches the
built executable. The per-user `org.nikore.bridge` LaunchAgent is installed and
starts CoreAudio PCM + native MIDI + feedback immediately and at GUI login.
A bounded render and MIDI LCD update through the final 128ms-runway service
completed with 120,320 rendered frames and zero render/feedback status errors.
Use `--uninstall-login` before manually opening another KORE process or running
an exclusive PCM-consumer test.
