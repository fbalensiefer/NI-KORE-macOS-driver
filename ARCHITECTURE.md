# Architecture

```text
KORE 1 (USB 17CC:4711)
   │ bulk IN 0x81 / bulk OUT 0x01
   ▼
macOS C bridge (libusb)
   │ decode ERP, IO and analog
   ├── CoreMIDI virtual source "KORE 1 Bridge" ──► Mixxx MIDI input / JS mapping
   │
   └── CoreMIDI virtual destination "KORE 1 Feedback" ◄── Mixxx MIDI output
                                   │
                                   └──► LED state (0x0C) / LCD framebuffer (0x08)
```

- The controller itself is not a class-compliant MIDI input; the bridge converts its proprietary USB reports into MIDI.
- `--mixxx-feedback` is opt-in and creates a **separate** CoreMIDI destination.
- A Mixxx input mapping does not automatically route MIDI output to that destination.
- The Mixxx JS mapping sends channel-16 CC feedback and custom 7-bit SysEx strings; see README.
- Jog wheel relative MIDI output and joystick assignment are not finished.
