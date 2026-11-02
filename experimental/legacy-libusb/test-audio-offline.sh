#!/bin/sh
set -eu
cd "$(dirname "$0")"
python3 tests/test_audio_patch.py
sh build.sh
printf '\nOffline tests and build completed. No hardware behavior verified.\n'
