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

Open `firmware/firmware.ino` in the Arduino toolchain configured for ESP32-S3.

The current local build check uses:

- board label: `ESP32S3 Dev Module`
- FQBN: `esp32:esp32:esp32s3`

Bundled Arduino CLI command example:

```powershell
& "C:\Users\V\AppData\Local\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" compile --fqbn esp32:esp32:esp32s3 --build-path "$env:TEMP\logline-greenhouse-firmware-build" firmware
```

This compile check verifies build reproducibility only. It does not validate relay wiring, sensor wiring, or safe hardware behavior.

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
