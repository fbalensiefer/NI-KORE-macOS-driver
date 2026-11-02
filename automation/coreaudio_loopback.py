#!/usr/bin/env python3
"""Opt-in bounded system CoreAudio -> shared MIDI/USB -> MAYA44 loopback."""
import argparse
import datetime
import json
import math
from pathlib import Path
import signal
import subprocess
import time
from measure_native_quality import measure
from analyze_stereo_capture import analyze as analyze_stereo
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
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--run-tone',action='store_true');p.add_argument('--stereo-test',action='store_true');p.add_argument('--use-login-bridge',action='store_true');p.add_argument('--pcm-gain',type=float,default=.00025);args=p.parse_args()
    if not math.isfinite(args.pcm_gain) or not 0<=args.pcm_gain<=.001:p.error('PCM test gain must be 0..0.001')
    if args.use_login_bridge and args.pcm_gain!=.00025:p.error('Cannot change gain of the installed login bridge')
    if not args.run_tone:print('Dry run: --run-tone required; no devices opened');return
    folder=ROOT/'artifacts'/('coreaudio-loopback-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f'));folder.mkdir()
    wav=folder/'maya44.wav';timing=folder/'timing.json';capturelog=folder/'capture.log';bridgelog=folder/'bridge.log';renderlog=folder/'render.log'
    capture=bridge=None
    try:
        with capturelog.open('w') as c,bridgelog.open('w') as b,renderlog.open('w') as r:
            capture=subprocess.Popen([str(ROOT/'build/kore-hal-capture'),'--record',str(wav),'--report',str(timing),'--seconds','10'],stdout=c,stderr=subprocess.STDOUT)
            wait_ready(capture,capturelog,'CAPTURE READY',5)
            agent_before=None
            if args.use_login_bridge:
                import os,re
                service=f'gui/{os.getuid()}/org.nikore.bridge'
                def agent_state():
                    text=subprocess.check_output(['/bin/launchctl','print',service],text=True)
                    if 'state = running' not in text:raise RuntimeError('Login bridge not running')
                    return re.search(r'\bpid = (\d+)',text).group(1),re.search(r'\bruns = (\d+)',text).group(1)
                agent_before=agent_state()
                error_log=Path.home()/'Library/Logs/KORE1-bridge-error.log';error_offset=error_log.stat().st_size
            else:
                bridge=subprocess.Popen([str(ROOT/'build/kore1-final-bridge'),'--run','--coreaudio-pcm','--continuous','--mixxx-feedback','--pcm-gain',str(args.pcm_gain)],stdout=b,stderr=subprocess.STDOUT)
                wait_ready(bridge,bridgelog,'NATIVE STREAM',6)
            command=[str(ROOT/'build/kore-coreaudio-render'),'--run','--feedback']
            if args.stereo_test:command.append('--stereo-test')
            render=subprocess.run(command,stdout=r,stderr=subprocess.STDOUT,timeout=8)
            stop(bridge)
            if render.returncode or (bridge and bridge.returncode):raise RuntimeError('Render/shared bridge failed')
            if args.use_login_bridge and (agent_state()!=agent_before or error_log.stat().st_size!=error_offset):raise RuntimeError('Login service restarted or logged an error during test')
            if capture.wait(timeout=12):raise RuntimeError('MAYA44 capture failed')
        result=analyze_stereo(wav,timing) if args.stereo_test else measure(wav,timing,None if args.use_login_bridge else bridgelog)
        result.update(requested_pcm_gain=args.pcm_gain,agent_state=agent_before,bridge_exit=bridge.returncode if bridge else None,use_login_bridge=args.use_login_bridge,render_exit=render.returncode,capture_exit=capture.returncode)
        (folder/'result.json').write_text(json.dumps(result,indent=2)+'\n')
        print(folder);print(json.dumps(result,indent=2))
        if not result.get('pass',True):raise RuntimeError('Stereo criteria not met; inspect result.json')
    finally:
        stop(bridge);stop(capture)
if __name__=='__main__':main()
