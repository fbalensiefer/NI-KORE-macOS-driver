# Device protocol notes — current reference

Hardware under test: **KORE 1, VID 0x17CC PID 0x4711**. OpenKoreBridge primarily targets KORE 2 (PID 0x4712). Shared caiaq Linux-driver code suggests many functions are common; observed KORE 1 LED and LCD tests support this, not universal command compatibility.

- USB interface `0`, alternate setting `1`.
- USB bulk OUT `0x01`, IN `0x81`.
- `0x01`: GET_DEVICE_INFO.
- `0x0B 0x01 0x0A 0x05`: AUTO_MSG enable used by bridge.
- `0x02`: ERP report, eight values decoded from pairs `(7,5),(12,14),(15,13),(0,2),(3,1),(8,10),(11,9),(4,6)` after command byte. Normalized circular position 0–999.
- `0x03`: ANALOG report: three 16-bit big-endian values, observed but physical inputs not identified.
- `0x04`: IO report: first four bytes are up to 32 digital button bits; the next byte is a jog-wheel value reported as `255 - raw[4]` (`ABS_MISC` in Linux driver).
- Native IO wheel conversion: first sample is baseline; subsequent 8-bit counter
  differences wrap to the shortest signed delta. Exactly half-turn jumps are
  ignored. Relative CC28 carries split steps of at most 63; CC20–27 remain ERPs.
- `0x0C` + 32 bytes: LED brightness state; index 30 is LCD backlight. The self-test confirmed play and F1 on KORE 1. The maintained `led_names` table is the LED-index reference; its order differs
  from input button bits. Current feedback uses bright/dim states and fixed
  encoder/jog brightness; see the mapping guide.
- `0x08`: LCD command/data transfer. KORE 2 reference sequences were validated in a KORE 1 LCD test. LCD has 128×64 monochrome pixels, organized as 8 pages × 4 data blocks × 32 bytes.

LCD command forms:

```text
08 00 01 XX            single command
08 00 02 XX YY         two-byte command
08 01 20 [32 bytes]    data block
```

Reference LCD init commands: `E2 A1 C8 A2 2C 2E 2F 27`, then `81 <contrast>`, followed by `A6 88 EF AF`. Use with care and only in opt-in modes.

References:
- https://github.com/OpenKoreBridge/OpenKoreBridge/blob/main/openkorebridge/lcd/openkore_lcd_init_test.c
- https://github.com/OpenKoreBridge/OpenKoreBridge/blob/main/openkorebridge/driver/module_src/input.c
- https://github.com/OpenKoreBridge/OpenKoreBridge/blob/main/openkorebridge/driver/module_src/control.c

## Native audio and CoreAudio framing

`AUDIO_PARAMS` is six bytes `09 01 02 B0 00 01` for the current 48kHz / 24-bit,
alignment-2, two paired-stream mode. Use observed IN packet layouts for OUT,
retain zero-length time slots and validate bounds. Signed24 samples are big-endian.
See [transport details](docs/PROTOCOL.md) and [architecture](ARCHITECTURE.md).

CoreAudio exposes four interleaved float channels (16 bytes/frame). `kown` is the
PID consumer lease; `kpc4` is the four-channel CFData pull selector, up to 512
frames per pull. The old stereo `kpcm` selector is intentionally not interchangeable.
These are application-local properties, not new USB commands.

## MIDI and display feedback

Input is channel 1. Relative values 1..63 increment, 65..127 decrement, 0/64 idle.
Notes are `36 + IO bit`: Backlight37, Control38, Enter39, Esc41, Sound42,
right44/down45/up46/left47, Listen48, Record49, Play50, Stop51, softkeys52..59,
touch sensors60..67. Current actions are in [the mapping guide](docs/MIXXX-FX-MAPPING.md).
Analog CC40..42 are emitted but are not assigned by the active FX preset.

Feedback uses a separate `KORE 1 Feedback` destination: channel-16 CC0..31 set
LED brightness, CC40 chooses deck 0/1, CC41 play state and CC42/43 BPM x10 high/low.
Text SysEx is `F0 7D 4B 01 DECK FIELD ASCII... F7` with fields 1=title, 2=artist.
Mixxx 2.5 does not supply real title/artist through the used Player API; the
current script sends loaded-state fallback instead. LCD text is 7-bit, uppercased
by the renderer and limited to 21 visible characters per line; German umlauts
are transliterated in the mapping. The text transport does not imply metadata
availability in every Mixxx version.

Current feature/deployment and evidence limits: [STATUS](docs/STATUS.md).
