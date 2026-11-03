> Status reference (2026-11-03): Historical research findings. Later native playback, CoreAudio integration and bounded line captures supersede early unknown-output statements.
> See [current status](STATUS.md) and [documentation index](INDEX.md).

> Historical research/handoff snapshot, superseded on November 2, 2026.
> See the repository README and current native playback results. Old command
> examples and status statements below are retained as evidence, not current instructions.

# Audio reverse-engineering findings — 2026-11-02

## Confidence convention

- **Observed:** device logs, USB descriptor inspection, or source shown during testing.
- **Compared:** checked against Linux `sound/usb/caiaq/{audio,device}.c`.
- **Hypothesis:** plausible explanation, not proven.

## Hardware and USB (observed)

- Native Instruments KORE 1: VID `0x17cc`, PID `0x4711`, USB 2.0, device revision `0x0029`.
- Interface 0 altsetting 0: bulk OUT `0x01`, bulk IN `0x81`, 512-byte maximum packets.
- Interface 0 altsetting 1: bulk endpoints plus isochronous IN `0x82` and OUT `0x06`, each with `wMaxPacketSize=0x0200`, `bInterval=1`.
- Device-info response: 2 analog inputs, 4 analog outputs, no digital inputs/outputs, `data_alignment=2`, `n_streams=2`.
- Audio parameter request `09 00 01 02 B0 00 01` (48 kHz, 24-bit, bpp=176, one packet/microframe) receives firmware status `1` (accepted).

## Observed transport behavior

- With 32 concurrent IN URBs and eight 512-byte descriptors per URB, roughly 8,000 IN packets/s arrive with no reported transfer errors.
- Virtually every IN packet is **2 bytes**, e.g. `F5 00`, `B7 00`, `E0 00`, `D1 00`, `01 00`, `73 00`. In a long sample the second byte was always zero; first-byte low six bits spanned all 64 values.
- RX-matched duplex returns 2-byte OUT packets `00 00`, ~8,000 packets/s, no reported USB errors or exhausted OUT slots.
- A short log recorded IN 48,000 packets vs OUT 47,952 (six eight-packet URBs difference); without bus-level tracing this is not evidence of lost packets.
- Fixed 96-byte OUT packets carried a correctly counted 48 kHz-equivalent six frames per packet per stream and generated nonzero PCM bytes, yet the analog loopback did **not** yield valid 440/880 Hz audio. Fixed 176-byte tests also failed to establish valid analog playback.
- An analog loopback from KORE headphone output into an ESI MAYA44 USB+ input captured mainly whining/high-frequency artifacts. The KORE output channel routing is unverified.

## Linux comparison

- `snd_usb_caiaq_set_audio_params()` in `device.c` sends command `0x09` with rate/depth/bpp/packets-per-microframe; the observed command and firmware ACK match.
- `audio.c` uses `N_URBS=32`, `FRAMES_PER_URB=8`, `BYTES_PER_FRAME=512`, capture endpoint 2, playback endpoint 6.
- Linux queues IN URBs and prepares/submits OUT URBs on IN completion, using actual IN packet lengths; RX-matched bridge scheduling follows this general approach, but macOS/libusb bus scheduling parity is **unverified**.
- Linux `fill_out_urb_mode_0()` interleaves PCM bytes for two streams and checkbytes. For alignment 2, `MAKE_CHECKBYTE(cdev,stream,i) = (stream << 1) | (~(i / (cdev->n_streams * 4)) & 1)`. The local `linux_fill_pcm()` shown during investigation follows the same broad 3-byte-sample/checkbyte structure.
- The bridge's report `short packets are ignored by Linux PCM decoder` is **misleading**. `bpp=176` is not a general minimum packet length; do not treat the diagnostic `eligible=0` as proof that Linux discards all shorter packets.
- Two-byte incoming packets do not consistently match expected alignment-2 checkbytes. It is **not established** that these bytes represent checkbytes at all; interpreting high bits as panic/status is conditional and unvalidated.
- Linux initializes `first_packet=4` and resets input/output panic state on stream start. A matching state transition in the bridge has not been demonstrated.

## Code reviewed (local working tree excerpts)

- `run_audio_init()` waits for device info, validates alignment/streams, calculates `bpp=176`, sends audio params, waits for ACK.
- `start_audio_probe()` allocates 32 IN transfers with eight 512-byte packets each (when `--audio-linux-urbs` is used), then submits them.
- `on_audio_rx()` reads `actual_length` and packet bytes via `libusb_get_iso_packet_buffer_simple()` and calls `audio_send_silence_matching_rx()`.
- `audio_send_silence_matching_rx()` copies actual IN packet lengths (unless fixed mode is requested), compacts packet payloads into a contiguous buffer, uses free OUT pool slots, and submits transfers. No obvious slot reuse bug was found from the excerpt.
- `on_audio_tx()` clears `busy` on completion; independent mode rearms the same slot only after completion.
- `linux_fill_pcm()` encodes 24-bit big-endian samples with stream interleaving/checkbytes. The 2-byte RX-matched path instead calls `v21_fill_short()` and sends `00 00` by default, **without PCM**.

## Not established

No validated explanation exists for persistent 2-byte IN packets, missing valid analog PCM, device-side audio readiness, USB microframe timing equivalence, panic-bit meaning for these particular packets, or output channel mapping. Neither a firmware defect nor a macOS USB scheduling defect has been proven.
