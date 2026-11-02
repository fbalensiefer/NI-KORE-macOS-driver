#!/usr/bin/env python3
"""Frequency-coded L/R/dual test; reports capture mapping without assuming cable pinout."""
import argparse,array,json,math,statistics,sys,wave
from pathlib import Path
from analyze_native_capture import amplitude,db

def analyze(wav_path,timing_path):
    timing=json.loads(Path(timing_path).read_text())
    if not timing.get('capture_contiguous') or timing.get('timestamp_gaps') or timing.get('invalid_callbacks'):
        raise ValueError('Discontinuous capture; stereo result refused')
    with wave.open(str(wav_path),'rb') as w:
        channels,rate,width,frames=w.getnchannels(),w.getframerate(),w.getsampwidth(),w.getnframes()
        if width!=4:raise ValueError('Expected signed32 capture')
        raw=array.array('i');raw.frombytes(w.readframes(frames))
    if sys.byteorder!='little':raw.byteswap()
    if len(raw)!=frames*channels:raise ValueError('Truncated stereo capture')
    if timing.get('channels',channels)!=channels:raise ValueError('Capture channel metadata mismatch')
    if timing.get('stored_frames')!=frames or timing.get('nominal_rate')!=rate:raise ValueError('Capture metadata mismatch')
    values=[[v/2147483648. for v in raw[ch::channels]] for ch in range(channels)]
    window=rate//4;hop=rate//8;centers=[(n+window/2)/rate for n in range(0,frames-window+1,hop)]
    spectra=[[[amplitude(v[n:n+window],rate,hz) for n in range(0,frames-window+1,hop)] for hz in (440,660)] for v in values]
    def level(ch,freq,start,end):
        selected=[v for t,v in zip(centers,spectra[ch][freq]) if start<=t<=end]
        return statistics.median(selected) if selected else 0.
    def active_min(ch,freq,base,start,end):
        selected=[v for t,v in zip(centers,spectra[ch][freq]) if base+start<=t<=base+end or base+3.65<=t<=base+4.35]
        return min(selected) if selected else 0.
    best=None
    # Coarse alignment tolerates arbitrary capture/bridge startup latency.
    for step in range(max(0,int((frames/rate-4.5)/.025))+1):
        base=step*.025
        left=[active_min(ch,0,base,.65,1.35) for ch in range(channels)]
        right=[active_min(ch,1,base,2.15,2.85) for ch in range(channels)]
        for l in range(channels):
            for r in range(channels):
                if l==r:continue
                # Penalize wrong time signatures and missing-channel noise maxima.
                off_l=level(l,0,base+2.15,base+2.85)
                off_r=level(r,1,base+.65,base+1.35)
                score=max(0,left[l]-off_l)*max(0,right[r]-off_r)
                if best is None or score>best[0]:best=(score,base,l,r)
    if best is None:raise ValueError('Stereo capture requires at least two input channels')
    _,base,left,right=best
    paths=[];passed=True
    for name,ch,other,freq,on_start,on_end,off_start,off_end in [('left',left,right,0,.65,1.35,2.15,2.85),('right',right,left,1,2.15,2.85,.65,1.35)]:
        on=level(ch,freq,base+on_start,base+on_end);dual=level(ch,freq,base+3.65,base+4.35)
        off=level(ch,freq,base+off_start,base+off_end);leak=level(other,freq,base+on_start,base+on_end)
        contrast=db(on/max(off,1e-15));separation=db(on/max(leak,1e-15));balance=db(dual/max(on,1e-15))
        windows=[v for t,v in zip(centers,spectra[ch][freq]) if base+on_start<=t<=base+on_end or base+3.65<=t<=base+4.35]
        reference=statistics.median(windows) if windows else 0
        dips=[round(t,4) for t,v in zip(centers,spectra[ch][freq]) if (base+on_start<=t<=base+on_end or base+3.65<=t<=base+4.35) and reference and v<reference*.7]
        ok=db(on)>-100 and contrast>15 and separation>15 and abs(balance)<3 and not dips
        passed &= ok
        paths.append({'virtual_channel':name,'frequency_hz':440 if freq==0 else 660,'capture_channel':ch+1,'tone_peak_dbfs':db(on),'on_off_contrast_db':contrast,'separation_from_other_input_db':separation,'dual_vs_solo_db':balance,'suspect_dip_window_centers_s':dips,'pass':ok})
    clipped=sum(abs(v)>=.999 for ch in values for v in ch)
    return {'capture_contiguous':True,'capture_frames':frames,'sample_rate':rate,'alignment_s':base,'paths':paths,'clipped_samples':clipped,'pass':bool(passed and not clipped),'quality_qualified':False,'solo_level_difference_db':paths[0]['tone_peak_dbfs']-paths[1]['tone_peak_dbfs'],'limitations':'Signature recognition uses a 15dB separation threshold, not a quality target. 250ms spectral windows, 125ms hop; separation is noise-floor limited. Cable pinout/polarity and calibrated clock accuracy are not inferred.'}
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('wav');p.add_argument('timing');p.add_argument('--report');a=p.parse_args()
    result=analyze(a.wav,a.timing);output=json.dumps(result,indent=2)+'\n';print(output,end='')
    if a.report:Path(a.report).write_text(output)
    raise SystemExit(0 if result['pass'] else 1)
