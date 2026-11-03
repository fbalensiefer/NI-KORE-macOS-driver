#!/usr/bin/env python3
"""Assemble built artifacts without installing a driver or opening hardware."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import zipfile
ROOT=Path(__file__).resolve().parents[1]
DEST=ROOT/'build/KORE1-macOS-development'
if DEST.exists():
    shutil.rmtree(DEST)
DEST.mkdir()
for name in ('kore1-final-bridge','kore-midi-v10','kore1-native-probe','kore-hal-capture','kore-coreaudio-render','kore-coreaudio-system-test'):
    shutil.copy2(ROOT/'build'/name,DEST/name)
shutil.copytree(ROOT/'build/KoreAudio.driver',DEST/'KoreAudio.driver')
shutil.copytree(ROOT/'mixxx',DEST/'mixxx')
for name in ('kore1-final-bridge.c','COPYING','NOTICE.md'):
    shutil.copy2(ROOT/name,DEST/name)
# Preserve documentation hierarchy so relative links work in the extracted zip.
for path in ROOT.glob('*.md'):
    shutil.copy2(path,DEST/path.name)
shutil.copytree(ROOT/'docs',DEST/'docs')
shutil.copytree(ROOT/'experimental/legacy-libusb',DEST/'experimental/legacy-libusb')
readme=DEST/'README.md'
readme.write_text('> Extracted development archive: runtime binaries are in this folder, so omit\n'
                  '> `build/` from tool paths. `make` commands require the source checkout; see\n'
                  '> [standalone compilation](docs/DEVELOPMENT-PACKAGE.md).\n\n'+readme.read_text())
(DEST/'automation').mkdir()
for name in ('coreaudio_loopback.py','analyze_stereo_capture.py','measure_native_quality.py','analyze_native_capture.py','analyze_echo_capture.py','install_user_bridge.py'):
    shutil.copy2(ROOT/'automation'/name,DEST/'automation'/name)
commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
dirty=bool(subprocess.check_output(['git','status','--porcelain'],cwd=ROOT,text=True).strip())
files={str(p.relative_to(DEST)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(DEST.rglob('*')) if p.is_file()}
(DEST/'manifest.json').write_text(json.dumps({'base_commit':commit,'working_tree_modified':dirty,'files_sha256':files},indent=2)+'\n')
archive=ROOT/'build/KORE1-macOS-development.zip'
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
    for p in sorted(DEST.rglob('*')):
        if p.is_file():
            z.write(p,p.relative_to(DEST.parent))
print(f'Local package: {archive}')
