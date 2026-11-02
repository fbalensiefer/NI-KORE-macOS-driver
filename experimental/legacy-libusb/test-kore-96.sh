#!/bin/sh
set -eu
cd "$(dirname "$0")"
if [ "$(uname -s)" != Darwin ]; then echo 'Hardware test requires macOS' >&2; exit 1; fi
if [ ! -f kore1-bridge-96.c ]; then echo 'Missing kore1-bridge-96.c' >&2; exit 1; fi
if ! command -v pkg-config >/dev/null || ! pkg-config --exists libusb-1.0; then echo 'Install libusb and pkg-config using Homebrew' >&2; exit 1; fi
cc -std=c11 -Wall -Wextra -O2 -o kore1-bridge-96 kore1-bridge-96.c $(pkg-config --cflags --libs libusb-1.0) -framework CoreMIDI -framework CoreFoundation -lpthread -lm
if [ -e /tmp/kore1-pcm.fifo ] && [ ! -p /tmp/kore1-pcm.fifo ]; then echo '/tmp/kore1-pcm.fifo exists but is not a FIFO; stopping' >&2; exit 1; fi
if [ ! -e /tmp/kore1-pcm.fifo ]; then mkfifo /tmp/kore1-pcm.fifo; fi
printf '%s\n' 'No speakers/headphones. No PCM writer: silence/underflow test.' 'Press Ctrl+C after 5 seconds.'
exec ./kore1-bridge-96 --pcm-fifo --audio-out-96 --audio-report-ms 1000 --audio-hex-probe
