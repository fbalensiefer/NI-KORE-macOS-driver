#!/usr/bin/env python3
"""Opt-in bounded system CoreAudio -> shared MIDI/USB -> MAYA44 loopback."""
import argparse
import datetime
import json
from pathlib import Path
import signal
import subprocess
import time
from measure_native_quality import measure
ROOT=Path(__file__).resolve().parents[1]
def wait_ready(process,path,marker,timeout):
    until=time.monotonic()+timeout
    while marker not in path.read_text():
        if process.poll() is not None or time.monotonic()>until:
            raise RuntimeError(f'{marker} not reached; inspect {path}')
        time.sleep(.02)
def stop(process):
    if process and process.poll() is None:
        process.send_signal(signal.SIGTERM)
        try:process.wait(timeout=5)
        except subprocess.TimeoutExpired:process.kill();process.wait();raise RuntimeError('Forced teardown; run is invalid')
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--run-tone',action='store_true');args=p.parse_args()
    if not args.run_tone:print('Dry run: --run-tone required; no devices opened');return
    folder=ROOT/'artifacts'/('coreaudio-loopback-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f'));folder.mkdir()
    wav=folder/'maya44.wav';timing=folder/'timing.json';capturelog=folder/'capture.log';bridgelog=folder/'bridge.log';renderlog=folder/'render.log'
    capture=bridge=None
    try:
        with capturelog.open('w') as c,bridgelog.open('w') as b,renderlog.open('w') as r:
            capture=subprocess.Popen([str(ROOT/'build/kore-hal-capture'),'--record',str(wav),'--report',str(timing),'--seconds','10'],stdout=c,stderr=subprocess.STDOUT)
            wait_ready(capture,capturelog,'CAPTURE READY',5)
            bridge=subprocess.Popen([str(ROOT/'build/kore1-final-bridge'),'--run','--coreaudio-pcm','--continuous','--mixxx-feedback'],stdout=b,stderr=subprocess.STDOUT)
            wait_ready(bridge,bridgelog,'NATIVE STREAM',6)
            render=subprocess.run([str(ROOT/'build/kore-coreaudio-render'),'--run','--feedback'],stdout=r,stderr=subprocess.STDOUT,timeout=6)
            stop(bridge)
            if render.returncode or bridge.returncode:raise RuntimeError('Render/shared bridge failed')
            if capture.wait(timeout=12):raise RuntimeError('MAYA44 capture failed')
        result=measure(wav,timing,bridgelog)
        result.update(bridge_exit=bridge.returncode,render_exit=render.returncode,capture_exit=capture.returncode)
        (folder/'result.json').write_text(json.dumps(result,indent=2)+'\n')
        print(folder);print(json.dumps(result,indent=2))
    finally:
        stop(bridge);stop(capture)
if __name__=='__main__':main()
