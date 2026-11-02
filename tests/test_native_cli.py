"""Parse only: never pass --run and never open a USB service."""
from pathlib import Path
import subprocess
exe=Path(__file__).resolve().parents[1]/'build/kore1-native-probe'
cases=[([],0),(['--layout','rx'],0),(['--layout','observed'],0),
       (['--zero-offset','compact','--buffer-guard','0'],0),
       (['--zero-offset','start','--buffer-guard','1'],0),
       (['--layout','bad'],2),(['--zero-offset','bad'],2),
       (['--buffer-guard','2'],2),(['--buffer-guard','1x'],2),
       (['--layout'],2)]
for args,expected in cases:
    r=subprocess.run([str(exe),'--dry-run',*args],capture_output=True,text=True)
    assert r.returncode==expected,(args,r.returncode,r.stdout,r.stderr)
    if expected==0:assert 'hardware=disabled' in r.stdout
print(f'PASS native CLI: {len(cases)} device-free cases')
