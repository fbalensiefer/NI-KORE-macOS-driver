#!/usr/bin/env python3
import math, struct, subprocess, sys, tempfile, wave
from pathlib import Path
with tempfile.TemporaryDirectory() as tmp:
    path=Path(tmp)/"test.wav"
    with wave.open(str(path),"wb") as w:
        w.setnchannels(2);w.setsampwidth(2);w.setframerate(48000)
        buf=bytearray()
        for i in range(48000*3):
            v=int(1200*math.sin(2*math.pi*440*i/48000))
            buf+=struct.pack("<hh",v,v)
        w.writeframes(buf)
    subprocess.run([sys.executable,str(Path(__file__).with_name("check_audio.py")),
                    str(path),"--report",str(Path(tmp)/"report.json")],check=True)
print("Synthetic audio analyzer test passed")
