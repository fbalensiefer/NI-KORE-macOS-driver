#!/usr/bin/env python3
"""Dependency-free repository consistency checks (not hardware tests)."""
from pathlib import Path
import xml.etree.ElementTree as ET
root=Path(__file__).resolve().parent
c=(root/'src/kore-midi-v10.c').read_text()
js=(root/'KORE-1-Mixxx-v10.js').read_text()
xml=root/'KORE-1-Mixxx-v10.midi.xml'
e=ET.parse(xml).getroot()
assert e.tag == 'MixxxControllerPreset'
assert '17cc' in c.lower() and '4711' in c.lower()
for s in ['KORE 1 Bridge','KORE 1 Feedback','--mixxx-feedback','--lcd-test','--led-test']:
    assert s in c, s
for f in e.findall('.//scriptfiles/file'):
    assert (xml.parent/f.attrib['filename']).exists(), f.attrib
for key in set(x.text for x in e.findall('.//control/key') if x.text and x.text.startswith('Kore1.')):
    assert key in js, key
for n in range(20,28):
    assert any(int(x.text,16)==n for x in e.findall('.//control/midino') if x.text), n
print('Repository checks passed: XML, JS mappings, bridge constants, required files')

# Historical sources must remain byte-for-byte intact after organization.
import hashlib, json
manifest=root/'experimental/legacy-libusb/MANIFEST.json'
for entry in json.loads(manifest.read_text()):
    path=root/entry['archived_path']
    assert hashlib.sha256(path.read_bytes()).hexdigest()==entry['sha256'], path
print('Historical archive manifest verified')
