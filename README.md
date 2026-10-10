# pi-rix

My Raspberry Pi guitar multi-effects project, built with Guitarix, JACK, and a touchscreen foot controller.

I am building a standalone rig that lets me play through Guitarix, switch presets with my feet, and manage banks from a small display. I use a Raspberry Pi 4B 8 GB for audio processing, an Audient iD4 MKII for audio I/O, and an Arduino Leonardo for the controls.

## My current setup

| Component | My configuration |
| --- | --- |
| Audio host | Raspberry Pi OS / Debian 13 Trixie, ARM64 |
| Latest recorded RT kernel | `6.18.50+rpt-rpi-v8-rt` |
| Audio engine | JACK + Guitarix; bridge targets Guitarix 0.46 JSON-RPC |
| Working audio baseline | 48 kHz, 256 frames per period, 3 periods |
| Controller | Arduino Leonardo, four latching 3PDT switches |
| Display / touch | ST7796S SPI TFT, 480 × 320 landscape / FT6336U I²C |
| Control link | USB serial at 115200 baud; local TCP JSON-RPC on port 7000 |

## What I have implemented

- Four footswitches with A/B assignments: eight preset slots per bank.
- Bank navigation, touchscreen preset assignment, and saved per-bank mappings.
- Active-preset feedback from Guitarix, including external preset changes.
- MIDI tuner feedback displayed on the controller.
- Confirmed touchscreen shutdown requests, including standby while Guitarix is offline.
- Reconnection handling for Guitarix and the Leonardo.

I include my iD4 hotplug supervisor, systemd user services, installation scripts, and QjackCtl autostart entry. The supervisor starts JACK and Guitarix when the interface appears and cleans them up when it disappears. I configure dependencies, permissions, and my local audio routing on the Pi before using the services.

## Source and documentation

| Path | What I keep there |
| --- | --- |
| [`src/main.cpp`](src/main.cpp) | My Leonardo firmware from the supplied project snapshot |
| [`src/guitarix_bridge.py`](src/guitarix_bridge.py) | My latest separately supplied Python bridge |
| [`src/audio_hotplug.py`](src/audio_hotplug.py) | My JACK/Guitarix hotplug supervisor |
| [`src/install.sh`](src/install.sh) | My user-service installer |
| [`platformio.ini`](platformio.ini) | My Leonardo build configuration |
| [`docs/setup.md`](docs/setup.md) | Wiring, dependencies, and manual startup notes |
| [`docs/protocol.md`](docs/protocol.md) | The serial protocol used by my firmware and bridge |
| [`docs/devlog/`](docs/devlog/) | My development notes in chronological order |
| [`docs/status.md`](docs/status.md) | Validation limits and remaining repository work |

## My development log

1. [Getting my guitar running on the Pi](docs/devlog/01-audio-and-realtime.md) — September 8–10, 2026.
2. [My first Leonardo controller](docs/devlog/02-midi-and-switches.md) — September 10–11, 2026.
3. [Bringing the controller and headless audio together](docs/devlog/03-touchscreen-and-headless.md) — October 9–11, 2026.

using Korea Standard Time for dates. I separate my recorded hardware results from what can be verified from the source snapshot.

4. [Adding my deployment files](docs/devlog/04-deployment-files.md) — October 11, 2026.
