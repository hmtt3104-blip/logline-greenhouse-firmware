# Changelog

## Unreleased

- Created sanitized clean export structure.
- Added sanitized Waveshare ESP32-S3 dual-zone firmware sketch.
- Moved network credentials to ignored local `firmware/config.h`.
- Added setup, safety, architecture, hardware, and reference documentation.
- Added `experiments/waveshare-s3-dualzone-port.md` and updated it with verified local Arduino compile evidence.
- Added a Serial Monitor bare-board bench checklist with baud rate, expected startup diagnostics, no-load safety rules, safe/blocked commands, and expected no-DHT behavior.
- Disabled startup close by default for the public export so first boot does not automatically queue close relay motion.
- Documented that if startup close is re-enabled in a private deployment, close relays may energize on boot for `startupCloseMs`.
- Marked public readiness as `NEEDS_CLEANUP` until hardware validation, relay behavior, board revision, analog sensor assumptions, and completed bench experiment records are validated.
- Renamed the Arduino sketch entrypoint to `firmware/firmware.ino` and documented the local `esp32:esp32:esp32s3` Arduino CLI compile check.
