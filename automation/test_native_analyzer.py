"""Synthetic positive/negative capture controls, independent of USB hardware."""
import importlib.util,math,random,struct,tempfile,wave
from pathlib import Path
spec=importlib.util.spec_from_file_location('analyzer',Path(__file__).with_name('analyze_native_capture.py'))
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
with tempfile.TemporaryDirectory() as folder:
 for signal in (False,True):
  path=Path(folder)/f'{signal}.wav';rng=random.Random(4711);data=bytearray();rate=44100
  for n in range(rate*3):
   row=[]
   for channel in range(4):
    value=rng.gauss(0,1e-6)
    if signal and channel==1 and rate//2<=n<rate*3//2:value+=.0001*math.sin(2*math.pi*440*n/rate)
    row.append(int(value*2147483647))
   data.extend(struct.pack('<4i',*row))
  with wave.open(str(path),'wb') as w:w.setnchannels(4);w.setsampwidth(4);w.setframerate(rate);w.writeframes(data)
  result=m.analyze(path)
  assert [x['tone_detected'] for x in result['measurements']]==([False,True,False,False] if signal else [False]*4)
print('PASS native capture analyzer: synthetic 4-channel positive and noise-only negative controls')
