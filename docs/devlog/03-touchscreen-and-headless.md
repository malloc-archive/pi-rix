# Bringing the controller and headless audio together

*October 9–10, 2026 · retrospective, dates in Korea Standard Time*

## Checking the RT kernel again

On October 9, I found that the Pi was running `6.18.50+rpt-rpi-v8` without `CONFIG_PREEMPT_RT` enabled. I switched to the packaged `6.18.50+rpt-rpi-v8-rt` kernel and confirmed `CONFIG_PREEMPT_RT=y` in its boot configuration.

Results from a 60-second test:

```sh
sudo cyclictest -m -Sp90 -i1000 -D60
```

| Thread | Average (µs) | Maximum (µs) |
| --- | ---: | ---: |
| 0 | 11 | 145 |
| 1 | 13 | 157 |
| 2 | 13 | 156 |
| 3 | 16 | 142 |

This short run does not establish long-duration stability or a controlled comparison against the earlier kernel.

## Touchscreen control

The firmware combines an ST7796S display, FT6336U touch input, and four latching switches. The touchscreen supports bank browsing and assignment of Guitarix presets to eight A/B slots. The bridge stores these assignments by bank in `preset_map.json`.

Guitarix control uses newline-delimited JSON-RPC over TCP. The Leonardo receives short serial messages for bank names, slot labels, and confirmed active presets. Display updates are paced because drawing on the 16 MHz controller can block serial processing. The firmware also requests another refresh if it does not receive all eight mapping entries.

Each switch's logical A/B state is aligned when a preset is selected from the screen or Guitarix, so the physical switch continues from the confirmed selection.

## Power, tuner, and USB switching

By October 10, touchscreen poweroff, tuner reception with the GUI closed, and audio recovery after switching the iD4 away from and back to the Pi were working.

For the tuner, I used Guitarix's `--tuner-midi-feedback` option and its `gx_head_amp:midi_out_1` JACK port. The bridge reads `jack_midi_dump` output in a separate thread and forwards note names and tuning values to the Leonardo. The center value is 63; this is not a calibrated cents measurement.

For shutdown, the screen asks for confirmation before sending `H|POWEROFF`. The bridge checks permission for the exact systemctl poweroff command and acknowledges that shutdown has started. The Pi must finish halting before power is removed.

The hotplug setup used a separate `audio_hotplug.py` process to manage JACK and Guitarix. The supervisor was added to the repository in the [deployment update](04-deployment-files.md). The bridge itself reconnects and keeps listening for poweroff requests while Guitarix is offline; it does not start or stop the audio services.

