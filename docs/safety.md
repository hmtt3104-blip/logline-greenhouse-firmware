# Safety

## Status

Repository status: Prototype.

Public readiness: NEEDS_CLEANUP.

Production readiness: Not production-ready.

Reason:

- This is a sanitized public Logline export.
- Fresh hardware validation after sanitization is still required.
- Relay behavior, board revision, analog sensor pins, and build reproducibility still need documented checks.

## Excluded from this export

- real Wi-Fi credentials;
- real access point passwords;
- private IP addresses;
- live greenhouse topology;
- private runtime configuration;
- logs;
- backups;
- binaries;
- images;
- deployment notes.

## Firmware safety boundaries

- Review relay wiring before connecting motors.
- Confirm relay active state before live actuation.
- Keep analog sensors disconnected until pins and voltage dividers are verified.
- Treat DHT readings as fallible and test sensor failure paths.
- Do not rely on the web UI as the only emergency stop path.
- Do not use this export for unattended live operation without a separate hardware safety review.

## Public safety rule

If this repository ever contains live credentials, private IPs, or topology, public readiness becomes BLOCKED until cleaned and reviewed.

If hardware behavior is uncertain, public readiness remains NEEDS_CLEANUP until the uncertainty is resolved or explicitly documented.
