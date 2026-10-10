# The first Leonardo controller

*September 10–11, 2026 · retrospective, dates in Korea Standard Time*

I first experimented with `MIDIUSB` and Program Change messages on the Leonardo. I confirmed that a message reached ALSA: `aseqdump` showed `Program change 0`.

## Debouncing each input

The early sketch shared state between buttons. That made one input affect another input's edge detection. Each switch needed independent previous-state and timing values.

The current firmware uses separate raw states, stable states, and last-change timestamps for all four inputs. It waits 40 ms before accepting a changed state. Four latching 3PDT switches are used, each with logical A/B slots, for eight assignments per bank.

## Upload interruptions

I also encountered an intermittent Leonardo upload problem. Uploading worked immediately after restarting my computer, but a later attempt failed after the 1200-baud touch reset changed the port from COM8 to COM6. A final fix is not recorded.

## Moving beyond the prototype

The current source no longer uses MIDI Program Change for preset selection. The controller sends requests over USB serial to a Python bridge, which selects Guitarix presets by bank and preset name through JSON-RPC. MIDI still has a separate role in the later tuner feedback path.

