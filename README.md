# pi-rix

A Raspberry Pi guitar multi-effects project, built with Guitarix, JACK, and a touchscreen foot controller.

A standalone rig for playing through Guitarix, switching presets with footswitches, and managing banks from a small display. A Raspberry Pi 4B 8 GB handles audio processing, an Audient iD4 MKII provides audio I/O, and an ESP32-S3-WROOM-1 N16R8 handles the controls.

## Hardware and software

| Component | Configuration |
| --- | --- |
| Audio host | Raspberry Pi OS / Debian 13 Trixie, ARM64 |
| Latest recorded RT kernel | `6.18.50+rpt-rpi-v8-rt` |
| Audio engine | JACK + Guitarix; bridge targets Guitarix 0.46 JSON-RPC |
| Working audio baseline | 48 kHz, 256 frames per period, 3 periods |
| Controller | ESP32-S3-WROOM-1 N16R8, four latching 3PDT switches |
| Display / touch | LCDWIKI MSP4031 ST7796S SPI TFT, 480 × 320 landscape / FT6336U I²C |
| Control link | Native USB CDC at 115200 baud; local TCP JSON-RPC on port 7000 |

## Features

- Four footswitches with A/B assignments: eight preset slots per bank.
- Bank navigation, touchscreen preset assignment, and saved per-bank mappings.
- Active-preset feedback from Guitarix, including external preset changes.
- MIDI tuner feedback displayed on the controller.
- Confirmed touchscreen shutdown requests, including standby while Guitarix is offline.
- Reconnection handling for Guitarix and the controller.
- Connection heartbeat, stale-state clearing, and automatic return to `CONNECTING` when the audio engine is unavailable.

The repository includes the iD4 hotplug supervisor, systemd user services, installation scripts, and QjackCtl autostart entry. The supervisor starts JACK and Guitarix when the interface appears and cleans them up when it disappears. Dependencies, permissions, and audio routing need to be configured on the Pi before using the services.

## Source and documentation

| Path | Contents |
| --- | --- |
| [`src/main.cpp`](src/main.cpp) | ESP32-S3 firmware |
| [`src/guitarix_bridge.py`](src/guitarix_bridge.py) | Guitarix control, tuner, and shutdown bridge |
| [`src/audio_hotplug.py`](src/audio_hotplug.py) | JACK/Guitarix hotplug supervisor |
| [`src/install.sh`](src/install.sh) | User-service installer |
| [`platformio.ini`](platformio.ini) | ESP32-S3 N16R8 build configuration |
| [`docs/setup.md`](docs/setup.md) | Wiring, dependencies, and manual startup notes |
| [`docs/protocol.md`](docs/protocol.md) | The serial protocol used by the firmware and bridge |
| [`docs/devlog/`](docs/devlog/) | Development notes in chronological order |
| [`docs/status.md`](docs/status.md) | Validation limits and remaining repository work |

## Development log

1. [Getting guitar audio running on the Pi](docs/devlog/01-audio-and-realtime.md) — September 8–10, 2026.
2. [The first Leonardo controller](docs/devlog/02-midi-and-switches.md) — September 10–11, 2026.
3. [Bringing the controller and headless audio together](docs/devlog/03-touchscreen-and-headless.md) — October 9–11, 2026.

4. [Adding deployment files](docs/devlog/04-deployment-files.md) — October 11, 2026.

5. [Moving to ESP32-S3 and handling audio disconnects](docs/devlog/05-esp32s3-and-connection-state.md) — October 11, 2026.

Dates use Korea Standard Time.

The ESP32-S3 port has not yet been verified by a fresh firmware build or hardware test. Earlier hardware results in the development log refer to the Leonardo setup.
