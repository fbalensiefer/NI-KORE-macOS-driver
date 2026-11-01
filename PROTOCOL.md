# Device protocol notes

Hardware under test: **KORE 1, VID 0x17CC PID 0x4711**. OpenKoreBridge primarily targets KORE 2 (PID 0x4712). Shared caiaq Linux-driver code suggests many functions are common; observed KORE 1 LED and LCD tests support this, not universal command compatibility.

- USB interface `0`, alternate setting `1`.
- USB bulk OUT `0x01`, IN `0x81`.
- `0x01`: GET_DEVICE_INFO.
- `0x0B 0x01 0x0A 0x05`: AUTO_MSG enable used by bridge.
- `0x02`: ERP report, eight values decoded from pairs `(7,5),(12,14),(15,13),(0,2),(3,1),(8,10),(11,9),(4,6)` after command byte. Normalized circular position 0–999.
- `0x03`: ANALOG report: three 16-bit big-endian values, observed but physical inputs not identified.
- `0x04`: IO report: first four bytes are up to 32 digital button bits; the next byte is a jog-wheel value reported as `255 - raw[4]` (`ABS_MISC` in Linux driver).
- `0x0C` + 32 bytes: LED brightness state; index 30 is LCD backlight. The self-test confirmed play and F1 on KORE 1. Other names currently derive from the Linux mapping.
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
