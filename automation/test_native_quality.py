"""Synthetic quality measurements and refusal of discontinuous capture metadata."""
import math,random,struct,tempfile,wave,json
from pathlib import Path
from measure_native_quality import measure
with tempfile.TemporaryDirectory() as folder:
 root=Path(folder);wav=root/'test.wav';timing=root/'timing.json';rate=44100;rng=random.Random(4711)
 data=bytearray()
 for n in range(rate*3):
  values=[rng.gauss(0,1e-6) for _ in range(4)]
  if rate//2<=n<rate*3//2:values[1]+=.0001*math.sin(2*math.pi*440.08*n/rate)
  data.extend(struct.pack('<4i',*[int(v*2147483647) for v in values]))
 with wave.open(str(wav),'wb') as w:w.setnchannels(4);w.setframerate(rate);w.setsampwidth(4);w.writeframes(data)
 meta={'capture_contiguous':True,'timestamp_gaps':0,'invalid_callbacks':0,'stored_frames':rate*3,'nominal_rate':rate,'host_measured_rate':rate}
 timing.write_text(json.dumps(meta));result=measure(wav,timing)
 assert abs(result['estimated_tone_hz_at_nominal_capture_rate']-440.08)<=.02,result
 assert result['capture_channel']==2 and result['clipped_samples']==0
 meta['timestamp_gaps']=1;timing.write_text(json.dumps(meta))
 try:measure(wav,timing)
 except ValueError:pass
 else:raise AssertionError('Must reject discontinuous capture')
print('PASS quality measurements: injected clock offset, channel isolation, gap refusal')
