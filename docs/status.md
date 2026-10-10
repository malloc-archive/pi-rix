# My current status

## What I imported

I imported `src/main.cpp` and `platformio.ini` from `pi-rix(2).zip`. I replaced the older bundled bridge with the separately supplied `guitarix_bridge(1).py`, keeping its contents unchanged under `src/guitarix_bridge.py`.

I left generated firmware binaries, downloaded PlatformIO libraries, and local editor configuration out of Git. I added dependency and setup documentation, and ignore runtime preset mappings so my personal assignments are not accidentally committed.

## Validation for this import

I checked Python syntax and reviewed the matching serial message handlers in the firmware and bridge. I also checked the imported source against the supplied files. I did not rebuild the Leonardo firmware here because PlatformIO is not installed in the import environment. I did not rerun hardware, JACK, Guitarix, tuner, or shutdown tests during this import.

My earlier hardware observations are recorded in the development log. They are distinct from fresh validation of this repository checkout.

## What I still need to capture

- My deployed `audio_hotplug.py`, audio routing, and service units.
- A reproducible firmware build with flash and SRAM usage recorded.
- A hardware smoke test covering all eight slots, saved assignments, tuner, reconnect, and offline shutdown.
- Longer audio-load tests with XRUN counts and measured round-trip latency.
- My complete power and wiring schematic.

I have not added a license file because I have not selected a license for this project.
