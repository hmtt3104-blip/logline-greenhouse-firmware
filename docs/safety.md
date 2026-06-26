# Safety

## Status

This is a sanitized public Logline export candidate.

It is not production-ready.

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

## Public safety rule

If this repository ever contains live credentials, private IPs, or topology, public readiness becomes blocked until cleaned and reviewed.
