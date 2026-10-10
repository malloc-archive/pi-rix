# Moving to ESP32-S3 and handling audio disconnects

*October 11, 2026 · Korea Standard Time*

The controller target changed from the Leonardo to an ESP32-S3-WROOM-1 N16R8. The ST7796S display, FT6336U touch interface, four latching switches, preset assignment screens, tuner, and shutdown UI remain part of the design.

## Hardware port

The firmware now uses explicit ESP32 SPI and I²C pins, with native USB CDC for the Pi connection. The display runs at 16 MHz SPI, touch at 100 kHz I²C, and GPIO21 controls the backlight. The PlatformIO environment configures 16 MB flash and 8 MB octal PSRAM. The current dependency pins are `espressif32@6.12.0` and Arduino_GFX `1.6.0`.

## Tuner redraw

The tuner status text sits below the meter's redraw rectangle. Clearing only the meter left previous `FLAT`, `SHARP`, `IN TUNE`, or `NO SIGNAL` text behind. A separate clear rectangle now covers the status text without erasing the static labels below it.

## Audio connection state

USB serial can remain connected when the iD4 is switched to the PC, so serial availability alone cannot indicate whether Guitarix is ready. The bridge now sends `C|1` and `C|0` status messages, repeated about once per second.

Offline status clears stale bank, preset, and tuner information and restores `CONNECTING` / `WAITING`. Preset and assignment commands are blocked while offline, and delayed state messages are discarded. Shutdown confirmation and status screens remain available. A heartbeat gap longer than six seconds after an online status also triggers the offline screen.

On recovery, the bridge sends online status before a fresh bank, mapping, and active-preset update. The existing mapping format and service paths remain compatible.

## Integration and validation

The existing installer bridge-copy step and Guitarix tuner startup option are retained. The old root-level Leonardo source is not part of the active PlatformIO build; the earlier implementation remains in Git history.

Python and shell syntax checks passed. The ESP32-S3 firmware has not been rebuilt or tested on hardware as part of this update. Next checks are a clean PlatformIO build, display and touch operation, all switch inputs, tuner redraw, and iD4 disconnect/reconnect with shutdown still available.
