# Roadmap

## Current

- Keep the export sanitized.
- Keep public readiness at `NEEDS_CLEANUP` until validation evidence exists.
- Public-readiness checklist result is recorded in `docs/safety.md` as `NEEDS_CLEANUP`.
- Validate that the firmware builds with local `firmware/config.h`.
- Re-check all relay pins against the physical Waveshare ESP32-S3 Relay-6CH board.
- Confirm the access point setup flow on bench hardware.
- Confirm that no private credentials, private IPs, live topology, logs, binaries, or deployment notes are present.

## Next

- Add a documented hardware test checklist.
- Add a repeatable firmware build note.
- Add experiment records for relay behavior, DHT reliability, Wi-Fi recovery, and sanitized build reproducibility.
- Decide whether runtime provisioning should replace compile-time network configuration.

## Evidence required before `READY`

Public readiness should remain `NEEDS_CLEANUP` until:

- firmware build reproducibility is documented;
- relay pins and active states are verified against the physical board;
- AP setup and local web UI behavior are verified on bench hardware;
- analog sensor wiring assumptions are confirmed or explicitly disabled;
- at least one completed hardware validation experiment record exists;
- safety notes reflect the tested behavior and remaining limitations.

## Later

- Add sanitized examples.
- Add diagrams that do not expose live topology.
- Document compatibility boundaries for board revisions and relay wiring.
