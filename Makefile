CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -O2
LIBUSB_CFLAGS := $(shell pkg-config --cflags libusb-1.0 2>/dev/null)
LIBUSB_LIBS := $(shell pkg-config --libs libusb-1.0 2>/dev/null)
TARGET := build/kore-midi-v10

.PHONY: all clean check
all: $(TARGET) bridge

$(TARGET): src/kore-midi-v10.c
	@mkdir -p build
	$(CC) $(CFLAGS) $(LIBUSB_CFLAGS) -o $@ $< $(LIBUSB_LIBS) -framework CoreMIDI -framework CoreFoundation -lpthread

check: bridge
	python3 automation/generate_controller.py --check
	python3 automation/generate_bridge.py --check
	python3 tests/test_bridge_cli.py
	python3 tests/test_mixxx_xml.py
	node tests/test_mixxx.js
	python3 test_repository.py
	python3 tests/test_audio_patch.py
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

build/kore1-native-probe: audio/native_probe.m audio/controller_engine.h audio/native_plan.h audio/native_discovery.h audio/tone.h audio/caiaq.h coreaudio/pcm_source.h coreaudio/rate_match.h coreaudio/pcm_ring.h coreaudio/client.h
	@mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -O2 -fobjc-arc -fblocks -mmacosx-version-min=12.0 -o $@ $< -framework Foundation -framework CoreAudio -framework CoreMIDI -framework IOKit -framework IOUSBHost

.PHONY: native-check
build/test-native-plan: tests/test_native_plan.c audio/native_plan.h audio/caiaq.h
	@mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -o $@ $<

build/test-native-transport: tests/test_native_transport.m audio/native_probe.m audio/controller_engine.h audio/native_plan.h audio/native_discovery.h audio/tone.h audio/caiaq.h coreaudio/pcm_source.h coreaudio/rate_match.h coreaudio/pcm_ring.h coreaudio/client.h
	@mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -fobjc-arc -fblocks -mmacosx-version-min=12.0 -o $@ $< -framework Foundation -framework CoreAudio -framework CoreMIDI -framework IOKit -framework IOUSBHost

native-check: native-probe build/test-native-plan build/test-native-transport build/test-native-stream build/test-native-tone build/test-native-midi
	./build/test-native-plan
	./build/test-native-transport
	./build/kore1-native-probe --dry-run --layout observed
	python3 tests/test_native_cli.py
	./build/test-native-stream
	./build/test-native-tone
	./build/test-native-midi
	python3 automation/test_native_analyzer.py

build/test-native-stream: tests/test_native_stream.m audio/native_probe.m audio/controller_engine.h audio/native_plan.h audio/native_discovery.h audio/tone.h audio/caiaq.h coreaudio/pcm_source.h coreaudio/rate_match.h coreaudio/pcm_ring.h coreaudio/client.h
	@mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -fobjc-arc -fblocks -mmacosx-version-min=12.0 -o $@ $< -framework Foundation -framework CoreAudio -framework CoreMIDI -framework IOKit -framework IOUSBHost

build/test-native-tone: tests/test_native_tone.c audio/tone.h audio/caiaq.h coreaudio/pcm_source.h coreaudio/rate_match.h coreaudio/pcm_ring.h coreaudio/client.h
	@mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -o $@ $< -lm

.PHONY: hal-capture
hal-capture: build/kore-hal-capture
build/kore-hal-capture: coreaudio/capture.m
	@mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -O2 -fobjc-arc -o $@ $< -framework Foundation -framework CoreAudio

.PHONY: coreaudio-prototype
coreaudio-prototype: build/KoreAudio.driver/Contents/MacOS/KoreAudio
build/KoreAudio.driver/Contents/MacOS/KoreAudio: kore1-final-bridge.c coreaudio/Info.plist
	@mkdir -p build/KoreAudio.driver/Contents/MacOS
	$(CC) -std=c11 -Wall -Wextra -Werror -O2 -fPIC -bundle -DKORE_HAL_PLUGIN -o $@ kore1-final-bridge.c -framework CoreAudio -framework CoreFoundation
	cp coreaudio/Info.plist build/KoreAudio.driver/Contents/Info.plist
	codesign --force --sign - build/KoreAudio.driver

.PHONY: coreaudio-check
build/test-hal-capture: tests/test_hal_capture.m coreaudio/capture.m
	@mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -fobjc-arc -o $@ $< -framework Foundation -framework CoreAudio
build/test-coreaudio-plugin: tests/test_coreaudio_plugin.c coreaudio/plugin.c coreaudio/pcm_ring.h coreaudio/pcm_source.h coreaudio/rate_match.h
	@mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -o $@ $< -framework CoreAudio -framework CoreFoundation -lpthread
coreaudio-check: build/test-stereo-pcm build/test-stereo-signal build/test-rate-match hal-capture coreaudio-prototype build/kore-coreaudio-render build/test-hal-capture build/test-coreaudio-plugin build/test-coreaudio-bundle
	./build/test-stereo-pcm
	./build/test-stereo-signal
	./build/test-rate-match
	./build/test-hal-capture
	./build/test-coreaudio-plugin
	./build/test-coreaudio-bundle
	python3 automation/test_native_quality.py
	python3 automation/test_stereo_analyzer.py

build/kore-coreaudio-render: coreaudio/render_test.m coreaudio/test_signal.h coreaudio/client.h coreaudio/pcm_ring.h
	@mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -O2 -fobjc-arc -o $@ $< -framework Foundation -framework CoreAudio -framework CoreMIDI

build/test-coreaudio-bundle: tests/test_coreaudio_bundle.m
	@mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -O2 -fobjc-arc -o $@ $< -framework Foundation -framework CoreAudio

build/test-rate-match: tests/test_rate_match.c coreaudio/rate_match.h coreaudio/pcm_ring.h
	@mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -O2 -g -fsanitize=address,undefined -o $@ $< -lm

# Reproducible standalone C bridge and complete local development package.
.PHONY: bridge development-package
bridge: build/kore1-final-bridge
kore1-final-bridge.c: $(wildcard audio/*.h audio/*.m coreaudio/*.h) coreaudio/plugin.c automation/generate_bridge.py
	python3 automation/generate_bridge.py
build/kore1-final-bridge: kore1-final-bridge.c
	@mkdir -p build
	$(CC) $(CFLAGS) -Werror -x objective-c -fobjc-arc -fblocks -mmacosx-version-min=12.0 -o $@ $< -framework Foundation -framework CoreAudio -framework CoreMIDI -framework IOKit -framework IOUSBHost

development-package: bridge all native-probe hal-capture coreaudio-prototype build/kore-coreaudio-render build/kore-coreaudio-system-test
	python3 automation/package_development.py

audio/controller_engine.h: src/kore-midi-v10.c automation/generate_controller.py
	python3 automation/generate_controller.py

audio/system_resources.h: coreaudio/Info.plist automation/system_driver.sh automation/generate_system_header.py
	python3 automation/generate_system_header.py

build/test-native-midi: tests/test_native_midi.m audio/native_probe.m audio/controller_engine.h
	@mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -fobjc-arc -fblocks -mmacosx-version-min=12.0 -o $@ $< -framework Foundation -framework CoreAudio -framework CoreMIDI -framework IOKit -framework IOUSBHost

build/kore-coreaudio-system-test: coreaudio/system_test.m coreaudio/client.h coreaudio/pcm_ring.h
	@mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -O2 -fobjc-arc -o $@ $< -framework Foundation -framework CoreAudio

build/test-stereo-signal: tests/test_stereo_signal.c coreaudio/test_signal.h
	@mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -o $@ $< -lm

build/test-stereo-pcm: tests/test_stereo_pcm.c coreaudio/pcm_source.h coreaudio/rate_match.h coreaudio/pcm_ring.h audio/caiaq.h
	@mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -o $@ $< -lm
