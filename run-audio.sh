#!/bin/sh
set -eu
cd "$(dirname "$0")"
[ -p /tmp/kore1-pcm.fifo ] || mkfifo /tmp/kore1-pcm.fifo
printf '%s\n' 'EXPERIMENTAL AUDIO: start this bridge, then in a second terminal run ./capture-blackhole.'
exec ./kore1-bridge --mixxx-feedback --pcm-fifo --audio-report-ms 1000
