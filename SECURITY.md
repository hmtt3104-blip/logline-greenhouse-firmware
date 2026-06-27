# Security

## Public export boundary

Repository status: Prototype.

Public readiness: NEEDS_CLEANUP.

Production readiness: Not production-ready.

This repository is a sanitized public firmware export, but it still needs hardware validation and review before it can be treated as public-ready or production-safe.

It must not contain:

- real Wi-Fi SSIDs;
- real Wi-Fi passwords;
- access point passwords used in deployment;
- private IP addresses;
- live topology;
- production hostnames;
- local absolute paths;
- logs;
- backups;
- binaries;
- images;
- API keys;
- Telegram tokens;
- Firebase credentials.

## Local configuration

Use `firmware/config.example.h` as the template.

Create `firmware/config.h` locally and do not commit it.

## Status escalation

If a secret, private IP, live topology, or private deployment detail is found in this repository, public readiness becomes BLOCKED until the detail is removed and any exposed credential is rotated.

If hardware behavior is uncertain, public readiness remains NEEDS_CLEANUP until the uncertainty is resolved or explicitly documented.

## Reporting

If a security issue is found, document the finding, remove the unsafe content from the public export, and rotate any exposed credential before treating the repository as safe again.
