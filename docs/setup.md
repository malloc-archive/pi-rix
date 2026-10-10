# My setup notes

## Controller wiring

I use the pin assignments in `src/main.cpp`:

| Signal | Leonardo connection |
| --- | --- |
| TFT CS | D10 |
| TFT DC / LCD_RS | D9 |
| TFT reset | D8 |
| SD CS | D7 |
| Touch interrupt | D6 |
| Touch reset | D5 |
| Touch SDA / SCL | D2 / D3 |
| Touch I²C address | `0x38` |
| Footswitches 1–4 | A0, A1, A2, A3, using internal pull-ups |
| TFT SPI MOSI / SCK / MISO | ICSP header |

I use display rotation 3 and matching touch rotation 3. I treat this as a signal map, not a complete electrical schematic; the snapshot does not document module power or level-shifting details.

## Building my firmware

From the repository root, with PlatformIO installed, I build and upload with:

```sh
pio run -e leonardo
pio run -e leonardo -t upload
```

My configuration declares `moononournation/GFX Library for Arduino@^1.6.8`, plus `-mcall-prologues` and `-Wl,--relax`. I have not changed those options during the repository import. I exclude `.pio` outputs and downloaded dependencies from Git.

## Running my bridge

I need Python 3 and `pyserial`. I also need these external programs for the tuner and shutdown paths:

- `jack_midi_dump`, `jack_lsp`, and `jack_connect`.
- `stdbuf`.
- `sudo` and `/usr/bin/systemctl` for the configured shutdown command.

I can install the Python dependency in a virtual environment:

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements.txt
```

I check `SERIAL_PORT` in `src/guitarix_bridge.py` before starting. It defaults to `/dev/ttyACM0`; a stable `/dev/serial/by-id/...` path can be substituted for my connected Leonardo. My process needs serial-device access and write access to the bridge directory because it stores `preset_map.json` there.

My recorded JACK baseline is:

```sh
jackd -dalsa -dhw:iD4 -r48000 -p256 -n3
```

I use the actual ALSA card identifier on my Pi if it differs from `iD4`. I run only one audio supervisor or manual JACK instance at a time.

With JACK running, my headless Guitarix configuration needs RPC port 7000 and tuner MIDI feedback. The options from my working setup are:

```sh
guitarix -N -p 7000 --tuner-midi-feedback
```

I retain my working JACK audio routing from the Pi. My included hotplug supervisor manages process startup and recovery. It does not explicitly connect audio ports, so I verify my saved Guitarix/JACK routing on the Pi.

I start the bridge from the repository root:

```sh
.venv/bin/python src/guitarix_bridge.py
```

The bridge connects to `127.0.0.1:7000`, reads the available banks, and creates default assignments from the first eight presets in each nonempty bank. Guitarix must have at least one bank containing presets. I keep the Guitarix RPC service local or otherwise restricted to my trusted setup.

## Shutdown permission

My bridge deliberately uses `sudo -n /usr/bin/systemctl poweroff`. For my normal bridge account, I configure only that exact command using `visudo`; this is a template, with `BRIDGE_USER` replaced by the actual account:

```sudoers
BRIDGE_USER ALL=(root) NOPASSWD: /usr/bin/systemctl poweroff
```

I verify that `/usr/bin/systemctl` exists on the target machine and that the exact command is permitted. I do not run the entire bridge as root. `H|STARTED` means the shutdown request has started, not that filesystem unmounting or poweroff has finished.

## Tuner and reconnect behavior

My bridge connects `gx_head_amp:midi_out_1` to the newly created `jack_midi_dump` monitor input. I check those JACK port names when troubleshooting. The firmware clears a stale tuner reading after about 1.6 seconds.

When Guitarix disappears, the bridge keeps the serial connection open and accepts poweroff requests while waiting for RPC to return. Starting and stopping JACK/Guitarix when the iD4 is removed belongs to my separate hotplug supervisor, now included as `src/audio_hotplug.py`.

## Installing my user services

I use the normal Pi account that owns my Guitarix presets and JACK session. These services run `/usr/bin/python3`, so I make `pyserial` available to that interpreter (for example, with the distribution's `python3-serial` package); the virtual environment above is only for manual startup.

Before installation, I stop my old manual JACK/Guitarix processes and disable QjackCtl's automatic JACK startup. From the repository root, I run:

```sh
sh src/install.sh
bash src/install_power_permission.sh
sudo loginctl enable-linger "$USER"
```

I run both scripts as my normal account, not with `sudo` in front of the script. The power-permission script requests elevation for its specific sudoers installation steps. Lingering lets my user services start without a graphical login.

My installer copies the bridge to `~/guitarix_bridge.py`, the supervisor to `~/.local/bin/audio_hotplug.py`, and both service units to `~/.config/systemd/user/`. It enables and starts the services immediately. If I rerun installation after updating the source, I restart them explicitly:

```sh
systemctl --user restart guitarix-hotplug.service guitarix-bridge.service
systemctl --user status guitarix-hotplug.service guitarix-bridge.service
journalctl --user -u guitarix-hotplug.service -u guitarix-bridge.service -n 100 --no-pager
```

The installed bridge stores mappings at `~/preset_map.json`. If I previously ran it from `src/`, I copy my existing mapping there before switching to the service, preserving any mapping already present in my home directory.

The supervisor watches `/proc/asound/iD4`, waits about 1.5 seconds after detection, and launches JACK with real-time priority 70, 48 kHz, 256 frames, three periods, and `-Xseq`. My account still needs suitable real-time scheduling permissions and access to the audio device. It starts Guitarix with port 7000 and tuner feedback enabled.

The QjackCtl desktop entry opens a monitor at graphical login; the entry itself does not disable QjackCtl's saved auto-start setting. I configure that setting in QjackCtl to avoid a second JACK owner.
