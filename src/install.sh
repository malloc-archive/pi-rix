#!/bin/sh
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
mkdir -p "$HOME/.local/bin" "$HOME/.config/systemd/user" "$HOME/.config/autostart"
cp "$HERE/audio_hotplug.py" "$HOME/.local/bin/audio_hotplug.py"
chmod +x "$HOME/.local/bin/audio_hotplug.py"
cp "$HERE/guitarix-hotplug.service" "$HERE/guitarix-bridge.service" "$HOME/.config/systemd/user/"
cp "$HERE/qjackctl.desktop" "$HOME/.config/autostart/"
cp "$HERE/guitarix_bridge.py" "$HOME/guitarix_bridge.py"
systemctl --user daemon-reload
systemctl --user enable --now guitarix-hotplug.service guitarix-bridge.service
echo "Installed. Next: run sudo loginctl enable-linger \"$USER\" for startup without graphical login."
echo "Important: Disable qjackctl's auto-start-JACK option and close old jackd/guitarix instances."
