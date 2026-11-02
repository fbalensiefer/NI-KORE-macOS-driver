#!/usr/bin/env python3
"""Offline structural audit of the current linux_fill_pcm() algorithm.
No USB access, audio playback or third-party packages.
"""
from pathlib import Path
import re

source = Path(__file__).with_name('kore1-bridge-96.c').read_text()
assert 'if(i%(2u*4u)==2u*2u)' in source, 'PCM framing changed; inspect C source before relying on this audit'
assert 'linux_pcm_sample_byte[stream]=(pos+1u)%6u;' in source, 'Sample state changed'
assert 'for(int stream=0;stream<2 && i<len;stream++)' in source, 'Stream interleave changed'

def simulate(n, pos=None):
    if pos is None: pos=[0,0]
    b=bytearray(n)
    checks=[]
    samples=[0,0]
    i=0
    while i<n:
        for stream in range(2):
            if i>=n: break
            old=pos[stream]
            # Unique marker for every stream/channel/byte position.
            b[i]=0x40+stream*32+old
            pos[stream]=(old+1)%6
            if old==5: samples[stream]+=1
            i+=1
        if i%8==4:
            for stream in range(2):
                if i>=n: break
                b[i]=((stream<<1)|(~(i//8)&1))&255
                checks.append(i)
                i+=1
    return b,checks,samples,pos

for n in (96,176):
    pos=[0,0]
    check_positions=[]
    frames=0
    for packet in range(8):
        b,checks,samples,pos=simulate(n,pos)
        assert checks==[j for k in range(4,n,8) for j in (k,k+1)], (n,checks)
        assert len(b)==n
        check_positions.extend(checks)
        frames+=samples[0]
    rate=frames*1000 # 8 microframes = 1ms
    print(f'{n}-byte OUT: {n//8} checkbyte pairs/block per microframe; check positions 4,5,12,13,...')
    print(f'  8 microframes: {frames} complete stereo frames on stream 0; nominal rate {rate:,} frames/s')
    print(f'  check bytes per packet: {len(checks)}; stream positions after 8 packets: {pos}')
    if n==96:
        assert rate==48000, rate
        print('  48 kHz equivalent, matching configured sample rate (framing still unverified).')
print('PASS: structural checks; does not validate actual KORE hardware protocol.')
