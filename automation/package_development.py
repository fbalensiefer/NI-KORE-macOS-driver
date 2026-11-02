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
shutil.copy2(ROOT/'docs/DEVELOPMENT-PACKAGE.md',DEST/'README.md')
shutil.copy2(ROOT/'docs/STEREO-OUTPUT-RESULTS.md',DEST/'STEREO-OUTPUT-RESULTS.md')
(DEST/'automation').mkdir()
for name in ('coreaudio_loopback.py','analyze_stereo_capture.py','measure_native_quality.py','analyze_native_capture.py'):
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
