# Safety

## Status

Repository status: Prototype.

Public readiness: NEEDS_CLEANUP.

Production readiness: Not production-ready.

Reason:

- This is a sanitized public Logline export.
- Fresh hardware validation after sanitization is still required.
- Relay behavior, board revision, analog sensor pins, and build reproducibility still need documented checks.

## Public readiness checklist result

Final public readiness status: NEEDS_CLEANUP.

Reason:

- The public tree is sanitized and excludes real Wi-Fi credentials, access point passwords, private IP addresses, live topology, runtime configuration, logs, backups, binaries, images, and deployment notes.
- Hardware behavior is not validated in this clean export.
- Relay behavior, board revision, analog sensor pins, and build reproducibility still need documented checks.
- No completed experiment record exists for the sanitized firmware export yet.

Finding statuses:

| Area | Finding status | Notes |
| --- | --- | --- |
| Secrets and credentials | OK | Real Wi-Fi credentials, AP passwords, and private configs are excluded. |
| Private topology | OK | Private IPs and live greenhouse topology are excluded. |
| Runtime artifacts | OK | Logs, backups, binaries, images, and deployment notes are excluded. |
| Hardware validation | WARNING | Relay behavior, board revision, analog pins, and build reproducibility are not validated in this clean export. |
| Experiment records | WARNING | `experiments/` is reserved until fresh hardware validation records exist. |
| Production safety | WARNING | Live vent/motor safety is not proven by this public export. |

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
