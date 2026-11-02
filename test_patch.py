from pathlib import Path
import unittest, tempfile, subprocess, sys
class PatchTest(unittest.TestCase):
 def test_patch(self):
  src=Path('kore1-bridge.c').read_text()
  with tempfile.TemporaryDirectory() as d:
   f=Path(d)/'kore1-bridge.c'; f.write_text(src)
   subprocess.run([sys.executable,str(Path(__file__).with_name('apply_kore_96.py')),str(f)],check=True,capture_output=True)
   out=f.read_text()
   self.assertIn('audio_out_96?96u:(unsigned)audio_bpp',out)
   self.assertIn('audio_out_96?96u:(unsigned)audio_bpp)',out)
   self.assertIn('slot->transfer->length=(int)off;',out)
   self.assertIn('unsigned char command[]={0x09,0x00,0x01,0x02,',out)
   self.assertIn('if(audio_out_96 && (!audio_out_fixed',out)
   self.assertIn('audio_out_96?"FIXED 96-byte experimental"',out)
   self.assertEqual(out.count('static int audio_out_96=0;'),1)
if __name__=='__main__': unittest.main()
