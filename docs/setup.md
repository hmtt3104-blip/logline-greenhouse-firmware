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

Do not use the placeholder AP password for real bench or field use.

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

## Serial bench checklist

Use this only for a bare-board first test.

Do not connect motors, actuators, or real relay loads for the first Serial Monitor test.

Serial Monitor baud rate:

```text
115200
```

Expected startup indicators include:

- firmware startup banner;
- reset reason;
- boot counter;
- Wi-Fi/AP startup messages;
- AP URL;
- web server startup message;
- command help list;
- system status dump.

Important startup behavior:

- public default disables startup close;
- first boot should print `Startup close disabled: both zones assumed closed.`;
- no automatic startup close relay motion should be queued on boot;
- if startup close is re-enabled in a private deployment, close relays may energize on boot for `startupCloseMs`;
- watch relay LEDs only with no loads attached during the first test.

Expected without DHT sensors attached:

- DHT read errors such as `DHT returned NAN`;
- zone error state after repeated DHT failures.

These DHT errors are expected in a no-sensor bench test.

Safe first Serial commands before relay validation:

```text
help
status
wifi status
wifi scan
wifi off
wifi on
wifi reconnect
log
auto off
reset_alarm
z1 stop
z2 stop
```

Do not run these until relay active state and wiring are verified with no motor load:

```text
z1 open
z1 close
z2 open
z2 close
```

Also avoid web UI motor controls until relay wiring is verified.

## Bench-first rule

Flash only on bench hardware or a reviewed non-production setup until the sanitized export has been validated.

## Validation checklist

- Confirm the sketch builds with local `firmware/config.h`.
- Confirm the board boots and prints expected Serial Monitor startup diagnostics at `115200`.
- Confirm public default does not queue automatic startup close relay motion.
- If startup close is re-enabled in a private deployment, confirm startup close relay behavior on bare board with no loads attached.
- Confirm relay pins match the test board.
- Confirm relay active level before connecting motors.
- Confirm DHT pins and sensor type.
- Confirm expected DHT failure behavior when sensors are absent.
- Confirm fallback AP starts with local non-placeholder config.
- Confirm the web interface does not expose passwords.
- Confirm static private IP assumptions are not present.
