# Waveshare S3 Dual-Zone Port

## Metadata

Date: 2026-06-26

Repository: logline-greenhouse-firmware

Domain: Greenhouse firmware / ESP32-S3 / relay control

Status: Draft / needs validation

Trust level: Low until bench validation is repeated from this sanitized export

Related decision: TBD

Related issue: TBD

Related experiments: dual-zone greenhouse firmware source snapshot

## Problem

The active greenhouse vent controller needs a public-safe firmware export without credentials, private IPs, live topology, or old private history.

## Hypothesis

The active Waveshare ESP32-S3 dual-zone sketch can be sanitized into a clean public export while preserving the core control logic for review and future experiments.

## Experiment

Copy the main sketch into a clean export, remove hardcoded network details, move credentials to ignored local configuration, remove password printing, and document hardware assumptions.

## Environment

Hardware: Waveshare ESP32-S3 Relay-6CH candidate board.

Software: Arduino-style ESP32-S3 firmware sketch.

Configuration: local `firmware/config.h`, excluded from git.

## Data

No new hardware data collected in this clean export step.

## Results

Pending validation.

## Failures

The source snapshot contained hardcoded credentials and private network assumptions that had to be removed before public export.

## Lessons

Credential separation and hardware assumption documentation are required before this firmware can be reviewed publicly.

## Next Question

Does the sanitized sketch build and behave correctly on bench hardware with placeholder-derived local configuration?

## Reproducibility

PARTIAL.

Another engineer can inspect the sanitized source and setup notes, but hardware behavior still requires bench validation.

## Notes

This record intentionally excludes live deployment details.
