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
cases += [(['--stream-silence'],0),(['--stream-silence','--duration-ms','100'],0),
          (['--stream-silence','--duration-ms','3001'],2),(['--stream-silence','--duration-ms','0'],2),
          (['--stream-silence','--duration-ms','x'],2),(['--stream-silence','--layout','rx'],2),
          (['--stream-silence','--zero-offset','compact'],2),(['--stream-silence','--buffer-guard','0'],2)]
cases += [(['--stream-tone',str(ch)],0) for ch in range(1,5)]
cases += [(['--stream-tone','0'],2),(['--stream-tone','1','--tone-amplitude','.002'],2),
          (['--stream-tone','1','--tone-amplitude','nan'],2),(['--stream-tone','1','--duration-ms','100'],2),
          (['--stream-tone','1','--stream-silence'],2),(['--tone-amplitude','.00025'],2)]
cases += [(['--coreaudio-pcm'],0),(['--coreaudio-pcm','--stream-silence'],2),(['--coreaudio-pcm','--stream-tone','3'],2)]
for args,expected in cases:
    r=subprocess.run([str(exe),'--dry-run',*args],capture_output=True,text=True)
    assert r.returncode==expected,(args,r.returncode,r.stdout,r.stderr)
    if expected==0:assert 'hardware=disabled' in r.stdout
print(f'PASS native CLI: {len(cases)} device-free cases')
