# Bringing the controller and headless audio together

*October 9–10, 2026 · retrospective, dates in Korea Standard Time*

## Checking my RT kernel again

On October 9, I found that the Pi was running `6.18.50+rpt-rpi-v8` without `CONFIG_PREEMPT_RT` enabled. I switched to the packaged `6.18.50+rpt-rpi-v8-rt` kernel and confirmed `CONFIG_PREEMPT_RT=y` in its boot configuration.

I recorded this 60-second test:

```sh
sudo cyclictest -m -Sp90 -i1000 -D60
```

| Thread | Average (µs) | Maximum (µs) |
| --- | ---: | ---: |
| 0 | 11 | 145 |
| 1 | 13 | 157 |
| 2 | 13 | 156 |
| 3 | 16 | 142 |

I treat these as measurements from that run. I have not established long-duration stability or a controlled performance comparison against the earlier kernel.

## My touchscreen controller

My current firmware combines an ST7796S display, FT6336U touch input, and four latching switches. I can browse banks and assign actual Guitarix presets to eight A/B slots. The bridge stores these assignments by bank in `preset_map.json`.

I use newline-delimited JSON-RPC over TCP for Guitarix control. The Leonardo receives short serial messages for bank names, slot labels, and confirmed active presets. I pace display updates because drawing on the 16 MHz controller can block serial processing. The firmware also requests another refresh if it does not receive all eight mapping entries.

I align each switch's logical A/B state when a preset is selected from the screen or Guitarix, so the physical switch continues from the confirmed selection.

## Power, tuner, and USB switching

By October 10, I had reported successful touchscreen poweroff, tuner reception with the Guitarix GUI closed, and audio recovery after switching the iD4 away from and back to the Pi.

For the tuner, I used Guitarix's `--tuner-midi-feedback` option and its `gx_head_amp:midi_out_1` JACK port. My latest bridge reads `jack_midi_dump` output in a separate thread and forwards note names and tuning values to the Leonardo. I use 63 as the center value; I do not label it as a calibrated cents measurement.

For shutdown, the screen asks for confirmation before sending `H|POWEROFF`. The bridge checks permission for the exact systemctl poweroff command and acknowledges that shutdown has started. I still wait for the Pi to halt before removing power.

My deployed hotplug arrangement used a separate `audio_hotplug.py` process to manage JACK and Guitarix. That file was absent from my first source snapshot; I added it in the [next import](04-deployment-files.md). The bridge itself reconnects and keeps listening for poweroff requests while Guitarix is offline; it does not start or stop the audio services.

## Importing the source on October 11

I supplied the PlatformIO project as `pi-rix(2).zip`, followed by a newer `guitarix_bridge(1).py`. I use that newer file as `src/guitarix_bridge.py` here. It includes shutdown, tuner monitoring, and offline standby handling that were absent from the bridge inside the ZIP.

I reconstructed these entries from my earlier development conversations. The Git commits record this retrospective import, not backdated original development commits.
