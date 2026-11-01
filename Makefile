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
	python3 tests/test_repository.py
	@if command -v node >/dev/null 2>&1; then node --check mixxx/KORE-1-Mixxx-v10.js; else echo 'Node.js not found; JS syntax check skipped'; fi

clean:
	rm -rf build
