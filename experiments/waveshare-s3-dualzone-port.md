# Waveshare S3 Dual-Zone Port

## Metadata

Date: 2026-06-26

Repository: logline-greenhouse-firmware

Domain: Greenhouse firmware / ESP32-S3 / relay control

Status: Draft / build verified / needs hardware validation

Trust level: Medium for sanitized build reproducibility; low for hardware behavior until bench validation is repeated from this sanitized export

Related decision: TBD

Related issue: TBD

Related experiments: dual-zone greenhouse firmware source snapshot

## Problem

The active greenhouse vent controller needs a public-safe firmware export without credentials, private IPs, live topology, or old private history.

## Hypothesis

The active Waveshare ESP32-S3 dual-zone sketch can be sanitized into a clean public export while preserving the core control logic for review and future experiments.

## Experiment

Copy the main sketch into a clean export, remove hardcoded network details, move credentials to ignored local configuration, remove password printing, document hardware assumptions, and verify that the sanitized Arduino sketch compiles locally.

## Environment

Hardware: Waveshare ESP32-S3 Relay-6CH candidate board.

Software: Arduino-style ESP32-S3 firmware sketch.

Configuration: local `firmware/config.h`, excluded from git.

Build tool used for verified compile:

```text
arduino-cli 1.5.1
esp32:esp32 2.0.17
DHT sensor library 1.4.7
Adafruit Unified Sensor 1.1.15
FQBN: esp32:esp32:esp32s3
Board label: ESP32S3 Dev Module
```

## Data

Verified local Arduino compile from the sanitized export:

```text
Sketch uses 813749 bytes (62%) of program storage space.
Global variables use 61016 bytes (18%) of dynamic memory.
```

No new bench hardware behavior data has been collected in this clean export step.

## Results

Build reproducibility: PASS.

Hardware behavior: NOT VALIDATED.

The sanitized sketch compiles locally with a local ignored `firmware/config.h` copied from `firmware/config.example.h` and placeholder/test values.

## Failures

The source snapshot contained hardcoded credentials and private network assumptions that had to be removed before public export.

The first build attempt failed before code compilation because the Arduino sketch folder and main file name did not follow Arduino CLI naming rules. The sketch was renamed to `firmware/firmware.ino`, after which compile passed.

## Lessons

Credential separation and hardware assumption documentation are required before this firmware can be reviewed publicly.

Arduino sketch naming must be reproducible from the public repo, not dependent on IDE-specific manual opening behavior.

A successful compile is necessary evidence, but it does not prove relay safety, board revision compatibility, analog sensor behavior, AP setup behavior, or safe live vent/motor operation.

## Hardware validation checklist

Before this experiment can be promoted beyond draft/build-verified status, validate on bench hardware:

- confirm exact Waveshare ESP32-S3 Relay-6CH board revision;
- confirm firmware flashes successfully to the selected board profile;
- confirm relay active state before connecting any motor load;
- confirm each documented relay pin maps to the expected physical relay;
- confirm DHT readings and failure behavior;
- confirm AP setup starts with a non-placeholder local password;
- confirm local web UI loads over bench/local network only;
- confirm manual OPEN/CLOSE commands do not overlap unsafe relay states;
- confirm failsafe behavior with sensor disconnect and Wi-Fi disconnect;
- record what was tested, what was not tested, and any deviations from public documentation.

## Next Question

Does the sanitized sketch behave correctly on bench hardware with local non-production configuration and no live vent/motor load?

## Reproducibility

PARTIAL.

Another engineer can clone the sanitized export, create local ignored `firmware/config.h`, and compile the firmware with Arduino CLI using `esp32:esp32:esp32s3`.

Hardware behavior still requires bench validation.

## Notes

This record intentionally excludes live deployment details.
