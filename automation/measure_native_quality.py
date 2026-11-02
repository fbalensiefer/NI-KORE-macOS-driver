#!/usr/bin/env python3
"""Relative loopback measurements; capture continuity report is mandatory."""
import argparse,array,json,math,wave,re
from pathlib import Path
from analyze_native_capture import analyze,amplitude,db

def measure(wav,timing,driver_log=None):
 timing=json.loads(Path(timing).read_text())
 if not timing.get('capture_contiguous') or timing.get('timestamp_gaps') or timing.get('invalid_callbacks'):
  raise ValueError('Refusing clock/quality measurements from a discontinuous capture')
 result=analyze(Path(wav));best=max(result['measurements'],key=lambda x:x['tone_rms_dbfs'])
 if not best['tone_detected']:raise ValueError('No convincing test tone')
 with wave.open(str(wav),'rb') as w:
  channels,rate,width=w.getnchannels(),w.getframerate(),w.getsampwidth();raw=array.array('i');raw.frombytes(w.readframes(w.getnframes()))
 if width!=4 or timing['stored_frames']!=result['frames'] or timing['nominal_rate']!=rate:raise ValueError('Capture metadata mismatch')
 values=[x/2147483648. for x in raw[best['capture_channel']-1::channels]]
 start=round(best['start_s']*rate);segment=values[start:start+rate//2];n=len(segment)
 mean=sum(segment)/n;weighted=[(v-mean)*(.5-.5*math.cos(2*math.pi*i/(n-1))) for i,v in enumerate(segment)]
 # Narrow peak fit uses a 0.01Hz grid, not a claim of 0.01Hz measurement accuracy.
 frequency=max((amplitude(weighted,rate,440+step/100),440+step/100) for step in range(-50,51))[1]
 c=[math.cos(2*math.pi*frequency*i/rate) for i in range(n)];s=[math.sin(2*math.pi*frequency*i/rate) for i in range(n)]
 cc=sum(v*v for v in c);ss=sum(v*v for v in s);cs=sum(x*y for x,y in zip(c,s))
 yc=sum((v-mean)*x for v,x in zip(segment,c));ys=sum((v-mean)*x for v,x in zip(segment,s));det=cc*ss-cs*cs
 a=(yc*ss-ys*cs)/det;b=(ys*cc-yc*cs)/det
 fundamental=math.hypot(a,b)/math.sqrt(2)
 residual=[v-mean-a*x-b*y for v,x,y in zip(segment,c,s)];noise=math.sqrt(sum(x*x for x in residual)/n)
 corrected=frequency*timing['host_measured_rate']/rate
 result={'capture_channel':best['capture_channel'],'capture_contiguous':True,'capture_frames':result['frames'],
         'sample_rate':rate,'fundamental_rms_dbfs':db(fundamental),'residual_rms_dbfs':db(noise),
         'signal_over_residual_db':db(fundamental/noise),'estimated_tone_hz_at_nominal_capture_rate':frequency,
         'host_corrected_tone_hz':corrected,'relative_frequency_error_ppm':(corrected/440-1)*1e6,
         'capture_clock_relative_host_ppm':(timing['host_measured_rate']/rate-1)*1e6,
         'window_start_s':start/rate,'window_duration_s':n/rate,'clipped_samples':sum(abs(v)>=.999 for v in values),
         'limitations':'Short low-level analog loopback; host-relative clock estimate, not calibrated absolute clock or isolated DAC distortion.'}
 if driver_log:
  line=Path(driver_log).read_text()
  match=re.search(r'IN submitted=\d+ completed=(\d+) packets=\d+ zero=\d+ bytes=(\d+)',line)
  if match:
   batches,transport_bytes=map(int,match.groups());duration=batches/1000
   inferred=transport_bytes/16/duration
   result.update({'usb_payload_inferred_rate':inferred,'usb_payload_relative_host_ppm':(inferred/48000-1)*1e6,
                  'usb_payload_predicted_tone_hz':440*inferred/48000,
                  'usb_packet_quantization_ppm':11/(48000*duration)*1e6})
 return result
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('wav');p.add_argument('--timing',required=True);p.add_argument('--report',required=True);p.add_argument('--driver-log');a=p.parse_args()
 r=measure(a.wav,a.timing,a.driver_log);text=json.dumps(r,indent=2)+'\n';Path(a.report).write_text(text);print(text,end='')
