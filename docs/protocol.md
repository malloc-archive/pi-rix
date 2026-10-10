# Controller protocol

The controller and bridge exchange newline-terminated ASCII lines over USB serial at 115200 baud. Switch indices are 0–3 and slot indices are 0–1. Full preset names stay on the Pi; shortened display labels are sent to the ESP32-S3.

## ESP32-S3 to Pi

| Message | Purpose |
| --- | --- |
| `R` | Refresh bank, mappings, and active preset |
| `P|switch|slot` | Select the assigned preset |
| `B|-1` / `B|1` | Move to the previous / next nonempty bank |
| `L|page` | Request a zero-based page of five real presets |
| `S|switch|slot|index` | Save a bank-relative preset index; `-1` clears the slot |
| `H|POWEROFF` | Request shutdown after touchscreen confirmation |

## Pi to ESP32-S3

| Message | Purpose |
| --- | --- |
| `K|index|count|bank_name` | Bank header with zero-based index |
| `M|switch|slot|preset_name` | A slot label; `----` denotes an empty slot |
| `A|switch|slot|preset_name` | Confirm the current preset |
| `A|255|255|preset_name` | Show a current preset without a matching slot |
| `T|page|page_count` | Preset-browser pagination |
| `E|row|preset_name` | A preset-browser row, 0–4 |
| `U|OK` / `U|ERR` | Assignment-save result |
| `H|STARTED` / `H|ERR` | Shutdown-request status, not a completed-halt signal |
| `C|1` / `C|0` | Guitarix online / offline status and periodic heartbeat |
| `N|note_name|velocity` | Tuner note and tuning value, with 63 as center |

Bank labels are limited to 24 ASCII characters and preset labels to 18. Pipe characters and line breaks are replaced before transmission; non-ASCII characters become replacement characters. Preset selection uses the full names or indices on the Pi, not the shortened labels.

## Guitarix side

The bridge sends JSON-RPC as newline-delimited JSON over TCP to `127.0.0.1:7000`, not as HTTP. It uses `banks`, `get`, `listen`, and `setpreset`, and handles `preset_changed` notifications. If a preset request remains unconfirmed for 0.8 seconds, the bridge queries the current state.

The mapping file beside the bridge stores eight preset names per bank, ordered FS1A, FS1B, FS2A, FS2B, FS3A, FS3B, FS4A, FS4B. Saving writes a temporary file, flushes and fsyncs it, then replaces the mapping file. This reduces the risk of a truncated file; full power-loss durability depends on the filesystem.

## Connection-state handling

The bridge announces online status before sending fresh bank and preset data, then repeats the status about once per second. Offline status clears the displayed state and blocks preset, bank, and assignment requests. Stale responses are ignored until online status returns; shutdown messages remain available. After an online heartbeat has been received, a gap longer than six seconds also returns the UI to `CONNECTING`.
