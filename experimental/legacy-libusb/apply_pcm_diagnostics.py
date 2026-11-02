from pathlib import Path
import sys
p=Path(sys.argv[1] if len(sys.argv)>1 else 'kore1-bridge.c')
s=p.read_text()
marker='static void linux_fill_pcm(unsigned char *buf, unsigned len, int tone) {'
if marker not in s: raise SystemExit('ABORT: linux_fill_pcm signature not found')
if 'KORE_PCM_DIAG_V1' in s: raise SystemExit('ABORT: diagnostics already installed')
pre='''/* KORE_PCM_DIAG_V1: passive encoder instrumentation; no changes to USB payload. */
static uint64_t diag_tone_packets=0, diag_tone_pcm_bytes=0, diag_tone_nonzero=0;
static int diag_first_tone_packet=0;
'''
s=s.replace(marker,pre+marker,1)
old='buf[i++]=(tone || pcm_fifo_mode)?linux_pcm_sample[stream][chan][byte]:0;'
new='''unsigned char out=(tone || pcm_fifo_mode)?linux_pcm_sample[stream][chan][byte]:0;
                buf[i++]=out;
                if(tone && stream==0) {diag_tone_pcm_bytes++;if(out) diag_tone_nonzero++;}'''
if s.count(old)!=1: raise SystemExit('ABORT: expected PCM write line not unique')
s=s.replace(old,new,1)
# Add first packet dump after fill function, before next function.
anchor='\nstatic void hp_fill_packet(unsigned char *buf, unsigned len) {'
if anchor not in s: raise SystemExit('ABORT: hp_fill_packet anchor not found')
s=s.replace(anchor,'''\n/* End of linux_fill_pcm() is immediately above this comment. */
'''+anchor,1)
# Need add code at end of linux_fill_pcm, matching braces with scanner
start=s.index(marker)
brace=s.index('{',start)
depth=0;end=None
for j in range(brace,len(s)):
    if s[j]=='{': depth+=1
    elif s[j]=='}':
        depth-=1
        if depth==0: end=j;break
if end is None: raise SystemExit('ABORT: unmatched function braces')
post='''
    if(tone) {
        diag_tone_packets++;
        if(!diag_first_tone_packet) {
            diag_first_tone_packet=1;
            fprintf(stderr,"PCM_DIAG first tone packet length=%u first_32=",len);
            for(unsigned j=0;j<len && j<32;j++) fprintf(stderr,"%02x%s",buf[j],j+1<len && j<31?" ":"");
            fputc('\\n',stderr);
        }
    }
'''
s=s[:end]+post+s[end:]
# Add final report immediately before existing hp_tone report
anchor='if(hp_tone) fprintf(stderr,"HP_TONE samples='
if anchor not in s: raise SystemExit('ABORT: final report anchor not found')
s=s.replace(anchor,'''fprintf(stderr,"PCM_DIAG tone_packets=%llu pcm_stream0_bytes=%llu nonzero=%llu frames_stream0=%llu\\n",
        (unsigned long long)diag_tone_packets,(unsigned long long)diag_tone_pcm_bytes,
        (unsigned long long)diag_tone_nonzero,(unsigned long long)linux_pcm_frames);
    '''+anchor,1)
backup=p.with_suffix(p.suffix+'.pre-pcm-diag')
if backup.exists(): raise SystemExit(f'ABORT: backup already exists: {backup}')
backup.write_text(p.read_text())
p.write_text(s)
print('Patched',p,'; backup:',backup)
