# Local CoreAudio output prototype — 2026-11-02

## Current state

An AudioServerPlugIn bundle now builds at `build/KoreAudio.driver`. It is locally
ad-hoc signed and can be loaded through its actual CFPlugIn factory in an offline
test. **It is not installed or registered with CoreAudio.** The user explicitly
chose to keep it local and finish offline verification. No system audio service
was restarted and no default audio selection was changed.

Apple's [AudioDriverKit sample](https://developer.apple.com/documentation/audiodriverkit/creating-an-audio-device-driver)
points virtual-device implementations toward an Audio Server Driver Plug-in.
The prototype follows the installed SDK's `AudioServerPlugIn.h` ABI; see Apple's
[plug-in documentation](https://developer.apple.com/documentation/coreaudio/creating-an-audio-server-driver-plug-in).
A DriverKit hardware extension would have additional entitlement/deployment
requirements; none is created or installed here.

## Implemented path

```text
Application → KORE 1 Experimental Output (virtual 48k stereo float32)
            → fixed SPSC ring in the local AudioServerPlugIn
            → host-marshalled CFData custom property, exclusive consumer PID lease
            → non-realtime prefetch queue in native USB bridge
            → local SPSC ring → capped S24_3BE packer → logical outputs 3/4
```

The virtual device publishes one fixed-rate 48000-Hz stereo output stream,
identity/format/latency/clock properties, IO start/stop and mixed-output handling.
It advertises no input stream and cannot become the default/system output
automatically. The audio IO callback only copies into a preallocated bounded
ring: no allocation, locks, property IPC, disk IO or USB operations there.

The bounded ring uses C11 acquire/release atomics with one producer and one
serialized consumer. Full blocks are rejected and overrun-counted if capacity
is exceeded; no live data is overwritten. Custom properties use CFPropertyList
marshalling with CFData PCM and CFBoolean ownership. The lease serializes readers
and rejects a different client PID. This is an experimental protocol, not a
production security/deployment review.

The native `--coreaudio-pcm` path queries the experimental device UID, claims
the lease before opening KORE USB, and pulls PCM on a separate non-realtime queue.
It applies a fixed .00025 gain and ceiling before signed24 encoding, routes
stereo to logical outputs 3/4, substitutes silence on underrun and counts it.
A missing/unavailable plug-in causes exit before USB access. The test remains
bounded to at most three seconds. It excludes synthetic layouts, silence mode
and the built-in tone. This path has been tested through its components offline,
not through a live CoreAudio server.

The custom property selectors are `kown` (CFBoolean lease) and `kpcm` (CFData
up to 512 stereo frames). No public shared-memory file, socket or system library
patch is used. PCM is prefetched outside the USB callback to avoid synchronous
CoreAudio IPC on its scheduling queue.

`coreaudio/render_test.m` is a test client which selects only the experimental
UID, never the default output. Its full-scale virtual tone is attenuated to
.00025 peak by the native bridge. It has not been run against the system host.

## Build and offline checks

```sh
make hal-capture native-probe coreaudio-prototype
make check audio-check native-check coreaudio-check
```

These commands do not install the driver or play audio. Verification includes:

- Actual signed bundle loading through CFPlugIn factory, ABI and device UID.
- Device and stream properties, input absence, fixed format and clock callbacks.
- Exclusive property ownership, invalid data and PCM delivery to the real packer.
- 100000 producer/consumer frames across a threaded ring with wraparound.
- Signed24 mapping and hard output cap for the native PCM source.
- Direct capture sample accounting and timestamp-gap refusal.
- Existing USB pools, cancellation, tone and controller regression tests.

`codesign --verify --strict build/KoreAudio.driver` and `plutil -lint` pass.
The bundle is a local development artifact, not a notarized distributable driver.

## Pending host verification and constraints

Loading the prototype into the real HAL would require approved installation into
`/Library/Audio/Plug-Ins/HAL/` and a CoreAudio restart, which interrupts Mac audio.
That action was declined for this session and was not attempted. Local bundle
loading does not prove real HAL enumeration, app compatibility, property IPC
marshalling or end-to-end app-to-USB playback; these remain to be tested.

The plug-in clock currently follows the host at a nominal 48000 Hz. The measured
KORE payload/sample clock is about +200 ppm relative to the host in short tests.
Buffering alone cannot absorb this indefinitely: implement and validate drift
management or device-clock coupling before enabling sustained general playback.
No adaptive resampling or long-duration playback claim is made here.

The compatibility MIDI bridge and native audio process still cannot share USB
interface 0. Combined controller/audio ownership, KORE input PCM capture and a
production installation/upgrade/removal workflow remain future work.
