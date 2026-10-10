# My controller protocol

I exchange newline-terminated ASCII lines over USB serial at 115200 baud. Switch indices are 0–3 and slot indices are 0–1. I keep full preset names on the Pi and send shortened display labels to the Leonardo.

## Leonardo to Pi

| Message | How I use it |
| --- | --- |
| `R` | Refresh bank, mappings, and active preset |
| `P|switch|slot` | Select the assigned preset |
| `B|-1` / `B|1` | Move to the previous / next nonempty bank |
| `L|page` | Request a zero-based page of five real presets |
| `S|switch|slot|index` | Save a bank-relative preset index; `-1` clears the slot |
| `H|POWEROFF` | Request shutdown after touchscreen confirmation |

## Pi to Leonardo

| Message | How I use it |
| --- | --- |
| `K|index|count|bank_name` | Bank header with zero-based index |
| `M|switch|slot|preset_name` | A slot label; `----` denotes an empty slot |
| `A|switch|slot|preset_name` | Confirm the current preset |
| `A|255|255|preset_name` | Show a current preset without a matching slot |
| `T|page|page_count` | Preset-browser pagination |
| `E|row|preset_name` | A preset-browser row, 0–4 |
| `U|OK` / `U|ERR` | Assignment-save result |
| `H|STARTED` / `H|ERR` | Shutdown-request status, not a completed-halt signal |
| `N|note_name|velocity` | Tuner note and tuning value, with 63 as center |

I limit bank labels to 24 ASCII characters and preset labels to 18. I replace pipe characters and line breaks before sending labels; non-ASCII characters become replacement characters. Preset selection uses the full names or indices on the Pi, not the shortened labels.

## Guitarix side

I send JSON-RPC as newline-delimited JSON over TCP to `127.0.0.1:7000`, not as HTTP. My bridge uses `banks`, `get`, `listen`, and `setpreset`, and handles `preset_changed` notifications. If a preset request remains unconfirmed for 0.8 seconds, I query the current state.

I store eight preset names per bank in `src/preset_map.json`, ordered FS1A, FS1B, FS2A, FS2B, FS3A, FS3B, FS4A, FS4B. I write a temporary file, flush and fsync it, then replace the mapping file. This reduces the risk of a truncated file; I do not claim full power-loss durability for every filesystem.
