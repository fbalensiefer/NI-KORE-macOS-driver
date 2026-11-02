# KORE 1 – RX-paced fixed-176 four-channel sweep

Experimental diagnostic based on uploaded `kore1-bridge-96-audit(2).c`. Does not claim full Linux parity or functioning DAC output.

## Build on macOS

```bash
cc -std=c11 -Wall -Wextra -O2 -o kore1-bridge-sweep176 kore1-bridge-sweep176.c $(pkg-config --cflags --libs libusb-1.0) -framework CoreMIDI -framework CoreFoundation -lm -lpthread
```

## Run

Keep KORE headphone output connected to MAYA44 line input. Set output and input levels low; do not wear headphones during diagnostics. Start MAYA44 recording first:

```bash
ffmpeg -hide_banner -f avfoundation -i ':1' -t 12 -ac 2 -ar 48000 -c:a pcm_f32le -y kore-sweep176.wav
```

Immediately run in a second terminal:

```bash
./kore1-bridge-sweep176 --audio-sweep-176
```

Let run for 8 seconds then Ctrl+C. Upload the WAV and log.

Timeline relative to ISO start: 0–1 s silence; 1–2.2 s channel 1 (stream 0 left); 2.2–3.4 s channel 2 (stream 0 right); 3.4–4.6 s channel 3 (stream 1 left); 4.6–5.8 s channel 4 (stream 1 right); then silence. Tone ~440 Hz, low amplitude.

**Limitations:** RX-completion paced but fixed 176 bytes for every OUT microframe, *not* RX packet-length matching. 0-byte IN packets are not mirrored. PCM packing and DAC routing are still hypotheses. Linux parity and CoreAudio integration remain future work.
