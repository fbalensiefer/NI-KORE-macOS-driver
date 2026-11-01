# Contributing

Issues and pull requests are welcome. Describe your KORE model, macOS version, Mixxx version, reproduction steps, and the exact CLI arguments used. Do not include private track libraries or machine identifiers in logs.

For changes affecting USB output, add an opt-in test command, explain expected packets, and demonstrate an isolated hardware test. Preserve the functioning v6 ERP filter unless a regression is evidenced. Do not copy proprietary Native Instruments firmware or copyrighted assets into this repository.

Run `make check` before contributing. Native macOS build and hardware verification are separately required.
