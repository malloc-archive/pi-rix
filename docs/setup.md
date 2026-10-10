# Setup

## Controller wiring

Pin assignments are defined in `src/main.cpp`:

| Signal | ESP32-S3 GPIO |
| --- | --- |
| TFT CS | GPIO10 |
| TFT DC / LCD_RS | GPIO9 |
| TFT backlight control | GPIO21 |
| TFT reset | GPIO8 |
| SD CS | GPIO7 |
| Touch interrupt | GPIO6 |
| Touch reset | GPIO5 |
| Touch SDA / SCL | GPIO17 / GPIO18 |
| Touch I²C address | `0x38` |
| Footswitches 1–4 | GPIO4, GPIO15, GPIO16, GPIO14, using internal pull-ups |
| TFT SPI MOSI / SCK / MISO | GPIO11 / GPIO12 / GPIO13 |

Display and touch both use rotation 3. This signal map does not specify module power or level shifting; a complete electrical schematic is still needed.

GPIO numbers are not physical header positions. The source configures SPI at 16 MHz and touch I²C at 100 kHz. Backlight GPIO21 is a control signal, not a direct LED power output.

## Building the firmware

With PlatformIO installed, build and upload from the repository root:

```sh
pio run -e esp32s3_n16r8
pio run -e esp32s3_n16r8 -t upload
```

The build configuration declares `espressif32@6.12.0` and `moononournation/GFX Library for Arduino@1.6.0`. Generated `.pio` outputs and downloaded dependencies are excluded from Git.

The `esp32s3_n16r8` environment targets `esp32-s3-devkitc-1` with 16 MB QSPI flash and 8 MB octal PSRAM (`qio_opi`). Native USB CDC is enabled at boot. Connect the Pi to the native USB port marked OTG, and verify the resulting serial-device path before starting the bridge. The UI starts without waiting for a USB connection.

## Running the bridge

The bridge requires Python 3 and `pyserial`, plus these external programs for tuner and shutdown support:

- `jack_midi_dump`, `jack_lsp`, and `jack_connect`.
- `stdbuf`.
- `sudo` and `/usr/bin/systemctl` for the configured shutdown command.

For manual startup, install the Python dependency in a virtual environment:

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements.txt
```

Check `SERIAL_PORT` in `src/guitarix_bridge.py` before starting. It defaults to `/dev/ttyACM0`; a stable `/dev/serial/by-id/...` path can be substituted for the connected ESP32-S3. The process needs serial-device access and write access to the bridge directory because it stores `preset_map.json` there.

The recorded JACK baseline is:

```sh
jackd -dalsa -dhw:iD4 -r48000 -p256 -n3
```

Replace `iD4` if the Pi uses a different ALSA card identifier. Run only one audio supervisor or manual JACK instance at a time.

With JACK running, start headless Guitarix with RPC port 7000 and tuner MIDI feedback:

```sh
guitarix -N -p 7000 --tuner-midi-feedback
```

The hotplug supervisor manages process startup and recovery. It does not explicitly connect audio ports, so verify the saved Guitarix/JACK routing on the Pi.

Start the bridge from the repository root:

```sh
.venv/bin/python src/guitarix_bridge.py
```

The bridge connects to `127.0.0.1:7000`, reads the available banks, and creates default assignments from the first eight presets in each nonempty bank. Guitarix must have at least one bank containing presets. Keep the Guitarix RPC service local or restricted to a trusted network.

## Shutdown permission

The bridge uses `sudo -n /usr/bin/systemctl poweroff`. Grant the normal bridge account access to that exact command using `visudo`. Replace `BRIDGE_USER` with the actual account:

```sudoers
BRIDGE_USER ALL=(root) NOPASSWD: /usr/bin/systemctl poweroff
```

Verify that `/usr/bin/systemctl` exists on the target machine and that the exact command is permitted. Run the bridge as a normal user. `H|STARTED` means the shutdown request has started, not that filesystem unmounting or poweroff has finished.

## Tuner and reconnect behavior

The bridge connects `gx_head_amp:midi_out_1` to the newly created `jack_midi_dump` monitor input. Check those JACK port names when troubleshooting. The firmware clears a stale tuner reading after about 1.6 seconds.

When Guitarix disappears, the bridge keeps the serial connection open and accepts poweroff requests while waiting for RPC to return. Starting and stopping JACK/Guitarix when the iD4 is removed belongs to the separate hotplug supervisor, now included as `src/audio_hotplug.py`.

## Installing the user services

Use the normal Pi account that owns the Guitarix presets and JACK session. These services run `/usr/bin/python3`, so make `pyserial` available to that interpreter (for example, with the distribution's `python3-serial` package); the virtual environment above is only for manual startup.

Before installation, stop any manual JACK/Guitarix processes and disable QjackCtl's automatic JACK startup. From the repository root, run:

```sh
sh src/install.sh
bash src/install_power_permission.sh
sudo loginctl enable-linger "$USER"
```

Run both scripts as the normal account, without `sudo` in front of the script. The power-permission script requests elevation for its specific sudoers installation steps. Lingering lets the user services start without a graphical login.

The installer copies the bridge to `~/guitarix_bridge.py`, the supervisor to `~/.local/bin/audio_hotplug.py`, and both service units to `~/.config/systemd/user/`. It enables and starts the services immediately. After rerunning installation to update the source, restart the services explicitly:

```sh
systemctl --user restart guitarix-hotplug.service guitarix-bridge.service
systemctl --user status guitarix-hotplug.service guitarix-bridge.service
journalctl --user -u guitarix-hotplug.service -u guitarix-bridge.service -n 100 --no-pager
```

The installed bridge stores mappings at `~/preset_map.json`. When migrating from a manual run in `src/`, copy the existing mapping there before switching to the service, preserving any mapping already present in the home directory.

The supervisor watches `/proc/asound/iD4`, waits about 1.5 seconds after detection, and launches JACK with real-time priority 70, 48 kHz, 256 frames, three periods, and `-Xseq`. The service account needs suitable real-time scheduling permissions and access to the audio device. It starts Guitarix with port 7000 and tuner feedback enabled.

The QjackCtl desktop entry opens a monitor at graphical login; the entry itself does not disable QjackCtl's saved auto-start setting. Disable that setting in QjackCtl to avoid a second JACK owner.

## Connection status

The bridge sends `C|1` while Guitarix is ready and `C|0` while offline, normally once per second. On `C|0`, the firmware clears stale preset and tuner data and returns to `CONNECTING` / `WAITING`. Shutdown screens are preserved. Once online status has been observed, a heartbeat gap longer than six seconds also triggers the offline state. Recovery sends `C|1` before the refreshed bank, mappings, and active preset.
