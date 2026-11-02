#!/bin/bash
set -euo pipefail
# macOS AVFoundation input indices vary; select explicitly from ffmpeg -list_devices output.
DEVICE="${KORE_AUDIO_INPUT_INDEX:-}"
if [[ -z "$DEVICE" ]]; then
  echo "Set KORE_AUDIO_INPUT_INDEX to the AVFoundation AUDIO device index." >&2
  echo "Discover: ffmpeg -f avfoundation -list_devices true -i ''" >&2
  exit 2
fi
if ! [[ "$DEVICE" =~ ^[0-9]+$ ]]; then echo "Invalid input index" >&2; exit 2; fi
OUT="${1:-loopback.wav}"
DURATION="${KORE_CAPTURE_SECONDS:-5}"
if ! [[ "$DURATION" =~ ^[0-9]+$ ]] || (( DURATION < 2 || DURATION > 20 )); then
  echo "Capture duration must be 2..20 seconds" >&2; exit 2
fi
mkdir -p "$(dirname "$OUT")"
# audio index syntax: :<audio index> ; no video capture
exec ffmpeg -nostdin -hide_banner -loglevel warning -y \
  -f avfoundation -i ":${DEVICE}" -t "$DURATION" \
  -ar 48000 -ac 2 -c:a pcm_s16le "$OUT"
