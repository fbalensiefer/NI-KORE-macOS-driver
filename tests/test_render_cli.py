"""Renderer gain/mode boundaries: all successful cases remain hardware-free."""
from pathlib import Path
import subprocess
exe=Path(__file__).resolve().parents[1]/'build/kore-coreaudio-render'
for value,code in [('0',0),('.1',0),('1',0),('nan',2),('inf',2),('-1',2),('1.1',2),('junk',2)]:
    result=subprocess.run([str(exe),'--level',value],capture_output=True,text=True)
    assert result.returncode==code,(value,result.returncode)
    if code==0:assert 'Dry run' in result.stdout
for options,code in [(['--echo-test','--headphones'],0),(['--echo-test','--stereo-test'],2)]:
    result=subprocess.run([str(exe),*options],capture_output=True,text=True)
    assert result.returncode==code,(options,result.returncode)
print('PASS device-free renderer gain and mode boundaries')

helper=exe.parents[1]/'automation/coreaudio_loopback.py'
import sys
for options,code in [(['--echo-test'],0),(['--signal-level','nan'],2),
                     (['--signal-level','0'],2),(['--echo-test','--stereo-test'],2)]:
    result=subprocess.run([sys.executable,str(helper),*options],capture_output=True,text=True)
    assert result.returncode==code,(options,result.returncode)
    if code==0:assert 'no devices opened' in result.stdout
