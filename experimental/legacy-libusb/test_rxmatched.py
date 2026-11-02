#!/usr/bin/env python3
"""Offline model of the opt-in RX-length matched scheduling policy."""
from pathlib import Path
s=Path(__file__).with_name('kore1-bridge-rxmatched.c').read_text()
for snippet in ('audio_rx_matched_zero=0', 'if(!audio_rx_matched_zero && !audio_out_fixed', 'if(!any) {if(audio_rx_matched_zero)', 'slot->transfer->iso_packet_desc[i].length=n;'):
    assert snippet in s, snippet

def schedule(lengths):
    assert len(lengths)==8
    if not any(lengths): return None
    assert all(n in (0,176) for n in lengths)
    return {'descriptors':lengths[:], 'payload_bytes':sum(lengths), 'zero_descriptors':lengths.count(0)}

for lengths in ([176]*8, [0]*8, [176,0,176,0,0,176,0,176], [0]*7+[176]):
    result=schedule(lengths)
    print(f'IN={lengths} -> {result}')
assert schedule([176,0,176,0,0,176,0,176])['payload_bytes']==704
assert schedule([0]*8) is None
print('PASS: offline descriptor/payload accounting and source guards')
