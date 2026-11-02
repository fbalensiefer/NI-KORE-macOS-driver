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

.PHONY: native-probe
native-probe: build/kore1-native-probe

build/kore1-native-probe: audio/native_probe.m audio/native_plan.h audio/native_discovery.h audio/tone.h audio/caiaq.h
	@mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -O2 -fobjc-arc -fblocks -mmacosx-version-min=12.0 -o $@ $< -framework Foundation -framework IOKit -framework IOUSBHost

.PHONY: native-check
build/test-native-plan: tests/test_native_plan.c audio/native_plan.h audio/caiaq.h
	@mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -o $@ $<

build/test-native-transport: tests/test_native_transport.m audio/native_probe.m audio/native_plan.h audio/native_discovery.h audio/tone.h audio/caiaq.h
	@mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -fobjc-arc -fblocks -mmacosx-version-min=12.0 -o $@ $< -framework Foundation -framework IOKit -framework IOUSBHost

native-check: native-probe build/test-native-plan build/test-native-transport build/test-native-stream build/test-native-tone
	./build/test-native-plan
	./build/test-native-transport
	./build/kore1-native-probe --dry-run --layout observed
	python3 tests/test_native_cli.py
	./build/test-native-stream
	./build/test-native-tone
	python3 automation/test_native_analyzer.py

build/test-native-stream: tests/test_native_stream.m audio/native_probe.m audio/native_plan.h audio/native_discovery.h audio/tone.h audio/caiaq.h
	@mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -fobjc-arc -fblocks -mmacosx-version-min=12.0 -o $@ $< -framework Foundation -framework IOKit -framework IOUSBHost

build/test-native-tone: tests/test_native_tone.c audio/tone.h audio/caiaq.h
	@mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -o $@ $< -lm
