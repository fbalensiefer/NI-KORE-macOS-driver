# Next development steps

As of November 2, 2026, repeated native streaming and analog playback are verified.
Direct HAL MAYA44 capture now stores exactly ten seconds with zero timestamp gaps.
Short relative measurements suggest a KORE clock about +200 ppm versus the host.
See [capture/clock evidence](CAPTURE-CONTINUITY.md).

1. **Keep the CoreAudio output prototype local**, as requested. A signed bundle,
   PCM property IPC, realtime-safe queue, native prefetch/packer and render client
   build and pass offline tests. Real HAL loading and application compatibility
   remain unverified; installation requires fresh approval.
2. **Implement drift handling before sustained app playback.** The current virtual
   device follows nominal 48 kHz host time; KORE payload timing differs slightly.
   Bounded three-second tests do not establish indefinite clock matching.
3. **Improve analog measurement SNR.** At the authorized low level, residual noise
   is about 6 dB above tone RMS. Do not infer a DAC distortion/dynamic-range spec
   from that measurement or automatically alter physical gain.
4. **Validate host integration and routing** when installation is authorized. Use
   the gap-free recorder and the capped virtual-output render test. Confirm app
   format negotiation, underrun/overflow behavior, timestamps and clean teardown.
5. **Unify interface ownership** before simultaneous MIDI and audio; native USB
   and the compatibility libusb controller still require exclusive interface 0.
6. **Implement KORE input PCM capture** only after validating its alignment/check
   state. MAYA44 capture is a measurement path, not KORE audio-input support.

The user authorized bounded KORE silence/tone tests and MAYA44 recording. They
chose not to install or restart CoreAudio for the new output prototype. No CoreAudio
server change has been made.
