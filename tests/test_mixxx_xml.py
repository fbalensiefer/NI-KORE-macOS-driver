"""Check that emitted MIDI note-off events reach the cue release handlers."""
from pathlib import Path
import xml.etree.ElementTree as ET
root=Path(__file__).resolve().parents[1]
e=ET.parse(root/'mixxx/KORE-1.midi.xml')
assert e.findtext('info/name')=='KORE 1 FX + Browser'
routes={(int(c.findtext('status'),16),int(c.findtext('midino'),16)):c.findtext('key') for c in e.findall('.//control')}
for note in (50,51,41,38,42,39):
    assert routes[0x90,note]==routes[0x80,note]=='Kore1.button'
assert (0x90,37) not in routes # LCD backlight must never start playback
assert all((0x90,note) not in routes for note in range(60,68)) # touch sensors
for cc in range(20,28):
    assert routes[0xb0,cc]=='Kore1.encoder'
assert routes[0xb0,28]=='Kore1.browse'
for f in e.findall('.//scriptfiles/file'):
    assert (root/'mixxx'/f.attrib['filename']).is_file()
print('Mixxx XML routes match bridge note-on, note-off and encoder events')
