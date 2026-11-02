#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p artifacts
echo "== Static repository tests =="
python3 -m compileall -q automation
if [[ -f tests/test_repository.py ]]; then python3 tests/test_repository.py; fi
if [[ -f Makefile ]] && command -v pkg-config >/dev/null; then
  echo "== Existing MIDI bridge build =="
  make
fi
echo "== Experimental CAIAQ build (if installed) =="
if [[ -f experimental/caiaq-audio-v1/build.sh ]]; then
  (cd experimental/caiaq-audio-v1 && bash ./build.sh)
else
  echo "No experimental/caiaq-audio-v1 source in checkout; skipped."
fi
echo "Static checks complete. No USB/audio output was started."
