# Historical libusb audio experiments

These snapshots preserve earlier evidence, not the current audio implementation.
Use the native target and documentation linked from the repository root README.
Historical README commands refer to their original workspace and may be unbounded,
use obsolete framing assumptions, enable tones or report superseded conclusions.
Do not run hardware scripts from this archive as a normal smoke test.

The grouped sources, diagnostics and historical notes were moved here without
content changes. `MANIFEST.json` records original paths and SHA256 hashes. The
root `kore1-bridge.c` remains a compatibility bridge with the user's pre-existing
local changes preserved in the cleanup commit. Git history retains older versions.
The historical ZIP in the parent directory is also retained.

Recordings, logs, binaries and backup copies are preserved locally under ignored
`artifacts/` and `build/legacy/`. They are not source-distribution dependencies.

Selected offline historical audits (run from this directory):

```sh
python3 test_rxmatched.py
python3 test-kore-pcm-offline.py
python3 test-kore-c-payload.py
```

The actively maintained tests are `make check audio-check native-check` from the
repository root. An archived source assertion is not proof of native playback.
