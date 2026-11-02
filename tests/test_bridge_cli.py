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
cases += [(['--coreaudio-pcm','--pcm-gain','.001'],0),(['--coreaudio-pcm','--pcm-gain','0'],0),(['--coreaudio-pcm','--pcm-gain','1.001'],2),(['--coreaudio-pcm','--pcm-gain','nan'],2),(['--pcm-gain','.00025'],2)]
cases += [(['--coreaudio-pcm','--pcm-gain',v],0) for v in ['.01','.1','1']]
cases += [(['--coreaudio-pcm','--pcm-gain',v],2) for v in ['-1','inf','-inf']]
cases += [(['--coreaudio-pcm','--main-gain','.2','--headphone-gain','1'],0),
          (['--headphone-gain','1'],2),(['--coreaudio-pcm','--main-gain','nan'],2),
          (['--coreaudio-pcm','--headphone-gain','1.01'],2)]
for args,expected in cases:
    r=subprocess.run([str(exe),*args],capture_output=True,text=True)
    assert r.returncode==expected,(args,r.stdout,r.stderr,r.returncode)
    if not args or (expected==0 and '--help' not in args):assert 'hardware=disabled' in r.stdout
print('PASS final bridge CLI: bounded defaults, explicit continuous operation, exclusive modes, no install side effects')

r=subprocess.check_output([str(exe),'--coreaudio-pcm','--main-gain','.2','--headphone-gain','1'],text=True)
assert 'main_gain=0.20000000 headphone_gain=1.00000000' in r
