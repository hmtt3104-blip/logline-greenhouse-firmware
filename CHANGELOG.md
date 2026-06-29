# Changelog

## Unreleased

- Created sanitized clean export structure.
- Added sanitized Waveshare ESP32-S3 dual-zone firmware sketch.
- Moved network credentials to ignored local `firmware/config.h`.
- Added setup, safety, architecture, hardware, and reference documentation.
- Added `experiments/waveshare-s3-dualzone-port.md` and updated it with verified local Arduino compile evidence.
- Added a Serial Monitor bare-board bench checklist with baud rate, expected startup diagnostics, no-load safety rules, safe/blocked commands, and expected no-DHT behavior.
- Documented the startup-close safety finding: close relays may energize for about `10000 ms` on boot, so the first board test must use USB/bare-board/no-load only.
- Marked public readiness as `NEEDS_CLEANUP` until hardware validation, relay behavior, board revision, analog sensor assumptions, and completed bench experiment records are validated.
- Renamed the Arduino sketch entrypoint to `firmware/firmware.ino` and documented the local `esp32:esp32:esp32s3` Arduino CLI compile check.
