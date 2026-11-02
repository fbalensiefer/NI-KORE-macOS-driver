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

## Native audio development path

`audio/native_probe.m` owns interface 0 through IOUSBHost.framework (macOS 12+).
It handles bulk device-info/AUDIO_PARAMS commands and schedules eight ISO
microframes per batch, using 32 IN and 64 OUT slots on a serial callback queue.
OUT lengths come from IN completions; zero requests retain their time slots and
use explicit valid buffer offsets. A fixed 32 ms OUT lead tolerates callback
jitter without silently changing the timeline. Bounded deadlines and a late
callback drain protect shutdown ownership.

`audio/caiaq.h`, `native_plan.h` and `tone.h` share tested framing, buffer plans
and opt-in capped PCM generation. The native process currently provides neither
a general application PCM feed nor CoreAudio/MIDI endpoints. It cannot run at
the same time as the libusb controller process: both require exclusive interface
ownership. The next integration must share that ownership rather than claim the
interface from competing processes.

The root compatibility libusb bridge and archived source snapshots are retained
for research. Current native results, including capture limitations, are linked
from the README.
