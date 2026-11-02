#!/usr/bin/env python3
"""Opt-in KORE 1 96-byte USB OUT experiment; leaves existing mode unchanged."""
from pathlib import Path
import sys
p=Path(sys.argv[1] if len(sys.argv)>1 else 'kore1-bridge.c')
s=p.read_text()
if 'static int audio_out_96=0;' in s:
    raise SystemExit('Already patched; no changes made')
def replace(old,new):
    global s
    count=s.count(old)
    if count!=1: raise SystemExit(f'Expected one occurrence, found {count}: {old[:100]!r}')
    s=s.replace(old,new,1)
replace('static int audio_out_fixed=0;', 'static int audio_out_96=0; /* opt-in 96-byte ISO OUT; keep AUDIO_PARAMS bpp=176 */\nstatic int audio_out_fixed=0;')
replace('slot->buffer+k*(unsigned)audio_bpp,(unsigned)audio_bpp);', 'slot->buffer+k*(audio_out_96?96u:(unsigned)audio_bpp),audio_out_96?96u:(unsigned)audio_bpp);')
replace('const unsigned tx_n=audio_independent_short?2u:(unsigned)audio_bpp;', 'const unsigned tx_n=audio_independent_short?2u:(audio_out_96?96u:(unsigned)audio_bpp);')
replace('slot->transfer->length=(int)off;\n        slot->busy=1;', 'slot->transfer->length=(int)off;\n        slot->busy=1;') # guard presence
replace('puts("--audio-out-fixed (v0.13 opt-in: send full audio_bpp bytes per OUT microframe, independent of IN lengths; requires duplex).");', 'puts("--audio-out-fixed (v0.13 opt-in: send full audio_bpp bytes per OUT microframe, independent of IN lengths; requires duplex).");\n    puts("--audio-out-96 (EXPERIMENTAL: with --headphone-tone or --pcm-fifo, transmit 96-byte iso OUT packets; AUDIO_PARAMS bpp remains 176).");')
replace('else if(!strcmp(argv[i],"--audio-out-fixed")) audio_out_fixed=1;', 'else if(!strcmp(argv[i],"--audio-out-fixed")) audio_out_fixed=1;\n        else if(!strcmp(argv[i],"--audio-out-96")) audio_out_96=1;')
replace('if(hp_tone) {puts("WARNING:', 'if(audio_out_96 && (!audio_out_fixed || !audio_tx_independent || (!hp_tone && !pcm_fifo_mode) || audio_independent_short || audio_start_prime)) {fprintf(stderr,"--audio-out-96 requires --headphone-tone or --pcm-fifo in independent fixed OUT mode\\n");return 2;}\n    if(hp_tone) {puts("WARNING:')
replace('puts("AUDIO v0.15 EXPERIMENT: OUT length=176 per microframe; PCM format and checksum remain unverified.");', 'if(audio_out_96) puts("AUDIO EXPERIMENT: OUT length=96 per microframe; AUDIO_PARAMS bpp=176; device interpretation unverified.");\n        else puts("AUDIO v0.15 EXPERIMENT: OUT length=176 per microframe; PCM format and checksum remain unverified.");')
replace('audio_out_fixed?"FIXED 176-byte diagnostic":"legacy matched IN lengths"', 'audio_out_96?"FIXED 96-byte experimental":"FIXED 176-byte diagnostic"')
# Critical: libusb transfer buffer remains allocated at 8*512 bytes, but actual submitted transfer length must match sum of descriptors.
# No changes to AUDIO_PARAMS handshake, no changes to default framing, no changes to independent short packets.
p.write_text(s)
print(f'Patched {p}; --audio-out-96 is opt-in')
