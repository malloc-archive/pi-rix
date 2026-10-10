# Project status

## Current implementation

The repository contains the Leonardo firmware, Python bridge, audio hotplug supervisor, two systemd user services, installation scripts, and a QjackCtl autostart entry.

The installer copies the bridge to the service's expected path. The supervisor starts Guitarix with tuner MIDI feedback enabled. Generated firmware binaries, downloaded PlatformIO libraries, editor configuration, and runtime preset mappings are excluded from Git.

## Validation

Python and shell syntax checks passed during repository integration. Service paths and serial message handlers were reviewed. The firmware was not rebuilt, and hardware, JACK, Guitarix, tuner, and shutdown tests were not rerun for this checkout.

Earlier hardware results are recorded in the development log. A fresh installation still needs verification on the Pi.

## Remaining work

- Document saved audio routing and target-machine dependencies and permissions.
- Record a reproducible firmware build with flash and SRAM usage.
- Run a hardware smoke test covering all eight slots, saved assignments, tuner, reconnect, and offline shutdown.
- Run longer audio-load tests with XRUN counts and measured round-trip latency.
- Add the complete power and wiring schematic.

No project license has been selected yet.
