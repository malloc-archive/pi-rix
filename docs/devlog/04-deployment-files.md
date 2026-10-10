# Adding deployment files

*October 11, 2026 · Korea Standard Time*

Added the audio hotplug supervisor, bridge and hotplug user services, service installer, shutdown-permission installer, and QjackCtl autostart entry. The firmware and Python bridge are unchanged.

Two integration gaps needed attention: the installer did not copy the bridge to `~/guitarix_bridge.py`, where the service expects it, and the supervisor's Guitarix command lacked `--tuner-midi-feedback`. Both are now included.

The supervisor and bridge remain separate user services so the controller can accept shutdown requests while the audio interface is away. JACK retains the 48 kHz / 256 / 3 configuration and real-time priority 70.

Python and shell syntax checks passed. The updated installation has not been retested on the Pi; saved audio routing and target-machine permissions are still required for a fresh setup.
