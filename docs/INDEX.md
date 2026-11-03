# Documentation index — 2026-11-03

Use [STATUS](STATUS.md) for current capabilities, decisions, deployment and limits.
Dated evidence is preserved even where later work superseded an early default.

## Current guides

| Document | Purpose |
|---|---|
| [README](../README.md) | Short project overview and entry points |
| [STATUS](STATUS.md) | Consolidated facts, evidence levels and current deployment |
| [Setup](../README-SETUP.md) | Fresh installation, user-local updates, Mixxx setup and removal |
| [Package details](DEVELOPMENT-PACKAGE.md) | Standalone compilation, installers and test tools |
| [Mixxx FX/browser/feedback](MIXXX-FX-MAPPING.md) | Active controls, arrow loading, LEDs, LCD fallback and installation history |
| [Architecture](../ARCHITECTURE.md) | Shared USB owner, four-channel HAL/PCM flow and compatibility boundaries |
| [Protocol](../PROTOCOL.md) | USB reports, MIDI notes/CCs, LEDs/LCD and custom PCM properties |
| [Transport reference](PROTOCOL.md) | Audio handshake, packet framing and research conventions |
| [Testing](../TESTING.md) | Offline targets, opt-in hardware methods and acceptance limits |
| [Next steps](NEXT-STEPS.md) | Unfinished engineering and explicitly deferred changes |
| [Contributing](../CONTRIBUTING.md) | Source generation, evidence and review conventions |
| [Changelog](../CHANGELOG.md) | Chronological implementation history, including superseded milestones |
| [Licensing/provenance](../NOTICE.md) | Existing third-party notices and review requirement |

## Dated evidence and investigations

| Document | Scope |
|---|---|
| [Stereo output](STEREO-OUTPUT-RESULTS.md) | Evolving gains, four-channel Main path and reboot follow-up |
| [Headphone diagnosis](HEADPHONE-DIAGNOSTICS.md) | Valid/rejected captures, chirps, clipping and unconfirmed TRRS adapter hypothesis |
| [Initial native integration](NATIVE-INTEGRATION-VALIDATION.md) | Earlier stereo HAL/shared MIDI milestone, not today's complete configuration |
| [Native transport](NATIVE-TRANSPORT.md) | Isolated native discovery/transaction work and bounded diagnostics |
| [Native streaming](NATIVE-STREAM-RESULTS.md) | Early streaming pools, cancellation and failure results |
| [Tone playback](TONE-PLAYBACK-RESULTS.md) | Original capped oscillator/channel measurements |
| [Capture continuity](CAPTURE-CONTINUITY.md) | Direct HAL recorder, old AVFoundation gaps and relative clock evidence |
| [Rate matching](RATE-MATCHING.md) | Initial ASRC design/simulation/short-run evidence |
| [Local HAL prototype](COREAUDIO-PROTOTYPE.md) | Historical uninstalled/stereo milestone |
| [Linux/libusb validation](AUDIO-VALIDATION.md) | Legacy failures and Linux reference comparisons |
| [Early findings](AUDIO-FINDINGS.md) | Historical hypotheses before native playback worked |

The JSON measurement files beside these reports are immutable evidence snapshots,
not newly measured production specifications. Current claims must include invalid
runs and later failures, not only selected passes.

## Historical handoffs and immutable archive

- [Old runner setup](history/AUTOMATED-RUNNER-SETUP.md): optional historical CI instructions.
- [Repository update handoff](../REPOSITORY_UPDATE.md) and
  [repository sync handoff](REPOSITORY-SYNC.md): preserved earlier instructions.
- [Legacy source archive](../experimental/legacy-libusb/README.md): files verified
  against its hash manifest; not edited to modernize obsolete hardware commands.

## Development archive layout

The local zip contains the runtime binaries/HAL bundle, active mapping, standalone
C source, root guides, this complete docs tree and the preserved legacy archive.
The docs retain repository-relative paths instead of flattened copies. `make`
commands refer to a source checkout; use the standalone compilation section of
the package guide when only the generated file is available. Runtime commands
shown as `./build/tool` become `./tool` inside the extracted archive.
