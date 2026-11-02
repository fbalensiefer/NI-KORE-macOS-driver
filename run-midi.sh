#!/bin/sh
set -eu
cd "$(dirname "$0")"
exec ./build/kore1-final-bridge --run --midi-only --continuous --mixxx-feedback "$@"
