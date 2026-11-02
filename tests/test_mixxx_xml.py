"""Check that emitted MIDI note-off events reach the cue release handlers."""
from pathlib import Path
import xml.etree.ElementTree as ET
root=Path(__file__).resolve().parents[1]
e=ET.parse(root/'mixxx/KORE-1.midi.xml')
routes={(int(c.findtext('status'),16),int(c.findtext('midino'),16)):c.findtext('key') for c in e.findall('.//control')}
for note in (39,41):
    assert routes[0x90,note]==routes[0x80,note]=='Kore1.cueButton'
for cc in range(20,28):
    assert routes[0xb0,cc]=='Kore1.encoder'
for f in e.findall('.//scriptfiles/file'):
    assert (root/'mixxx'/f.attrib['filename']).is_file()
print('Mixxx XML routes match bridge note-on, note-off and encoder events')
