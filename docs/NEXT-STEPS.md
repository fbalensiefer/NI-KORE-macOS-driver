# Reproducible next steps / handoff

1. **Preserve the exact current Mac working tree.** It contains local v0.23+ diagnostics absent from the public GitHub source. Commit it on a dedicated branch before attempting merges. Keep historical experiment snapshots separate.
2. **Correct misleading reporting only:** replace the phrase `short packets are ignored by Linux PCM decoder` with `packets shorter than configured bpp (diagnostic classification only)`; do not alter USB behavior as part of this wording fix.
3. **Review Linux state transitions:** compare `snd_usb_caiaq_pcm_prepare()`, `stream_start()`, `read_completed()`, and `read_in_urb_mode2()` against the bridge. Document any actual differences before patching.
4. **Passive packet diagnosis:** capture raw first 64 IN packets with sequence numbers, timestamps, packet statuses and lengths. Test candidate checkbyte interpretations offline; do not treat unverified bits as firmware panic indicators.
5. **Validate scheduling with a USB bus trace** if tooling permits; libusb callback intervals do not prove device-visible microframe placement.
6. **Verify analog routing** independently and with safe gain, ideally through a known-good Linux CAIAQ baseline on the same hardware. Do not run loud or prolonged test tones.
7. **Only after establishing a working PCM path**, integrate BlackHole/FIFO audio robustly, add drift management and tests, then consider CoreAudio HAL support.

## Suggested smoke tests (macOS)

```sh
./build.sh
make check
./run-midi.sh
```

Use audio diagnostics only after inspecting the available flags in the *current* local source. The research runs used options such as `--audio-duplex-silence --audio-linux-urbs --audio-hex-probe --audio-report-ms 1000`. These are **diagnostic**, not proof of playback. Stop with Ctrl+C.

## Current status

No verified audio playback. Do not publish a release advertised as a working KORE 1 audio driver.
