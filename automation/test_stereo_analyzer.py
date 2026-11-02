"""Synthetic stereo evidence, missing channel, mono duplication and gap refusal."""
import array,json,math,random,tempfile,wave
from pathlib import Path
from analyze_stereo_capture import analyze
rate=8000;frames=rate*6
with tempfile.TemporaryDirectory() as directory:
    root=Path(directory);timing=root/'timing.json';wav=root/'test.wav'
    report={'capture_contiguous':True,'stored_frames':frames,'nominal_rate':rate,'channels':4,'timestamp_gaps':0,'invalid_callbacks':0};timing.write_text(json.dumps(report))
    def make(mode):
        rng=random.Random(10);samples=array.array('i')
        for n in range(frames):
            t=n/rate-.4;l=r=0.
            if .5<=t<1.5 or 3.5<=t<4.5:l=.0001*math.sin(2*math.pi*440*t)
            if 2<=t<3 or 3.5<=t<4.5:r=.0001*math.sin(2*math.pi*660*t)
            if mode=='missing':r=0
            if mode=='dropout' and .95<=t<1.2:l=0
            if mode=='mono':l=r=l+r
            values=[r,l,0.,0.]
            for value in values:samples.append(round((value+rng.uniform(-.000001,.000001))*2147483647))
        with wave.open(str(wav),'wb') as w:w.setnchannels(4);w.setsampwidth(4);w.setframerate(rate);w.writeframes(samples.tobytes())
    make('stereo');result=analyze(wav,timing);assert result['pass'],result
    assert [p['capture_channel'] for p in result['paths']]==[2,1]
    make('missing');assert not analyze(wav,timing)['pass']
    make('mono');assert not analyze(wav,timing)['pass']
    make('dropout');assert not analyze(wav,timing)['pass']
    report['timestamp_gaps']=1;timing.write_text(json.dumps(report))
    try:analyze(wav,timing)
    except ValueError:pass
    else:raise AssertionError('Capture gaps accepted')
print('PASS stereo analyzer: swapped capture mapping, missing-channel/mono/dropout negatives and capture-gap refusal')
