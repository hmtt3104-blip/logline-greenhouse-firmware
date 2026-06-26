# Architecture

## Purpose

Describe the sanitized firmware architecture for the Waveshare ESP32-S3 Relay-6CH dual-zone greenhouse controller experiment.

## Main components

- ESP32-S3 firmware sketch.
- Two DHT sensor inputs.
- Two motor-control relay pairs for vent zones.
- One service motor relay pair.
- Local web interface served by the device.
- Wi-Fi station mode for local network connection.
- Fallback access point mode for setup and recovery.
- Local event log stored on device.

## Control model

Each zone has:

- current temperature and humidity state;
- target opening step;
- open relay output;
- close relay output;
- movement timer;
- safety/error state;
- manual and automatic command paths.

## Network model

The public export uses placeholders only.

Real network credentials belong in `firmware/config.h`, which is ignored by git.

This export does not document or include live network topology.

## Safety model

Relay outputs must be reviewed on bench hardware before live use.

Analog sensor pins are marked as unconfirmed placeholders in the firmware and should not be connected until the board pinout and voltage levels are verified.
