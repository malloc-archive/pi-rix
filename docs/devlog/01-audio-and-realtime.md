# Getting guitar audio running on the Pi

*September 8–10, 2026 · retrospective, dates in Korea Standard Time*

I started pi-rix to build a Raspberry Pi guitar multi-effects unit around hardware I already had: a Raspberry Pi 4B, an Audient iD4 MKII, and an Arduino Leonardo. I wanted to edit presets remotely, select them with physical switches, and see the current preset without keeping a desktop interface open.

## The first working audio path

I got live guitar processing working through JACK and Guitarix, with the iD4 handling input and output. The next problem was making the sound reliable enough to play through.

At 48 kHz, 128 frames per period, and three periods, some Guitarix presets produced XRUNs and audible interruptions. Closing Guitarix stopped the XRUNs, and increasing the buffer to 256 frames was stable in those tests. At the time, I measured about 32.6 °C, a 1.8 GHz ARM clock, and `throttled=0x0`.

That made 48 kHz / 256 / 3 the practical baseline. End-to-end guitar latency was not measured; buffer arithmetic and scheduler latency are different measurements.

## Working on real-time scheduling

I explored an ARM64 kernel cross-build using Docker on my PC. I wanted to keep my existing Pi installation and retain a bootable fallback kernel. The build configuration included `CONFIG_PREEMPT_RT=y` and `CONFIG_HZ=250`.

The cross-build output and the kernel eventually booted had different version labels, so the installed-kernel result does not establish which build produced it.

I later booted `6.18.39+rpt-rpi-v8-rt`. One recorded 60-second cyclictest run showed maximum scheduling latencies of 107, 173, 159, and 130 microseconds across four threads. These were short observations, not a guarantee against XRUNs under every preset or load.

## Next steps

The next step was a usable controller alongside the working audio configuration. The real-time kernel was one part of that work; it did not remove the need to test the actual Guitarix signal chain.
