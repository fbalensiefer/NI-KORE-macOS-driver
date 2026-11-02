#!/bin/sh
set -eu
cd "$(dirname "$0")"
exec ./build/kore1-final-bridge --mixxx-feedback "$@"
