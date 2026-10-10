# Adding my deployment files

*October 11, 2026 · Korea Standard Time*

I added the remaining deployment files from `pi-rix10-11.zip`: my audio hotplug supervisor, bridge and hotplug user services, service installer, shutdown-permission installer, and QjackCtl desktop autostart entry. My firmware and Python bridge match the versions already imported.

I corrected two integration gaps while bringing these files together. My service installer now copies the included bridge to `~/guitarix_bridge.py`, matching its ExecStart path. My supervisor now passes `--tuner-midi-feedback` when it starts Guitarix, so the tuner path does not depend on a separate manual launch.

I keep the supervisor and bridge as separate user services so the controller can still accept shutdown requests while the audio interface is away. I retain the existing 48 kHz / 256 / 3 JACK configuration and real-time priority 70.

I checked Python and shell syntax for this update. I did not execute the installers, change a running Pi, or rerun hardware tests during the repository import. My saved audio routing and target-machine permissions still need to be in place for a fresh installation.
