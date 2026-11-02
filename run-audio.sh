#!/bin/sh
set -eu
cd "$(dirname "$0")"
printf '%s\n' 'Native bounded silence test: stop other KORE processes first; disconnect speakers/headphones and minimize gain.'
exec ./build/kore1-native-probe --run --stream-silence --duration-ms 3000 "$@"
