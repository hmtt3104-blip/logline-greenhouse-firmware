# Setup

## Local configuration

Copy:

```text
firmware/config.example.h
```

to:

```text
firmware/config.h
```

Then replace the placeholder values in `firmware/config.h`.

`firmware/config.h` is ignored by git and must stay local.

## Build notes

Open `firmware/greenhouse-dualzone-waveshare-s3-relay6ch.ino` in the Arduino toolchain configured for ESP32-S3.

Select the board profile that matches the test Waveshare ESP32-S3 Relay-6CH hardware.

## Bench-first rule

Flash only on bench hardware or a reviewed non-production setup until the sanitized export has been validated.

## Validation checklist

- Confirm the sketch builds with local `firmware/config.h`.
- Confirm relay pins match the test board.
- Confirm relay active level before connecting motors.
- Confirm DHT pins and sensor type.
- Confirm fallback AP starts with local placeholder-derived config.
- Confirm the web interface does not expose passwords.
- Confirm static private IP assumptions are not present.
