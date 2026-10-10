# Getting my guitar running on the Pi

*September 8–10, 2026 · retrospective, dates in Korea Standard Time*

I started pi-rix to build a Raspberry Pi guitar multi-effects unit around hardware I already had: a Raspberry Pi 4B, an Audient iD4 MKII, and an Arduino Leonardo. I wanted to edit presets remotely, select them with physical switches, and see the current preset without keeping a desktop interface open.

## My first working audio path

I got live guitar processing working through JACK and Guitarix, with the iD4 handling input and output. The next problem was making the sound reliable enough to play through.

At 48 kHz, 128 frames per period, and three periods, some Guitarix presets produced XRUNs and audible interruptions. Closing Guitarix stopped the XRUNs, and increasing the buffer to 256 frames was stable in my reported tests. At the time, I measured about 32.6 °C, a 1.8 GHz ARM clock, and `throttled=0x0`.

I kept 48 kHz / 256 / 3 as my practical baseline. I did not measure end-to-end guitar latency, so I do not treat buffer arithmetic or scheduler latency as an audio round-trip measurement.

## Working on real-time scheduling

I explored an ARM64 kernel cross-build using Docker on my PC. I wanted to keep my existing Pi installation and retain a bootable fallback kernel. My build notes included `CONFIG_PREEMPT_RT=y` and `CONFIG_HZ=250`.

My build output and the kernel I eventually booted had different version labels. I therefore keep the cross-build experiment separate from the installed-kernel result rather than claiming that one proves the other.

I later booted `6.18.39+rpt-rpi-v8-rt`. One recorded 60-second cyclictest run showed maximum scheduling latencies of 107, 173, 159, and 130 microseconds across four threads. These were short observations, not a guarantee against XRUNs under every preset or load.

## What I carried forward

I needed both a reliable audio configuration and a usable controller. The real-time kernel was one part of that work; it did not remove the need to test the actual Guitarix signal chain.
