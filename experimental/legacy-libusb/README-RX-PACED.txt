KORE 1: RX-paced 96-byte OUT experiment
=======================================
Source: kore1-bridge-rx-paced.c

IMPORTANT: This is NOT exact Linux snd-usb-caiaq parity. Linux matches OUT
packet lengths to incoming IN packet lengths. Since this KORE often returns
0 or 176 bytes on IN, matching alone cannot prove playback. This mode keeps
96-byte OUT packets but triggers one OUT batch per completed IN batch, rather
than maintaining an independent OUT rearm loop.

Build (macOS with Homebrew libusb):
cc -std=c11 -Wall -Wextra -O2 -o kore1-bridge-rx-paced \
  kore1-bridge-rx-paced.c $(pkg-config --cflags --libs libusb-1.0) \
  -framework CoreMIDI -framework CoreFoundation -lm -lpthread

Run with KORE headphone out connected to MAYA44 line input, volume low:
./kore1-bridge-rx-paced --audio-rx-paced-96

Capture MAYA44 with FFmpeg in another terminal first:
ffmpeg -hide_banner -f avfoundation -i ':1' -t 12 -ac 2 -ar 48000 \
  -c:a pcm_f32le -y kore-rx-paced-test.wav

Note: Test tone is active from 1.0s to 2.2s after ISO startup.
Stop bridge with Ctrl+C. Compare the WAV with previous capture.

Diagnostic interpretation:
- A 440Hz peak would support analog playback.
- No peak does not identify whether routing, packet sizes, or DAC enable is wrong.
- OUT completion means USB transfer completion, not audible audio.

The original file is unchanged. The new CLI flag is opt-in.
No device hardware test or macOS compile has been performed here.
