from pathlib import Path
import unittest, tempfile, subprocess, sys

class PatchTest(unittest.TestCase):
 def test_historical_patch(self):
  # This utility targets its original snapshot, not the current bridge.
  src='static int audio_out_fixed=0;\nslot->buffer+k*(unsigned)audio_bpp,(unsigned)audio_bpp);\nconst unsigned tx_n=audio_independent_short?2u:(unsigned)audio_bpp;\nslot->transfer->length=(int)off;\n        slot->busy=1;\nputs("--audio-out-fixed (v0.13 opt-in: send full audio_bpp bytes per OUT microframe, independent of IN lengths; requires duplex).");\nelse if(!strcmp(argv[i],"--audio-out-fixed")) audio_out_fixed=1;\nif(hp_tone) {puts("WARNING:\nputs("AUDIO v0.15 EXPERIMENT: OUT length=176 per microframe; PCM format and checksum remain unverified.");\naudio_out_fixed?"FIXED 176-byte diagnostic":"legacy matched IN lengths"\nunsigned char command[]={0x09,0x01,0x02,\n'
  with tempfile.TemporaryDirectory() as d:
   f=Path(d)/'bridge.c'; f.write_text(src)
   subprocess.run([sys.executable,str(Path(__file__).resolve().parent/'experimental/legacy-libusb/apply_kore_96.py'),str(f)],check=True,capture_output=True)
   out=f.read_text()
   self.assertIn('audio_out_96?96u:(unsigned)audio_bpp',out)
   self.assertIn('slot->transfer->length=(int)off;',out)
   self.assertIn('unsigned char command[]={0x09,0x01,0x02,',out)
   self.assertNotIn('0x09,0x00,0x01,0x02,',out)
   self.assertEqual(out.count('static int audio_out_96=0;'),1)
   again=subprocess.run([sys.executable,str(Path(__file__).resolve().parent/'experimental/legacy-libusb/apply_kore_96.py'),str(f)],capture_output=True)
   self.assertNotEqual(again.returncode,0)
   self.assertEqual(f.read_text(),out)
if __name__=='__main__': unittest.main()
