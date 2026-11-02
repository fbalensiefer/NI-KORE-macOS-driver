KORE 1 96-byte payload hardware audit

This is a modified copy of your uploaded C source. Original untouched.

Build (macOS, Homebrew libusb):
  cc -std=c11 -Wall -Wextra -O2 -o kore1-bridge-audit kore1-bridge-96-audit.c $(pkg-config --cflags --libs libusb-1.0) -framework CoreMIDI -framework CoreFoundation -lm -lpthread

Run with KORE attached, but with speakers/headphones disconnected:
  ./kore1-bridge-audit --headphone-tone --audio-out-96
  Wait 4 seconds and Ctrl+C.

Look for AUDIO PAYLOAD SILENCE and AUDIO PAYLOAD TONE, with nonzero_pcm_bytes > 0 in the tone case.
The existing test tone is -60 dBFS (amplitude 8192/8388607), intentionally lower than -30 dBFS. It is active only from 1.0 to 2.2 seconds after ISO OUT startup.

Important: This is NOT a confirmation of audio playback. No audio output hardware should be connected during this first diagnostic.
