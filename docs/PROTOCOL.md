> Status reference (2026-11-03): Transport-focused reference. Current four-channel publication, wheel/feedback mapping and deployment are summarized in the root protocol/architecture guides.
> See [current status](STATUS.md) and [documentation index](INDEX.md).

# CAIAQ protocol reference and implementation notes

## Endpoint map

| Purpose | USB endpoint | Type |
| --- | --- | --- |
| Commands to KORE | `0x01` | bulk OUT |
| Responses / controls | `0x81` | bulk IN |
| Audio capture from KORE | `0x82` | isochronous IN |
| Audio playback to KORE | `0x06` | isochronous OUT |

Use interface 0, alternate setting 1 for audio. Eight packets per transfer, 512-byte maximum packet allocation, 32 concurrent IN URBs in the Linux reference. These are transfer buffer capacities, not a promise that each packet has 512 bytes of payload.

## Handshake

1. Arm bulk IN before requesting `GET_DEVICE_INFO` (`0x01`); retry if startup response is missed.
2. Use device-reported stream count and alignment; do not guess.
3. Send `AUDIO_PARAMS`: `09 01 02 B0 00 01` for observed 48 kHz / 24-bit / two streams / alignment 2.
4. Require response status `1` before streaming.
5. Submit isochronous IN; in RX-matched mode submit OUT after IN completion, with packet lengths derived from IN descriptors.

## Framing

Linux `sound/usb/caiaq/audio.c` is authoritative for the Linux driver behavior. `BYTES_PER_SAMPLE=3`, `BYTES_PER_SAMPLE_USB=4`, two channels per stream, and alignment-2 checkbytes are part of the mode-0 OUT encoding. `bpp=176` is the configured capacity/drift parameter; Linux capture decoding
skips shorter packets, while OUT filling uses each successful IN actual length. The first two bytes of the observed short IN packets are not a validated status structure.

## Research hygiene

Record flags, source commit/hash, USB descriptors, raw packet lengths and payload samples, and analog capture chain for each run. Separate firmware ACK, host-side USB success, valid protocol framing, and verified analog playback. Never claim one proves the next.


The six-byte AUDIO_PARAMS command has no extra bank byte. The earlier seven-byte
sequence in historical notes was corrected during development. Native playback
uses explicit offsets for zero-length transactions and retains every time slot;
see [native transport](NATIVE-TRANSPORT.md) and [recorded results](TONE-PLAYBACK-RESULTS.md).
