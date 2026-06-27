# Roadmap

## Current

- Keep the export sanitized.
- Keep public readiness at `NEEDS_CLEANUP` until validation evidence exists.
- Validate that the firmware builds with local `firmware/config.h`.
- Re-check all relay pins against the physical Waveshare ESP32-S3 Relay-6CH board.
- Confirm the access point setup flow on bench hardware.
- Confirm that no private credentials, private IPs, live topology, logs, binaries, or deployment notes are present.

## Next

- Add a documented hardware test checklist.
- Add a repeatable firmware build note.
- Add experiment records for relay behavior, DHT reliability, Wi-Fi recovery, and sanitized build reproducibility.
- Decide whether runtime provisioning should replace compile-time network configuration.
- Define the evidence required before public readiness can become `READY`.

## Later

- Add sanitized examples.
- Add diagrams that do not expose live topology.
- Document compatibility boundaries for board revisions and relay wiring.
