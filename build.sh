#!/bin/sh
set -eu
command -v pkg-config >/dev/null || { echo "Install pkg-config: brew install pkg-config"; exit 1; }
pkg-config --exists libusb-1.0 || { echo "Install libusb: brew install libusb"; exit 1; }
cc -std=c11 -Wall -Wextra -O2 -o kore1-bridge kore1-bridge.c $(pkg-config --cflags --libs libusb-1.0) -framework CoreMIDI -framework CoreFoundation -lpthread -lm

cc -std=c11 -Wall -Wextra -O2 -o capture-blackhole capture-blackhole.c -framework AudioToolbox -framework CoreAudio -framework CoreFoundation
