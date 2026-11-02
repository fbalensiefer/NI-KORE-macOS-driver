# KORE 1 RX-length matched silence experiment

This patch is based on the user-supplied `kore1-bridge-single176.c` and **does not overwrite it**.

## What changed

- New opt-in `--audio-rx-matched-zero`: for every completed 8-packet IN URB, copy each successful IN packet's `actual_length` to its corresponding OUT ISO descriptor, including zeros. Non-completed IN descriptors become zero length.
- Do not submit all-zero OUT batches; submit mixed batches with zero-length descriptors and a compacted payload buffer.
- Continue to send silence with the existing alignment-2 check bytes for nonzero packets; no PCM test tones.
- Report counts of zero-length descriptors, mixed batches, and all-zero batches.
- Reject combinations with tone, fixed-length, FIFO, independent OUT, priming, or echo options.

**Important limitation:** macOS libusb/IOKit support for *mixed zero-length isochronous OUT packet descriptors* has NOT been verified. A submit error or stalled OUT does not establish a device firmware fault. Do not assume this reproduces Linux kernel USB timing. No macOS compile or hardware test was possible in this environment.

## Offline audit

```sh
python3 test_rxmatched.py
```

## Build on macOS

```sh
cc -std=c11 -Wall -Wextra -O2 -o kore1-bridge-rxmatched \
  kore1-bridge-rxmatched.c \
  $(pkg-config --cflags --libs libusb-1.0) \
  -framework CoreMIDI -framework CoreFoundation -lm -lpthread
```

## Controlled hardware silence-only test

Disconnect headphones and speakers; turn output gain to minimum. Run for 3–5 seconds and Ctrl+C:

```sh
./kore1-bridge-rxmatched --audio-rx-matched-zero --audio-linux-urbs --audio-linux-trace
```

Record the full terminal output. Look for `AUDIO RX-MATCH` counters and any `AUDIO OUT submit` error. If macOS rejects zero-length ISO descriptors, **stop** rather than trying a tone. The original `kore1-bridge-single176.c` is unchanged.

## Remaining uncertainties

The USB transfer submission pattern, exact PCM/checkbyte framing, DAC routing, and reliable playback are not established. The test is for scheduler behavior only.
