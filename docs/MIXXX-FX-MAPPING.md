# KORE 1 FX and library mapping

The active preset is `mixxx/KORE-1.midi.xml` with `KORE-1.js`. Archived root v10
presets are preserved, but are not this layout. On the user's sandboxed Mixxx,
the active preset previously pointed into the application bundle at
`KORE-1-Mixxx-v10.1.midi.xml`. The new files were copied into the user controllers
folder and mixxx.cfg was backed up before selecting their absolute path. No
application-bundle files were modified.

## Deck and transport

Control selects A; Sound selects B. Left/right arrows also select A/B; the LCD
header follows selection. The physical Play button is MIDI note 50, Stop is 51.
Backlight is note 37 and never starts playback. Esc (41) is momentary cue; its
release always goes back to the deck where it was pressed, even after switching
decks. Listen (48) toggles PFL. Enter (39) loads the selected library track into
the selected stopped deck; it refuses to replace a playing deck. Up/down arrows
move one track. Touch notes 60..67, Record and Menu have no action.

## Rotary controls

All eight relative encoders now control effects, not volume or pitch. CC20 is
the selected deck's QuickEffect superknob (normally Filter); CC21 is FX dry/wet,
CC22 the FX superknob, CC23/24/25 the metaknobs for slots 1/2/3, CC26/27 the
first parameters for slots 1/2. Values use normalized getParameter/setParameter,
so bounded movement is valid for differing effect parameter ranges.
Deck A uses EffectUnit1 and B uses EffectUnit2. Choose an effect preset before
expecting sound changes from an empty slot; ensure its deck routing is enabled.

The softkey order below follows the Linux KORE keycode array (BTN1..BTN8), not a
new physical calibration. Read left to right on the front panel:

| Row | Button 1 | Button 2 | Button 3 | Button 4 |
|---|---|---|---|---|
| Top (notes 55,54,53,52) | QuickEffect enable | FX slot 1 enable | Slot 2 enable | Slot 3 enable |
| Bottom (59,58,57,56) | FX unit enable | Route unit to selected deck | Previous chain preset | Next chain preset |

No effect, volume, pitch or routing is forced during mapping initialization.
Existing LED feedback is retained; this change does not claim a newly calibrated
softkey LED layout.

## Jogwheel

The bridge previously read IO byte 4 only for diagnostics. It now emits relative
CC28 from the inverted 8-bit counter, matching Linux's `255 - buf[4]` ABS_MISC
convention. First observation is baseline; duplicate samples emit nothing.
Wraparound uses the shortest signed delta; an exactly 128-count jump is ambiguous
and ignored. Deltas are split into at most 63-count MIDI messages; 1..63 means
positive, 65..127 negative, 0 and 64 idle. Mixxx receives signed track increments.
A new installed bridge binary is necessary; a script-only replacement cannot
expose a previously unreported hardware wheel. Detent scaling/direction and all
front-panel positions still need a physical smoke test.

## Verification

Offline tests cover the actual native decoder's wheel baseline, wraparound,
reverse movement, split delta and ambiguous jump. JavaScript/XML tests ensure
backlight and touch sensors cannot trigger Play, cue release survives deck
changes, FX values clamp, and loading protects playing decks. These checks do
not constitute a physical hardware test or proof that Mixxx loaded the preset.

After installation, restart Mixxx and confirm KORE 1 Bridge -> KORE-1 mapping.
With no performance in progress: select a stopped deck, browse with the wheel,
load with Enter, check Play/Stop, then move a filter and choose/toggle an FX preset.
Backlight must not alter deck playback. Physical wheel direction and softkey
labels should be checked against this table before considering them validated.

References: [Linux v6.12 caiaq input.c](https://github.com/torvalds/linux/blob/v6.12/sound/usb/caiaq/input.c),
[Mixxx control reference](https://manual.mixxx.org/2.5/en/chapters/appendix/mixxx_controls.html).

## User-local bridge installation on this Mac

The macOS administrator dialog for a root binary replacement did not appear and
the 100s attempt timed out; its finally block restored the previous service.
The tested bridge was then installed to
`~/Library/Application Support/KORE1/kore1-final-bridge` using
`python3 automation/install_user_bridge.py --run` (default is dry-run). The
existing launch agent now points there and retains its MIDI/audio/gain arguments.
The system HAL was not changed or restarted. Its format query still returned
48kHz / four outputs / 16 bytes per frame. Installed bridge SHA256:
`a4ae7c1085571b12ad8fbf8b4036adf2ffa44772ac594095f2e5ae6093d9a3f0`.
The first service check reported running PID 6776, run count 1, no previous exit,
and native shared MIDI/audio startup completed.

Agent backups are in `~/Library/Application Support/KORE1/Backups/`. The user's
Mixxx config backup is `mixxx.cfg.backup-fx-20261103-083731` in the sandboxed Mixxx
support folder. Running the older system bridge's `--install-login` would restore
its system binary path and lose the new wheel feature; use the user-local update
helper after rebuilding, or install the updated system package first.

Mixxx was reopened after the user-folder preset was selected. Its startup log
confirmed discovery of one user MIDI mapping and the KORE Bridge input/Feedback
output. Desktop accessibility returned `AXError.cannotComplete`, and the
available log had not yet confirmed opening the new script; live preset loading
and physical control behavior must not be reported as verified. A physical
Play/Backlight/wheel smoke-test was requested from the user.
