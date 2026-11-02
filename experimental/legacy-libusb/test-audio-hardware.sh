#!/bin/sh
set -eu
cd "$(dirname "$0")"
sh build.sh
mkdir -p logs
stamp=$(date +%Y%m%d-%H%M%S)
log="logs/audio-$stamp.log"
printf 'Hardware diagnostic. Disconnect headphones/speakers or set volume to minimum.\n'
printf 'Stop after ~5 seconds using Ctrl+C. Log: %s\n' "$log"
./kore1-bridge --audio-probe --audio-duplex-silence --audio-linux-urbs --audio-hex-probe --audio-report-ms 1000 2>&1 | tee "$log"
