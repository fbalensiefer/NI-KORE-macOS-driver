#!/usr/bin/env python3
"""Analyze a captured stereo PCM WAV; no third-party Python dependencies."""
import argparse, json, math, struct, wave
from pathlib import Path

def main():
    p=argparse.ArgumentParser()
    p.add_argument("wav")
    p.add_argument("--expected-hz",type=float,default=440.0)
    p.add_argument("--tolerance-hz",type=float,default=20.0)
    p.add_argument("--min-rms",type=float,default=0.0003)
    p.add_argument("--max-rms",type=float,default=0.35)
    p.add_argument("--report",default="audio-report.json")
    args=p.parse_args()
    with wave.open(args.wav,"rb") as w:
        channels=w.getnchannels(); rate=w.getframerate(); width=w.getsampwidth()
        n=w.getnframes(); data=w.readframes(n)
    if channels!=2 or width!=2 or rate!=48000:
        raise SystemExit(f"Expected 48 kHz stereo 16-bit PCM WAV, got {rate}Hz/{channels}ch/{width*8}bit")
    samples=struct.unpack("<"+"h"*(len(data)//2),data)
    results=[]
    for ch in range(2):
        values=[samples[i]/32768.0 for i in range(ch,len(samples),2)]
        # Ignore 0.5 s of startup and analyze up to 2 s; limit 100k samples.
        start=min(rate//2,len(values)//4)
        v=values[start:start+min(rate*2,len(values)-start)]
        if not v: raise SystemExit("Capture too short")
        rms=math.sqrt(sum(x*x for x in v)/len(v))
        peak=max(abs(x) for x in v)
        # Zero crossings provide a cheap frequency estimate for a clean test sine.
        crossings=[i for i in range(1,len(v)) if v[i-1]<=0<v[i]]
        freq=(len(crossings)-1)*rate/(crossings[-1]-crossings[0]) if len(crossings)>1 else 0
        ok=args.min_rms<=rms<=args.max_rms and abs(freq-args.expected_hz)<=args.tolerance_hz and peak<0.98
        results.append({"channel":ch,"rms":round(rms,6),"peak":round(peak,6),
                        "estimated_frequency_hz":round(freq,2),"pass":ok})
    result={"sample_rate":rate,"channels":channels,"expected_hz":args.expected_hz,
            "measurements":results,"pass":all(x["pass"] for x in results)}
    Path(args.report).write_text(json.dumps(result,indent=2)+"\n")
    print(json.dumps(result,indent=2))
    raise SystemExit(0 if result["pass"] else 1)
if __name__=="__main__":main()
