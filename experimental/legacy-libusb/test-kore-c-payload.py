#!/usr/bin/env python3
"""Compile and execute the actual linux_fill_pcm() function without USB/macOS dependencies."""
from pathlib import Path
import subprocess, tempfile, shutil
src=Path(__file__).with_name('kore1-bridge-96.c').read_text()
needle='static void linux_fill_pcm(unsigned char *buf, unsigned len, int tone) {'
assert src.count(needle)==1
start=src.index(needle); opening=src.index('{',start); depth=0; end=None
for i in range(opening,len(src)):
    if src[i]=='{': depth+=1
    elif src[i]=='}':
        depth-=1
        if depth==0: end=i+1;break
assert end
fn=src[start:end]
assert 'linux_pcm_sample_byte[stream]=(pos+1u)%6u;' in fn
assert 'if(i%(2u*4u)==2u*2u)' in fn
pre='''#include <stdio.h>\n#include <stdint.h>\n#include <string.h>\n#include <assert.h>\nstatic int audio_alignment=2,audio_streams=2,pcm_fifo_mode=0;\nstatic unsigned linux_pcm_sample_byte[2]={0,0};\nstatic unsigned char linux_pcm_sample[2][2][3];\nstatic unsigned frames[2][2];\nstatic void linux_make_pcm_sample(int stream,int chan,double frequency) {\n (void)frequency; unsigned n=frames[stream][chan]++;\n for(int b=0;b<3;b++) linux_pcm_sample[stream][chan][b]=(unsigned char)(0x40+stream*32+chan*8+b+(n%8)*0);\n}\n'''
post='''
int main(void) {
 unsigned char buf[96]; unsigned long checks=0;
 for(int packet=0;packet<8;packet++) {
  linux_fill_pcm(buf,96,1);
  for(int i=0;i<96;i+=8) {
   unsigned idx=(unsigned)i/8;
   assert(buf[i+0]==0x40 && buf[i+1]==0x60);
   assert(buf[i+2]==0x41 && buf[i+3]==0x61);
   assert(buf[i+4]==((~idx)&1u));
   assert(buf[i+5]==(2u|((~idx)&1u)));
   assert(buf[i+6]==0x42 && buf[i+7]==0x62 ||
          buf[i+6]==0x48 && buf[i+7]==0x68 ||
          buf[i+6]==0x49 && buf[i+7]==0x69 ||
          buf[i+6]==0x4a && buf[i+7]==0x6a);
   checks+=2;
  }
 }
 printf("PASS C payload: 8 x 96 bytes; %lu checkbytes; stream byte positions %u,%u\\n",checks,linux_pcm_sample_byte[0],linux_pcm_sample_byte[1]);
 return 0;
}
'''
# Expected PCM alternates channels every 3 PCM bytes, with 4 bytes per stream per 8-byte block.
post='''
int main(void) {
 unsigned char buf[96]; unsigned long checks=0;
 unsigned expected_pos[2]={0,0};
 for(int packet=0;packet<8;packet++) {
  linux_fill_pcm(buf,96,1);
  for(int i=0;i<96;i+=8) {
   for(int b=0;b<4;b++) {
    unsigned stream=(unsigned)b%2;
    unsigned pos=expected_pos[stream];
    unsigned expected=0x40+stream*32+((pos/3u)&1u)*8+(pos%3u);
    assert(buf[i+b]==expected);
    expected_pos[stream]=(pos+1u)%6u;
   }
   unsigned idx=(unsigned)i/8;
   assert(buf[i+4]==((~idx)&1u));
   assert(buf[i+5]==(2u|((~idx)&1u)));
   checks+=2;
   for(int b=6;b<8;b++) {
    unsigned stream=(unsigned)b%2;
    unsigned pos=expected_pos[stream];
    unsigned expected=0x40+stream*32+((pos/3u)&1u)*8+(pos%3u);
    assert(buf[i+b]==expected);
    expected_pos[stream]=(pos+1u)%6u;
   }
  }
 }
 assert(expected_pos[0]==0 && expected_pos[1]==0);
 printf("PASS: actual C linux_fill_pcm: 8 x 96 bytes, %lu checkbytes, 48 stereo frames per stream, continuity OK\\n",checks);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
    c=Path(d)/'audit.c'; exe=Path(d)/'audit';c.write_text(pre+fn+post)
    compiler=shutil.which('cc') or shutil.which('clang')
    assert compiler,'No C compiler found'
    subprocess.run([compiler,'-std=c11','-Wall','-Wextra','-Werror',str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
