# Safety

## Status

Repository status: Prototype.

Public readiness: NEEDS_CLEANUP.

Production readiness: Not production-ready.

Reason:

- This is a sanitized public Logline export.
- Local Arduino compile reproducibility has been verified.
- Serial Monitor bare-board bench readiness has been reviewed.
- Fresh hardware validation after sanitization is still required.
- Relay behavior, board revision, analog sensor pins, and live bench behavior still need documented checks.

## Public readiness checklist result

Final public readiness status: NEEDS_CLEANUP.

Reason:

- The public tree is sanitized and excludes real Wi-Fi credentials, access point passwords, private IP addresses, live topology, runtime configuration, logs, backups, binaries, images, and deployment notes.
- Build reproducibility is verified with Arduino CLI using `esp32:esp32:esp32s3`.
- Hardware behavior is not validated in this clean export.
- Relay behavior, board revision, analog sensor pins, startup relay behavior, and bench boot behavior still need documented checks.
- The existing experiment record documents build evidence and serial bench readiness, but no completed hardware behavior validation exists yet.

Finding statuses:

| Area | Finding status | Notes |
| --- | --- | --- |
| Secrets and credentials | OK | Real Wi-Fi credentials, AP passwords, and private configs are excluded. |
| Private topology | OK | Private IPs and live greenhouse topology are excluded. |
| Runtime artifacts | OK | Logs, backups, binaries, images, and deployment notes are excluded. |
| Build reproducibility | OK | Arduino CLI compile passed with `esp32:esp32:esp32s3`. |
| Serial bench readiness | OK | Bare-board Serial Monitor checklist exists; hardware has not been tested yet. |
| Hardware validation | WARNING | Relay behavior, board revision, analog pins, startup close behavior, and bench boot behavior are not validated on hardware in this clean export. |
| Experiment records | WARNING | Build evidence exists, but a completed hardware behavior validation record is still missing. |
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
- First board power-up must be USB/bare-board/no-load only.
- Startup close is enabled in the current firmware; close relays may energize for about `10000 ms` on boot.
- Do not connect motors, actuators, or real relay loads during the first Serial Monitor test.
- Do not run OPEN/CLOSE commands or web UI motor controls until relay active state and wiring are verified.
- Keep analog sensors disconnected until pins and voltage dividers are verified.
- Treat DHT readings as fallible and test sensor failure paths.
- DHT `NAN` errors are expected when no DHT sensors are connected during a no-sensor bench test.
- Do not rely on the web UI as the only emergency stop path.
- Do not use this export for unattended live operation without a separate hardware safety review.

## Public safety rule

If this repository ever contains live credentials, private IPs, or topology, public readiness becomes BLOCKED until cleaned and reviewed.

If hardware behavior is uncertain, public readiness remains NEEDS_CLEANUP until the uncertainty is resolved or explicitly documented.
