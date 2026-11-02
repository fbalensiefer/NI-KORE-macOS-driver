#!/usr/bin/env python3
"""All-channel PCM WAV tone search. Reports measured evidence, not DAC attribution."""
import argparse,array,math,json,sys,wave
from pathlib import Path

def db(value):return 20*math.log10(max(value,1e-15))
def amplitude(values,rate,hz):
 # Goertzel: coherent peak amplitude, including arbitrary phase.
 coeff=2*math.cos(2*math.pi*hz/rate);a=b=0.
 mean=sum(values)/len(values)
 for value in values:
  c=value-mean+coeff*a-b;b=a;a=c
 return 2*math.sqrt(max(0,a*a+b*b-coeff*a*b))/len(values)
def analyze(path):
 with wave.open(str(path),'rb') as wav:
  channels,rate,width,n=wav.getnchannels(),wav.getframerate(),wav.getsampwidth(),wav.getnframes()
  if width not in (2,4):raise ValueError('Expected signed16 or signed32 PCM WAV')
  raw=array.array('h' if width==2 else 'i');raw.frombytes(wav.readframes(n))
  if sys.byteorder!='little':raw.byteswap()
 scale=float(2**(8*width-1));results=[];window=rate//2;hop=rate//8
 for channel in range(channels):
  values=[v/scale for v in raw[channel::channels]]
  peak=max(map(abs,values),default=0)
  best=None
  for start in range(0,len(values)-window+1,hop):
   segment=values[start:start+window]
   tone=amplitude(segment,rate,440)/math.sqrt(2)
   neighbors=[amplitude(segment,rate,hz)/math.sqrt(2) for hz in (420,430,450,460)]
   noise=max(neighbors+[1e-15]);snr=db(tone/noise)
   entry={'start_s':start/rate,'tone_rms_dbfs':db(tone),'neighbor_contrast_db':snr}
   if best is None or tone>best[0]:best=(tone,entry)
  if best:
   start=round(best[1]['start_s']*rate);segment=values[start:start+window]
   # Refine near the target only; this is not a sample-clock accuracy measurement.
   best[1]['peak_near_440_hz']=max((amplitude(segment,rate,step/4),step/4) for step in range(1744,1777))[1]
  baseline=amplitude(values[:window],rate,440)/math.sqrt(2) if len(values)>=window else 0
  entry=best[1] if best else {};tone=best[0] if best else 0
  results.append({'capture_channel':channel+1,'peak_dbfs':db(peak),'baseline_440_dbfs':db(baseline),**entry,
                  'tone_detected':bool(best and db(tone)>-100 and best[1]['neighbor_contrast_db']>15 and db(tone)-db(baseline)>10)})
 return {'file':str(path),'sample_rate':rate,'channels':channels,'frames':n,'duration_s':n/rate,'measurements':results}
def main():
 parser=argparse.ArgumentParser();parser.add_argument('wav',type=Path);parser.add_argument('--report',type=Path)
 args=parser.parse_args();result=analyze(args.wav);output=json.dumps(result,indent=2)+'\n';print(output,end='')
 if args.report:args.report.write_text(output)
if __name__=='__main__':main()
