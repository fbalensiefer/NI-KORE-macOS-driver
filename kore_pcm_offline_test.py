#!/usr/bin/env python3
"""Offline diagnostic model of kore1-bridge.c linux_fill_pcm (no USB access).

This reproduces the current alignment=2, streams=2 tone path for fixed
176-byte packets. It does NOT validate the KORE firmware's expected protocol.
"""
import math
from collections import Counter

PACKET_BYTES = 176
MICROFRAMES_PER_SECOND = 8000
TARGET_RATE = 48000

class Encoder:
    def __init__(self):
        self.sample_byte = [0, 0]
        self.sample = [[[0, 0, 0] for _ in range(2)] for _ in range(2)]
        self.phase = 0.0
        self.frames = 0

    def make_sample(self, stream, channel):
        if stream != 0:
            self.sample[stream][channel] = [0, 0, 0]
            return
        value = int(math.sin(self.phase) * 8192.0)
        self.sample[stream][channel] = [(value >> 16) & 255,
                                        (value >> 8) & 255,
                                        value & 255]
        if channel == 1:
            self.phase += 2 * math.pi * 440 / TARGET_RATE
            if self.phase >= 2 * math.pi:
                self.phase -= 2 * math.pi
            self.frames += 1

    def fill(self, length=PACKET_BYTES, tone=True):
        out = bytearray(length)
        i = 0
        while i < length:
            for stream in range(2):
                if i >= length:
                    break
                pos = self.sample_byte[stream]
                chan = (pos // 3) & 1
                byte = pos % 3
                if byte == 0:
                    self.make_sample(stream, chan)
                out[i] = self.sample[stream][chan][byte] if tone else 0
                i += 1
                self.sample_byte[stream] = (pos + 1) % 6
            if i % 8 == 4:
                for stream in range(2):
                    if i >= length:
                        break
                    out[i] = (stream << 1) | ((~(i // 8)) & 1)
                    i += 1
        return bytes(out)


def decode_stream_zero(packets):
    # CAIAQ alignment=2: 8-byte group = 6 PCM bytes (3 per stream) + 2 check bytes.
    # Extract stream-0 bytes in chronological order, then group into 24-bit samples.
    pcm = bytearray()
    for packet in packets:
        for start in range(0, len(packet), 8):
            group = packet[start:start + 8]
            for offset in (0, 2, 6):
                if offset < len(group):
                    pcm.append(group[offset])
    samples = []
    for i in range(0, len(pcm) - 2, 3):
        v = int.from_bytes(pcm[i:i+3], 'big', signed=True)
        samples.append(v)
    return samples[0::2], samples[1::2]


def tone_frequency(samples, sample_rate):
    # Zero-crossing frequency estimate; ignores low-amplitude samples.
    crossings = [i for i in range(1, len(samples)) if samples[i-1] <= 0 < samples[i]]
    if len(crossings) < 2:
        return None
    return sample_rate * (len(crossings)-1) / (crossings[-1]-crossings[0])


def main():
    encoder = Encoder()
    packets = [encoder.fill() for _ in range(8000)]
    left, right = decode_stream_zero(packets)
    actual_frames = len(left)
    frame_rate = actual_frames  # 8000 packets represent one second of USB time
    print('OFFLINE TEST — modeled from kore1-bridge.c linux_fill_pcm')
    print('USB packets:', len(packets))
    print('Bytes per packet:', len(packets[0]))
    print('Stream-0 stereo frames over one second of USB:', actual_frames)
    print('Implied frames per second:', frame_rate)
    print('Expected 48-kHz frames per second:', TARGET_RATE)
    print('PCM frame count tracked by encoder:', encoder.frames)
    print('Decoded left-channel 440-Hz tone at nominal PCM clock:',
          f'{tone_frequency(left, TARGET_RATE):.2f} Hz')
    print('Decoded left-channel tone if 88k frames are consumed in one second:',
          f'{tone_frequency(left, frame_rate):.2f} Hz')
    print('Nonzero PCM samples:', sum(v != 0 for v in left), '/', len(left))
    print('First 24 USB bytes:', packets[0][:24].hex(' '))
    assert all(len(p) == 176 for p in packets)
    assert len(left) == 88000, (len(left), 'Unexpected frame count')
    assert len(right) == len(left)
    assert abs(tone_frequency(left, TARGET_RATE)-440) < 2
    assert frame_rate != TARGET_RATE
    print('PASS: confirmed fixed-176 packet pacing mismatch in modeled implementation')
    print('NOTE: This is a software-model result, not proof of device playback behavior.')

if __name__ == '__main__':
    main()
