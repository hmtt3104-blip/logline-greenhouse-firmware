# Security

## Public export boundary

This repository is a sanitized public firmware export candidate.

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

## Reporting

If a secret or private deployment detail is found in this repository, treat the export as blocked until the detail is removed and any exposed credential is rotated.
