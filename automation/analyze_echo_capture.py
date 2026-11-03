#!/usr/bin/env python3
"""Optional NumPy matched-chirp diagnostic; not an audio-quality certification."""
import json,wave
from pathlib import Path
import numpy as np

def analyze(wav,timing):
    meta=json.loads(Path(timing).read_text())
    if not meta.get('capture_contiguous') or meta.get('timestamp_gaps') or meta.get('invalid_callbacks'):
        raise ValueError('Discontinuous capture; echo analysis refused')
    with wave.open(str(wav)) as w:
        rate,channels,frames,width=w.getframerate(),w.getnchannels(),w.getnframes(),w.getsampwidth()
        data=w.readframes(frames)
    if width!=4 or len(data)!=frames*channels*4 or meta['stored_frames']!=frames or meta['nominal_rate']!=rate or meta['channels']!=channels:
        raise ValueError('Capture format/metadata mismatch')
    x=np.frombuffer(data,dtype='<i4').reshape(-1,channels)/2147483648.
    clipped=int(np.sum(np.abs(x)>=.999))
    # Same 48k sample grid as the renderer, linearly resampled to ADC nominal rate.
    n=np.arange(2400);t=n/48000.
    pulse=(.5-.5*np.cos(2*np.pi*n/2399))*np.sin(2*np.pi*(200*t+.5*(9800/.05)*t*t))
    t_adc=np.arange(round(.05*rate))/rate
    ref=np.interp(t_adc,t,pulse)
    size=1<<(frames+len(ref)-1).bit_length();spectrum=np.fft.rfft(ref,size)
    paths=[]
    for ch in range(min(2,channels)):
        corr=np.fft.irfft(np.fft.rfft(x[:,ch],size)*np.conj(spectrum),size)[:frames-len(ref)+1]
        peak=int(np.argmax(np.abs(corr)));strength=float(abs(corr[peak]));energy=float(np.sum(ref**2))
        lo,hi=peak+round(.005*rate),min(len(corr),peak+round(.250*rate))
        if hi<=lo:raise ValueError('Missing post-pulse window')
        delayed=lo+int(np.argmax(np.abs(corr[lo:hi])));ratio=abs(corr[delayed])/max(strength,1e-15)
        db=lambda v:float(20*np.log10(max(float(v),1e-15)))
        paths.append({'capture_channel':ch+1,'pulse_start_s':peak/rate,'correlated_amplitude_dbfs':db(strength/energy),
                      'strongest_delayed_correlation_db':db(ratio),'delay_ms':(delayed-peak)*1000/rate,
                      'pass':bool(db(strength/energy)>-80 and db(ratio)<-40)})
    if len(paths)!=2:raise ValueError('Need two capture inputs')
    return {'pass':bool(not clipped and all(p['pass'] for p in paths)), 'capture_contiguous':True,'capture_frames':frames,
            'clipped_samples':clipped,'paths':paths,'quality_qualified':False,
            'limitations':'50ms chirp matched correlation; checks delayed replicas 5..250ms below -40dB relative correlation. Not a calibrated impulse response, headphone-load test or full frequency-response/distortion measurement. ADC/DAC clock mismatch and filtering affect the limit.'}
