#!/bin/sh
set -eu
cd "$(dirname "$0")"
exec make check audio-check native-check coreaudio-check
