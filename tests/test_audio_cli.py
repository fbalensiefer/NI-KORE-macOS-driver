"""All commands include --dry-run; never initialize USB or CoreMIDI."""
from pathlib import Path
import subprocess
exe=Path(__file__).resolve().parents[1]/'build/kore1-bridge-audio'
cases=[(['--audio-fixed96-silence'],0),(['--audio-init-only'],0),
       (['--audio-init'],0),(['--audio-rx-matched-zero'],0),
       (['--headphone-tone','--audio-out-96','--audio-tone-channel','4'],0),
       (['--audio-fixed96-silence','--headphone-tone'],2),
       (['--audio-fixed96-silence','--pcm-fifo'],2),
       (['--audio-fixed96-silence','--audio-rx-matched-zero'],2),
       (['--audio-init-only','--audio-init'],2),
       (['--audio-max-amplitude','nan'],2),(['--audio-max-amplitude','.002'],2),
       (['--audio-duration-ms','0'],2),(['--audio-duration-ms','3x'],2),
       (['--audio-tone-channel','5'],2)]
profiles=['uniform96','uniform176','leading-zero','trailing-zero','interior-zero','observed','variable-nonzero','observed-last-data']
cases += [(['--audio-layout-probe',name],0) for name in profiles]
cases += [(['--audio-layout-probe','observed',flag],2) for flag in
          ['--headphone-tone','--pcm-fifo','--audio-fixed96-silence','--audio-rx-matched-zero','--audio-init-only']]
cases += [(['--audio-layout-probe','unknown'],2),
          (['--audio-layout-probe','observed','--audio-layout-probe','uniform96'],2)]
for args,expected in cases:
    r=subprocess.run([str(exe),'--dry-run',*args],capture_output=True,text=True)
    assert r.returncode==expected,(args,r.returncode,r.stdout,r.stderr)
print(f'PASS: {len(cases)} CLI cases without device access')
