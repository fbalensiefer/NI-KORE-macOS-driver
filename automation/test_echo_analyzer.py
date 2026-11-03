"""Matched-chirp controls: no echo, 80ms replica, clipping and capture-gap refusal."""
import tempfile,wave,json
from pathlib import Path
import numpy as np
from analyze_echo_capture import analyze
rate=44100;frames=rate*4
n=np.arange(2400);t=n/48000.
s=(.5-.5*np.cos(2*np.pi*n/2399))*np.sin(2*np.pi*(200*t+.5*(9800/.05)*t*t))
pulse=np.interp(np.arange(round(.05*rate))/rate,t,s)
with tempfile.TemporaryDirectory() as folder:
    root=Path(folder);wav=root/'capture.wav';timing=root/'timing.json'
    meta={'capture_contiguous':True,'timestamp_gaps':0,'invalid_callbacks':0,'stored_frames':frames,'nominal_rate':rate,'channels':4}
    def capture(echo=0,clip=False):
        x=np.zeros((frames,4));rng=np.random.default_rng(42);x+=rng.normal(0,1e-6,x.shape)
        for ch,start in enumerate((rate//2,rate*2)):
            x[start:start+len(pulse),ch]+=.1*pulse
            delay=round(.08*rate);x[start+delay:start+delay+len(pulse),ch]+=echo*.1*pulse
        if clip:x[10,0]=1
        with wave.open(str(wav),'wb') as w:
            w.setnchannels(4);w.setsampwidth(4);w.setframerate(rate);w.writeframes((np.clip(x,-1,.999999999)*2147483647).astype('<i4').tobytes())
        timing.write_text(json.dumps(meta))
    capture();assert analyze(wav,timing)['pass']
    capture(.2);result=analyze(wav,timing);assert not result['pass'];assert all(abs(p['delay_ms']-80)<.05 for p in result['paths'])
    capture(clip=True);assert not analyze(wav,timing)['pass']
    meta['timestamp_gaps']=1;capture()
    try:analyze(wav,timing)
    except ValueError:pass
    else:raise AssertionError('Accepted capture gap')
    meta['timestamp_gaps']=0;meta['nominal_rate']=48000;capture()
    try:analyze(wav,timing)
    except ValueError:pass
    else:raise AssertionError('Accepted sample-rate metadata mismatch')
print('PASS echo analyzer: known delayed-copy detection, no-echo control, clipping and gap refusal')
