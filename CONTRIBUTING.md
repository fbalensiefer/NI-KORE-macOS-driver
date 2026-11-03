# Contributing

Start with [current status](docs/STATUS.md), [architecture](ARCHITECTURE.md) and
[tests](TESTING.md). Report KORE model, macOS/Mixxx versions, reproduction steps,
actual agent/manual arguments and whether a result is offline or physical.
Remove private library paths, machine identifiers and unrelated logs from reports.

Edit maintained modular sources, then run `automation/generate_controller.py`
and/or `automation/generate_bridge.py` when their inputs change. Keep the generated
standalone file reproducible; do not hand-edit generated output. Preserve the
existing ERP filtering unless a regression justifies changing it. Root legacy
mappings and archived experiment hashes are historical compatibility evidence.

Run checks appropriate to the change. `make check` covers repository/mapping and
CLI consistency; native/audio/HAL changes require their sanitizer/ABI targets.
Optional echo checks require NumPy. Documentation-only changes need link/status
consistency and package review, not fresh hardware output. Builds never install.

USB-affecting work needs explicit opt-in bounded commands, packet expectations
and isolated ownership. Confirm the current hardware setup before playback;
never run untrusted PR code automatically on attached hardware. Record invalid
captures and failures, not only passes. Software tests do not establish sustained
clock/audio quality or every physical LED position.

Keep current guides separate from dated milestone reports. Update STATUS,
NEXT-STEPS and the mapping/setup guides when behavior or deployment changes.
Retain licensing/attribution and the existing review requirement in NOTICE.md;
never include proprietary firmware, personal recordings or compiled build outputs
in Git. The local package manifest records source revision and file hashes.
