#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
python3 -m compileall -q automation
make all audio native-probe hal-capture coreaudio-prototype
make check audio-check native-check coreaudio-check
printf '%s\n' 'Builds and offline checks complete. No USB/audio hardware was opened.'
