CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -O2
LIBUSB_CFLAGS := $(shell pkg-config --cflags libusb-1.0 2>/dev/null)
LIBUSB_LIBS := $(shell pkg-config --libs libusb-1.0 2>/dev/null)
TARGET := build/kore-midi-v10

.PHONY: all clean check
all: $(TARGET)

$(TARGET): src/kore-midi-v10.c
	@mkdir -p build
	$(CC) $(CFLAGS) $(LIBUSB_CFLAGS) -o $@ $< $(LIBUSB_LIBS) -framework CoreMIDI -framework CoreFoundation -lpthread

check:
	python3 test_repository.py
	python3 test_patch.py
	python3 automation/test_analyzer.py
	@if command -v node >/dev/null 2>&1; then node --check KORE-1-Mixxx-v10.js; else echo "SKIP: Node unavailable"; fi

clean:
	rm -rf build

.PHONY: audio audio-check
audio: build/kore1-bridge-audio

build/kore1-bridge-audio: kore1-bridge-audio.c audio/caiaq.h
	@mkdir -p build
	$(CC) $(CFLAGS) -Werror $(LIBUSB_CFLAGS) -o $@ $< $(LIBUSB_LIBS) -framework CoreMIDI -framework CoreFoundation -lpthread -lm

build/test-caiaq: tests/test_caiaq.c kore1-bridge-audio.c audio/caiaq.h
	@mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined $(LIBUSB_CFLAGS) -o $@ $< $(LIBUSB_LIBS) -framework CoreMIDI -framework CoreFoundation -lpthread -lm

audio-check: audio build/test-caiaq
	./build/test-caiaq
	python3 tests/test_audio_cli.py
