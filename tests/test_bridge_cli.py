"""Final bridge safety parsing only: every successful case stays device-free."""
from pathlib import Path
import subprocess
exe=Path(__file__).resolve().parents[1]/'build/kore1-final-bridge'
cases=[([],0),(['--help'],0),(['--dry-run','--coreaudio-pcm','--continuous','--mixxx-feedback'],0),
       (['--midi-only','--continuous'],0),(['--stream-silence','--duration-ms','100'],0),
       (['--duration-ms','3001'],2),(['--duration-ms','-1'],2),(['--duration-ms','nan'],2),
       (['--continuous','--duration-ms','3000'],2),(['--stream-tone','3','--continuous'],2),
       (['--stream-tone','0'],2),(['--stream-tone','3','--duration-ms','100'],2),
       (['--coreaudio-pcm','--midi-only'],2),(['--midi-only','--no-midi'],2),
       (['--install-system','--dry-run'],2),(['--stream-tone','3'],0)]
for args,expected in cases:
    r=subprocess.run([str(exe),*args],capture_output=True,text=True)
    assert r.returncode==expected,(args,r.stdout,r.stderr,r.returncode)
    if not args or (expected==0 and '--help' not in args):assert 'hardware=disabled' in r.stdout
print('PASS final bridge CLI: bounded defaults, explicit continuous operation, exclusive modes, no install side effects')
