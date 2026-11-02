# How to merge this documentation with the latest local bridge

GitHub's public `kore1-bridge.c` is older than the source excerpts used in the 2026-11-02 debugging session. The current local file is the authoritative source for the latest experiments. The documentation update intentionally does **not** replace it with a historical ZIP snapshot.

From the Mac with the existing project:

```sh
cd ~/Documents/NI-KORE-macOS-driver
git status --short
git switch -c research/audio-handoff-2026-11
# Review before staging; avoid committing secrets, large captures, or compiled binaries.
git add kore1-bridge.c
git commit -m 'Preserve local CAIAQ audio diagnostic source'
```

Then copy the provided `README.md`, `docs/`, and `experimental/` into the repository (preserve existing `build.sh`, `Makefile`, `src/`, `mixxx/`, tests, and any working source). Run `./build.sh` and `make check` on the Mac, inspect `git diff`, commit, and push the branch. Open a pull request to `main` after review.

**Historical snapshot:** `experimental/KORE-Audio-v0.23-Framing-Audit.zip` contains an earlier standalone source plus framing tests. It is for reference only; do not overwrite `kore1-bridge.c` with it.

**Access note:** Automated GitHub write attempt on 2026-11-02 failed with `403 Resource not accessible by integration`; no GitHub branch or commit was created by that attempt.
