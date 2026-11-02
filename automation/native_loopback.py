#!/usr/bin/env python3
"""Explicit opt-in bounded KORE tone + timestamp-accounted MAYA44 capture."""
import argparse,subprocess,time,datetime,json,re
from pathlib import Path

def main():
 p=argparse.ArgumentParser();p.add_argument('--run-tone',action='store_true');p.add_argument('--channel',type=int,choices=range(1,5),default=3)
 p.add_argument('--capture-backend',choices=['hal','avfoundation'],default='hal');p.add_argument('--input-index',type=int)
 args=p.parse_args();root=Path(__file__).resolve().parents[1]
 stamp=datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f');folder=root/'artifacts'/f'native-tone-ch{args.channel}-{stamp}'
 if not args.run_tone:print('Dry-run: requires --run-tone; no devices opened');return
 if args.capture_backend=='avfoundation' and args.input_index is None:p.error('AVFoundation requires a verified --input-index')
 folder.mkdir(parents=True,exist_ok=False)
 capture=folder/'maya44-4ch.wav';caplog=folder/'capture.log';driverlog=folder/'driver.log';timing=folder/'timing.json'
 if args.capture_backend=='hal':
  recorder_cmd=[str(root/'build/kore-hal-capture'),'--record',str(capture),'--report',str(timing),'--seconds','10']
  ready='CAPTURE READY'
 else:
  listing=subprocess.run(['ffmpeg','-hide_banner','-f','avfoundation','-list_devices','true','-i',''],capture_output=True,text=True,timeout=5)
  audio=listing.stderr.split('AVFoundation audio devices:',1)[-1]
  indices=[int(x) for x in re.findall(r'\[(\d+)\] MAYA44 USB\+',audio)]
  if indices!=[args.input_index]:raise RuntimeError('MAYA44 device verification failed; no tone started')
  (folder/'devices.log').write_text(listing.stderr)
  recorder_cmd=['ffmpeg','-hide_banner','-nostdin','-f','avfoundation','-i',f':{args.input_index}',
                '-t','10','-map','0:a:0','-ac','4','-ar','44100','-c:a','pcm_s32le','-n',str(capture)]
  ready='Output #0'
 driver=[str(root/'build/kore1-native-probe'),'--run','--stream-tone',str(args.channel),'--tone-amplitude','0.00025']
 with caplog.open('w') as output:
  recorder=subprocess.Popen(recorder_cmd,stdout=output,stderr=subprocess.STDOUT)
  try:
   limit=time.monotonic()+5
   while ready not in caplog.read_text():
    if recorder.poll() is not None or time.monotonic()>limit:raise RuntimeError('Capture not ready; no tone started')
    time.sleep(.05)
   with driverlog.open('w') as output_driver:
    result=subprocess.run(driver,stdout=output_driver,stderr=subprocess.STDOUT,timeout=10)
   code=recorder.wait(timeout=15)
   metadata={'channel':args.channel,'amplitude':.00025,'capture_backend':args.capture_backend,
             'driver_exit':result.returncode,'capture_exit':code,'wav':str(capture),'driver_log':str(driverlog)}
   if timing.exists():metadata['timing']=json.loads(timing.read_text())
   (folder/'run.json').write_text(json.dumps(metadata,indent=2)+'\n');print(json.dumps(metadata,indent=2))
   if code or result.returncode:raise RuntimeError('Capture or native transport failed; inspect logs')
  finally:
   if recorder.poll() is None:
    recorder.terminate()
    try:recorder.wait(timeout=3)
    except subprocess.TimeoutExpired:recorder.kill();recorder.wait()
if __name__=='__main__':main()
