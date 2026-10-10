#!/usr/bin/env python3
"""Restart JACK + Guitarix when the USB iD4 connects/disconnects.

Run as the SAME normal Linux user that will use qjackctl and guitarix_bridge.py.
The bridge is managed separately (it already reconnects to Guitarix itself).
"""

import os
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path

CARD_ID = "iD4"                       # card used previously as hw:iD4
CARD_PATH = Path("/proc/asound") / CARD_ID
JACK_COMMAND = [
    "jackd", "-R", "-P70", "-dalsa", f"-dhw:{CARD_ID}",
    "-r48000", "-p256", "-n3", "-Xseq",
]
GUITARIX_COMMAND = ["guitarix", "-N", "-p", "7000", "--tuner-midi-feedback"]

# Do not allow a stray JACK client to spawn its own, conflicting JACK daemon.
CHILD_ENV = os.environ.copy()
CHILD_ENV["JACK_NO_START_SERVER"] = "1"
# Avoid JACK ALSA reservation failing if the graphical D-Bus session isn't up yet.
CHILD_ENV["JACK_NO_AUDIO_RESERVATION"] = "1"

jack_process = None
guitarix_process = None
stopping = False


def log(message):
    print(f"[{time.strftime('%F %T')}] {message}", flush=True)


def stop_process(proc, description):
    if proc is None or proc.poll() is not None:
        return
    log(f"Stopping {description}")
    # Each child has its own process group; this avoids leaving JACK helpers.
    try:
        os.killpg(proc.pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    try:
        proc.wait(timeout=4)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(proc.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        proc.wait()


def stop_audio():
    global jack_process, guitarix_process
    stop_process(guitarix_process, "Guitarix")
    guitarix_process = None
    stop_process(jack_process, "JACK")
    jack_process = None


def jack_is_ready():
    try:
        return subprocess.run(
            ["jack_lsp"], stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            env=CHILD_ENV, timeout=2, check=False,
        ).returncode == 0
    except (OSError, subprocess.TimeoutExpired):
        return False


def guitarix_rpc_is_ready():
    try:
        with socket.create_connection(("127.0.0.1", 7000), timeout=0.5):
            return True
    except OSError:
        return False


def wait_ready(check, proc, seconds):
    end = time.monotonic() + seconds
    while time.monotonic() < end and not stopping and CARD_PATH.exists():
        if proc.poll() is not None:
            return False
        if check():
            return True
        time.sleep(0.25)
    return False


def start_audio():
    global jack_process, guitarix_process
    log("iD4 detected: starting JACK")
    try:
        jack_process = subprocess.Popen(
            JACK_COMMAND, env=CHILD_ENV, stdin=subprocess.DEVNULL,
            start_new_session=True,
        )
        if not wait_ready(jack_is_ready, jack_process, 10):
            raise RuntimeError("JACK could not start / device unavailable")

        log("JACK ready: starting Guitarix RPC engine")
        guitarix_process = subprocess.Popen(
            GUITARIX_COMMAND, env=CHILD_ENV, stdin=subprocess.DEVNULL,
            start_new_session=True,
        )
        if not wait_ready(guitarix_rpc_is_ready, guitarix_process, 10):
            raise RuntimeError("Guitarix RPC 7000 not available")
        log("Guitarix ready; existing bridge should reconnect automatically")
        return True
    except (OSError, RuntimeError) as exc:
        log(f"START FAILED: {exc}")
        stop_audio()
        return False


def handle_signal(signum, frame):
    global stopping
    stopping = True


def main():
    signal.signal(signal.SIGTERM, handle_signal)
    signal.signal(signal.SIGINT, handle_signal)
    log(f"Watching {CARD_PATH} (JACK 48000Hz, p256, n3)")
    try:
        connected_since = None
        while not stopping:
            present = CARD_PATH.exists()
            running = (jack_process is not None and jack_process.poll() is None
                       and guitarix_process is not None and guitarix_process.poll() is None)

            if not present:
                if jack_process is not None or guitarix_process is not None:
                    log("iD4 removed: cleaning up audio engine")
                    stop_audio()
                connected_since = None
                time.sleep(0.5)
                continue

            if running:
                time.sleep(1)
                continue

            # Device present but JACK/Guitarix exited: restart both.
            if jack_process is not None or guitarix_process is not None:
                log("Audio engine exited: restarting")
                stop_audio()

            if connected_since is None:
                connected_since = time.monotonic()
            if time.monotonic() - connected_since < 1.5:
                time.sleep(0.25)
                continue

            if not start_audio():
                time.sleep(3)
            else:
                connected_since = None
    finally:
        stop_audio()
        log("Hotplug monitor stopped")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        log(f"FATAL: {exc}")
        raise
