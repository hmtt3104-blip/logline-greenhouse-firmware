# logline-greenhouse-firmware

## What this is

Sanitized public export of a Waveshare ESP32-S3 Relay-6CH dual-zone greenhouse firmware experiment.

Status: Prototype / NEEDS_CLEANUP / Not production-ready.

This repository contains firmware and documentation for studying a two-zone greenhouse vent controller. It is not a live production dump and does not include private configuration, credentials, live topology, logs, backups, binaries, or deployment notes.

## Foundation

This repository follows the public standards and operating model defined in:

https://github.com/hmtt3104-blip/logline-foundation

Logline Foundation defines how public experiments are documented, reviewed, sanitized, and linked across repositories.

## Problem

Greenhouse ventilation needs local control that can continue operating when network integration is unavailable or not yet trusted.

## Hypothesis

A Waveshare ESP32-S3 Relay-6CH board can run a local dual-zone vent controller with DHT-based temperature/humidity input, relay-based motor control, fallback access point setup, and a small embedded web interface.

## Experiment

Port the active dual-zone controller sketch into a sanitized firmware repository, separate all credentials into ignored local configuration, document the relay/pin assumptions, and keep the source export reviewable without live deployment details.

What is intentionally not tested in this export:

- live greenhouse operation;
- production relay safety;
- confirmed analog sensor wiring;
- long-duration unattended operation;
- recovery from every possible sensor or motor fault.

## Architecture

- `firmware/` contains the sanitized Arduino sketch and example local configuration.
- `hardware/` documents the generic Waveshare ESP32-S3 Relay-6CH pin map used by this experiment.
- `docs/` contains architecture, setup, and safety notes.
- `experiments/` is reserved for experiment records after fresh hardware validation.
- `reference/` explains the sanitization boundary.
- `data/` is reserved for sanitized reference data only.
- `examples/` is reserved for sanitized examples only.

## Current status

Repository status: Prototype.

Public readiness: NEEDS_CLEANUP.

Trust level: Medium for documentation shape, low for hardware validation in this clean export.

Production readiness: Not production-ready.

Reason for public readiness status:

- The export is sanitized, but fresh hardware validation after sanitization is still missing.
- Relay behavior, board revision, and analog sensor pins still need documented hardware checks.
- Build reproducibility has a first local Arduino CLI check, but it does not validate hardware behavior.
- No completed experiment record has been added for the sanitized firmware export yet.

## Results / Lessons

- The active firmware can be reduced to a public-safe source snapshot.
- Credentials and deployment details must stay outside the repository.
- Static private IP assumptions are not appropriate for a public firmware export.
- Hardware pin mapping needs explicit validation notes because analog sensor pins are not confirmed in this port.

## What failed

- The source firmware contained hardcoded Wi-Fi/AP values and private network assumptions.
- Serial help previously exposed credential values.
- The firmware still needs fresh validation after sanitization.

## Known limitations

- This export does not prove safe operation on live vents or motors.
- The reference board revision is not fully confirmed in this public record.
- Analog sensor behavior is not verified in the clean export.
- Build reproducibility is documented as an Arduino CLI check, but hardware validation is still pending.
- No completed experiment record exists for the sanitized firmware export yet.
- No release-ready claim is made.

## Next questions

- Which exact Waveshare ESP32-S3 Relay-6CH revision is the reference board?
- Should network setup remain compile-time only, or move to runtime provisioning?
- Which relay wiring and failsafe behavior should be documented as the public reference?
- Should analog sensors stay disabled until verified on hardware?

## Safety / Security notes

- Real `firmware/config.h` is ignored by git.
- Do not commit real Wi-Fi SSIDs, Wi-Fi passwords, AP passwords, private IPs, live hostnames, logs, binaries, or deployment notes.
- This export intentionally excludes live greenhouse topology.
- Do not treat this firmware as production-ready without hardware validation and safety review.

## Repository map

```text
logline-greenhouse-firmware/
├── firmware/
├── hardware/
├── docs/
├── experiments/
├── examples/
├── reference/
├── data/
├── README.md
├── SECURITY.md
├── ROADMAP.md
├── CHANGELOG.md
├── CONTRIBUTING.md
└── .gitignore
```

## How to run / reproduce

1. Review `docs/safety.md`.
2. Copy `firmware/config.example.h` to `firmware/config.h`.
3. Replace placeholder values in `firmware/config.h` with local test credentials.
4. Open `firmware/firmware.ino` in the Arduino toolchain.
5. Select board label `ESP32S3 Dev Module`.
6. Use FQBN `esp32:esp32:esp32s3`.
7. Optional bundled Arduino CLI compile check:

```powershell
& "C:\Users\V\AppData\Local\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" compile --fqbn esp32:esp32:esp32s3 --build-path "$env:TEMP\logline-greenhouse-firmware-build" firmware
```

8. Flash only on bench hardware or a reviewed non-production setup.

## Related repositories

- https://github.com/hmtt3104-blip/logline-foundation
- https://github.com/hmtt3104-blip/logline-greenhouse-ai
- https://github.com/hmtt3104-blip/logline-greenhouse-edge
