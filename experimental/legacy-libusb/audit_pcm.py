#!/usr/bin/env python3
"""Offline audit of the current KORE bridge's 2-stream alignment=2 packet layout.
No USB hardware needed. Does not establish parity with Linux snd-usb-caiaq.
"""
from pathlib import Path
import re, sys
p=Path(sys.argv[1]) if len(sys.argv)>1 else Path('kore1-bridge-single176.c')
s=p.read_text()
for token in ('linux_pcm_sample_byte[stream]=(pos+1u)%6u;', 'if(i%(2u*4u)==2u*2u)', 'if(byte==0) linux_make_pcm_sample'):
    if token not in s: raise SystemExit(f'FAIL: expected source pattern absent: {token}')
print(f'Source: {p}')
print('Assumptions: 2 interleaved streams, 24-bit samples, 2 channels/stream, checkbyte pair per 8 output bytes.')
for n in (96,176):
    assert n%8==0
    blocks=n//8
    pcm_bytes_per_stream=blocks*3
    stereo_frames_per_stream=pcm_bytes_per_stream/6
    sample_rate=stereo_frames_per_stream*8000
    print(f'OUT={n} bytes/microframe: {blocks} checkbyte pairs, {pcm_bytes_per_stream} PCM bytes/stream, {stereo_frames_per_stream:g} stereo frames/stream/microframe => {sample_rate:,.0f} frames/s at 8000 microframes/s')
print('Expected nominal 48 kHz at 8k microframes/s: 6 stereo frames/stream/microframe => 96 bytes OUT/microframe.')
print('176 bytes is a configured maximum/threshold in this experiment, NOT evidence of a required constant OUT payload.')
print('WARNING: The current source increments its phase for each generated channel sample, and the single176 compensation is experimental; this is not proof of correct waveform generation.')
print('WARNING: The current matched-length path skips any 8-packet batch containing a zero-length IN packet. Prior logs show many zero-length IN packets, so that path is not a viable Linux parity test.')
print('NEXT: obtain exact Linux audio.c frame packing and OUT callback behavior before changing live USB scheduling.')
