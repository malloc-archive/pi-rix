#!/usr/bin/env bash
set -euo pipefail

# Grant only the exact poweroff command to the CURRENT login account.
user="$(id -un)"
if [[ ! "$user" =~ ^[a-zA-Z_][a-zA-Z0-9_.-]*$ ]]; then
  echo "Unsupported username for sudoers entry" >&2
  exit 1
fi

if [[ ! -x /usr/bin/systemctl ]]; then
  echo "/usr/bin/systemctl is missing" >&2
  exit 1
fi

out="/etc/sudoers.d/guitarix-pedal-poweroff"
tmp="$(mktemp)"
trap 'rm -f "$tmp"' EXIT
printf '%s ALL=(root) NOPASSWD: /usr/bin/systemctl poweroff\n' "$user" > "$tmp"

# Do not install invalid sudoers syntax.
sudo /usr/sbin/visudo -cf "$tmp"
sudo install -o root -g root -m 0440 "$tmp" "$out"
sudo /usr/sbin/visudo -cf "$out"

echo "Installed: $out"
echo "Checking noninteractive authorization (does not power off):"
sudo -n -l /usr/bin/systemctl poweroff
