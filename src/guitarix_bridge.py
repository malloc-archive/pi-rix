#!/usr/bin/env python3
"""Guitarix 0.46 JSON-RPC <-> controller USB Serial bank and preset controller.

controller -> Pi:
    R                 request complete bank / preset refresh
    P|switch|slot     select mapped preset (switch=0..3, slot=0..1)
    B|-1  or B|1     previous/next Guitarix bank
    L|page             fetch five Guitarix presets for setup screen
    S|switch|slot|index  persist preset assignment (-1 = unassign)
Pi -> controller:
    K|index|count|bank_name    bank title + page number (0-based index)
    M|switch|slot|preset_name  visible 8 slot names for this bank
    A|switch|slot|preset_name  confirmed Guitarix current preset
    A|255|255|preset_name     no matching slot
    T|page|page_count        setup preset list page
    E|row|preset_name       setup preset list entry
    U|OK or U|ERR           assignment saved / failed
    H|STARTED or H|ERR       system shutdown request state
    N|note_name|velocity     Guitarix MIDI tuner feedback (63=center)
    C|1 or C|0              Guitarix online/offline + periodic heartbeat
controller -> Pi (additional):
    H|POWEROFF              request safe shutdown (after UI confirmation)

The per-bank mappings are kept in preset_map.json beside this file.
JSON-RPC notifications must be newline-delimited TCP JSON, not HTTP.
"""

import contextlib
import json
import os
import queue
import re
import shutil
import socket
import subprocess
import threading
import time
from pathlib import Path

import serial

HOST = "127.0.0.1"
PORT = 7000
SERIAL_PORT = "/dev/ttyACM0"  # or /dev/serial/by-id/...
SERIAL_BAUD = 115200
CONFIG_PATH = Path(__file__).resolve().with_name("preset_map.json")
PRESETS_PER_PAGE = 5


def rpc_message(method, params=(), request_id=None):
    payload = {"jsonrpc": "2.0", "method": method, "params": list(params)}
    if request_id is not None:
        payload["id"] = request_id
    return (json.dumps(payload, ensure_ascii=False) + "\n").encode("utf-8")


def rpc_call(method, params=()):
    with socket.create_connection((HOST, PORT), timeout=3) as conn:
        conn.settimeout(5)
        conn.sendall(rpc_message(method, params, request_id=1))
        buf = bytearray()
        while True:
            part = conn.recv(8192)
            if not part:
                raise ConnectionError(f"Guitarix disconnected during {method}")
            buf.extend(part)
            if b"\n" in buf:
                reply = json.loads(bytes(buf).split(b"\n", 1)[0])
                if "error" in reply:
                    raise RuntimeError(f"Guitarix {method}: {reply['error']}")
                return reply["result"]
            if len(buf) > 2_000_000:
                raise ValueError(f"Oversize Guitarix reply for {method}")


def list_banks():
    result = rpc_call("banks", [])
    if not isinstance(result, list):
        raise TypeError("Unexpected Guitarix 'banks' response")
    banks = []
    for item in result:
        if isinstance(item, dict) and isinstance(item.get("name"), str):
            presets = [p for p in item.get("presets", []) if isinstance(p, str)]
            if presets:  # Cannot select a preset in an empty bank
                banks.append({"name": item["name"], "presets": presets})
    if not banks:
        raise RuntimeError("Guitarix has no banks with presets")
    return banks


def current_from_guitarix():
    bank = rpc_call("get", ["system.current_bank"])["system.current_bank"]
    preset = rpc_call("get", ["system.current_preset"])["system.current_preset"]
    return (str(bank), str(preset))


def defaults_for(banks):
    return {bank["name"]: bank["presets"][:8] + [""] * (8 - min(8, len(bank["presets"])))
            for bank in banks}


def load_mapping(banks):
    defaults = defaults_for(banks)
    original = {}
    if CONFIG_PATH.exists():
        document = json.loads(CONFIG_PATH.read_text(encoding="utf-8"))
        original = document
        if "banks" in document:
            if not isinstance(document["banks"], dict):
                raise ValueError("'banks' in preset_map.json must be an object")
            for name, mapping in document["banks"].items():
                if name in defaults:
                    if not isinstance(mapping, list) or len(mapping) != 8:
                        raise ValueError(f"{name}: expected 8 preset names")
                    defaults[name] = mapping
        elif "slots" in document:  # Migration from previous FS1A..FS4B file
            for i, entry in enumerate(document["slots"]):
                if i >= 8:
                    break
                if isinstance(entry, dict) and entry.get("bank") in defaults:
                    if entry.get("preset"):
                        defaults[entry["bank"]][i] = entry["preset"]
            print("[MIGRATION] Old single mapping converted to per-bank mappings", flush=True)
        else:
            raise ValueError("Unknown preset_map.json format")

    for bank in banks:
        selected = defaults[bank["name"]]
        valid = set(bank["presets"])
        for i, name in enumerate(selected):
            if not isinstance(name, str) or (name and name not in valid):
                raise ValueError(
                    f"{bank['name']} FS{i // 2 + 1}{'AB'[i % 2]}: "
                    f"preset {name!r} does not exist in Guitarix"
                )
    # Keep existing unknown bank mappings as backup if a bank is temporarily absent.
    previous = {}
    if CONFIG_PATH.exists():
        original = json.loads(CONFIG_PATH.read_text(encoding="utf-8"))
        previous = original.get("banks", {}) if isinstance(original.get("banks"), dict) else {}
    saved = {**previous, **defaults}
    if not CONFIG_PATH.exists() or original.get("banks") != saved:
        save_mapping_atomic(defaults)
    return defaults


def lcd_ascii(value, limit):
    return str(value).replace("|", "/").replace("\r", " ").replace("\n", " ").encode(
        "ascii", errors="replace"
    ).decode("ascii")[:limit]


def send(arduino, *items):
    arduino.write(("|".join(map(str, items)) + "\n").encode("ascii", errors="replace"))


def bank_index(banks, name):
    return next((i for i, b in enumerate(banks) if b["name"] == name), None)


def send_bank(arduino, banks, mapping, bank_name):
    index = bank_index(banks, bank_name)
    if index is None:
        return
    send(arduino, "K", index, len(banks), lcd_ascii(bank_name, 24))
    # The controller redraws individual TFT cards on each M message.
    # Pace serial updates so its USB receive buffer does not overflow.
    time.sleep(0.025)
    for i, name in enumerate(mapping[bank_name]):
        send(arduino, "M", i // 2, i % 2, lcd_ascii(name, 18) if name else "----")
        time.sleep(0.025)
    arduino.flush()
    print(f"[BANK] {index + 1}/{len(banks)}: {bank_name}", flush=True)


def send_preset_page(arduino, banks, selected_bank, page):
    """Send a page of REAL Guitarix presets; identity uses index, not LCD label."""
    bank = next(b for b in banks if b["name"] == selected_bank)
    names = bank["presets"]
    pages = max(1, (len(names) + PRESETS_PER_PAGE - 1) // PRESETS_PER_PAGE)
    page = max(0, min(page, pages - 1))
    send(arduino, "T", page, pages)
    time.sleep(0.03)
    for row, name in enumerate(names[page * PRESETS_PER_PAGE:(page + 1) * PRESETS_PER_PAGE]):
        send(arduino, "E", row, lcd_ascii(name, 18))
        time.sleep(0.04)  # TFT redraw on controller is blocking
    arduino.flush()


def save_mapping_atomic(mapping):
    """Do not leave a truncated JSON file when power disappears while saving."""
    old = {}
    if CONFIG_PATH.exists():
        original = json.loads(CONFIG_PATH.read_text(encoding="utf-8"))
        if isinstance(original.get("banks"), dict):
            old = original["banks"]
    document = {"banks": {**old, **mapping}}
    tmp = CONFIG_PATH.with_suffix(".json.tmp")
    with tmp.open("w", encoding="utf-8") as f:
        json.dump(document, f, ensure_ascii=False, indent=2)
        f.write("\n")
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, CONFIG_PATH)


def send_active(arduino, mapping, bank, preset, preferred_index=None):
    slots = mapping.get(bank, [""] * 8)
    found = next((i for i, name in enumerate(slots) if name and name == preset), None)
    if preferred_index is not None and 0 <= preferred_index < 8 and slots[preferred_index] == preset:
        found = preferred_index
    name = lcd_ascii(preset, 18) or "NO PRESET"
    if found is None:
        send(arduino, "A", 255, 255, name)
        print(f"[CURRENT] {bank} / {preset} (unassigned)", flush=True)
    else:
        send(arduino, "A", found // 2, found % 2, name)
        print(f"[CURRENT] FS{found // 2 + 1}{'AB'[found % 2]}: {bank} / {preset}", flush=True)


def next_bank(banks, current_bank, delta):
    index = bank_index(banks, current_bank)
    if index is None:
        index = 0
    target = banks[(index + delta) % len(banks)]
    return target["name"]


def select_bank_target(banks, mapping, bank_name):
    # First mapped preset in that bank; fallback if all slots intentionally empty.
    presets = mapping[bank_name]
    for index, preset in enumerate(presets):
        if preset:
            return preset, index
    bank = next(b for b in banks if b["name"] == bank_name)
    return bank["presets"][0], None


def request_safe_poweroff(arduino):
    """Shutdown through an exact sudoers rule; never use shell=True.

    The STARTED acknowledgement only means systemd was asked to power off.
    It does NOT mean the Pi has completed filesystem unmount / halted.
    """
    command = ["sudo", "-n", "/usr/bin/systemctl", "poweroff"]
    try:
        permission = subprocess.run(
            ["sudo", "-n", "-l", "/usr/bin/systemctl", "poweroff"],
            capture_output=True, text=True, timeout=4, check=False,
        )
        if permission.returncode != 0:
            raise RuntimeError(permission.stderr.strip() or "No sudoers permission")
    except (OSError, subprocess.TimeoutExpired, RuntimeError) as exc:
        print(f"[POWER ERROR] Authorization failed: {exc}", flush=True)
        send(arduino, "H", "ERR")
        arduino.flush()
        return

    print("[POWER] Shutdown confirmed on touch screen; requesting poweroff", flush=True)
    send(arduino, "H", "STARTED")
    arduino.flush()
    time.sleep(0.2)  # allow the controller to draw the in-progress message
    try:
        result = subprocess.run(command, capture_output=True, text=True, timeout=8)
        if result.returncode != 0:
            raise RuntimeError(result.stderr.strip() or f"exit status {result.returncode}")
    except (OSError, subprocess.TimeoutExpired, RuntimeError) as exc:
        print(f"[POWER ERROR] {exc}", flush=True)
        send(arduino, "H", "ERR")
        arduino.flush()
        return

    # systemd is shutting down. Do not restart normal Guitarix communication.
    print("[POWER] poweroff requested; waiting for systemd to stop this process", flush=True)
    while True:
        time.sleep(10)


def guitarix_port_present():
    try:
        with socket.create_connection((HOST, PORT), timeout=0.4):
            return True
    except OSError:
        return False


def wait_for_guitarix_or_poweroff(arduino):
    """Keep accepting safe-poweroff even when the audio interface is on the PC.

    When USB switching removes the iD4, audio_hotplug.py stops Guitarix.
    The bridge must still read controller USB serial to accept H|POWEROFF.
    """
    buf = bytearray()
    next_check = 0.0
    next_status = 0.0
    print("[STANDBY] Guitarix offline; power button remains available", flush=True)
    while True:
        now = time.monotonic()
        if now >= next_status:
            send(arduino, "C", 0)
            next_status = now + 1.0
        if now >= next_check:
            if guitarix_port_present():
                return
            next_check = now + 1.0
        n = arduino.in_waiting
        if n:
            buf.extend(arduino.read(min(n, 256)))
            if len(buf) > 512:
                buf.clear()
            while b"\n" in buf:
                raw, _, rest = buf.partition(b"\n")
                buf = bytearray(rest)
                if raw.strip() == b"H|POWEROFF":
                    request_safe_poweroff(arduino)
        time.sleep(0.04)


# Examples from jack_midi_dump -r:
#     +4864: 90 40 36 note on (channel 0): pitch 64, velocity 54
TUNER_MIDI_RE = re.compile(r"\b9[0-9a-fA-F]\s+([0-9a-fA-F]{2})\s+([0-9a-fA-F]{2})\b")
NOTE_NAMES = ("C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B")


def midi_to_note(note):
    return NOTE_NAMES[note % 12] + str(note // 12 - 1)


def parse_tuner_midi(line):
    match = TUNER_MIDI_RE.search(line)
    if not match:
        return None
    note, velocity = (int(v, 16) for v in match.groups())
    if not (1 <= velocity <= 127 and 0 <= note <= 127):
        return None
    return (midi_to_note(note), velocity)


class TunerMidiMonitor:
    """Run jack_midi_dump in a subprocess; never parse MIDI in JACK's RT thread.

    JACK's monitor receives the MIDI events; an ordinary Python reader thread
    turns the text into the latest note. Serial is written ONLY on main thread.
    """
    def __init__(self):
        self.proc = None
        self.latest = queue.Queue(maxsize=8)
        self._thread = None
        self._last_start_attempt = 0.0
        self.connected = False

    @staticmethod
    def ports():
        result = subprocess.run(["jack_lsp"], stdout=subprocess.PIPE,
                                stderr=subprocess.DEVNULL, text=True, timeout=2,
                                check=False)
        return set(result.stdout.splitlines()) if result.returncode == 0 else set()

    def _read(self, proc):
        for line in proc.stdout:
            value = parse_tuner_midi(line)
            if value is not None:
                try:
                    self.latest.put_nowait(value)
                except queue.Full:
                    try:
                        self.latest.get_nowait()
                    except queue.Empty:
                        pass
                    try:
                        self.latest.put_nowait(value)
                    except queue.Full:
                        pass
        # Normal when JACK / USB interface is disconnected.

    def ensure_started(self):
        if self.proc is not None and self.proc.poll() is None:
            return
        if time.monotonic() - self._last_start_attempt < 3.0:
            return
        self.close()
        self._last_start_attempt = time.monotonic()
        if not shutil.which("jack_midi_dump") or not shutil.which("jack_connect"):
            print("[TUNER] jack_midi_dump / jack_connect missing", flush=True)
            return
        try:
            previous = self.ports()
            cmd = ["stdbuf", "-oL", "-eL", "jack_midi_dump", "-r"]
            self.proc = subprocess.Popen(cmd, stdout=subprocess.PIPE,
                                         stderr=subprocess.STDOUT,
                                         text=True, bufsize=1)
            self._thread = threading.Thread(target=self._read, args=(self.proc,), daemon=True)
            self._thread.start()
            monitor_port = None
            for _ in range(12):
                if self.proc.poll() is not None:
                    break
                created = self.ports() - previous
                choices = [name for name in created
                           if name.startswith("midi-monitor") and name.endswith(":input")]
                if choices:
                    monitor_port = choices[0]
                    break
                time.sleep(0.1)
            if monitor_port:
                result = subprocess.run(
                    ["jack_connect", "gx_head_amp:midi_out_1", monitor_port],
                    capture_output=True, text=True, timeout=3, check=False)
                self.connected = result.returncode == 0 or "already connected" in result.stderr
                print(f"[TUNER] MIDI {monitor_port}: {'connected' if self.connected else result.stderr.strip()}", flush=True)
                if not self.connected:
                    self.close()  # JACK port may not be ready yet: retry soon
            else:
                print("[TUNER] Monitor JACK input port unavailable", flush=True)
                self.close()
        except (OSError, subprocess.TimeoutExpired) as exc:
            print(f"[TUNER] Starting monitor failed: {exc}", flush=True)
            self.close()

    def drain(self):
        newest = None
        while True:
            try:
                newest = self.latest.get_nowait()
            except queue.Empty:
                return newest

    def close(self):
        proc = self.proc
        self.proc = None
        self.connected = False
        if proc is not None:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=1)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait(timeout=1)
            if proc.stdout:
                proc.stdout.close()


def run_once(arduino):
    banks = list_banks()
    mapping = load_mapping(banks)

    with contextlib.ExitStack() as cleanup:
        listener = cleanup.enter_context(socket.create_connection((HOST, PORT), timeout=3))
        listener.settimeout(0.05)
        listener.sendall(rpc_message("listen", ["all"]))
        current = current_from_guitarix()
        visible_bank = current[0] if current[0] in mapping else banks[0]["name"]

        # Mark the audio engine online BEFORE sending the new preset state.
        # ESP32 ignores stale K/M/A packets while in offline/CONNECTING mode.
        send(arduino, "C", 1)
        last_status_sent = time.monotonic()
        send_bank(arduino, banks, mapping, visible_bank)
        send_active(arduino, mapping, *current)
        print("[READY] Bank arrows and footswitches enabled", flush=True)
        tuner = TunerMidiMonitor()
        cleanup.callback(tuner.close)
        tuner.ensure_started()
        last_tuner_sent = 0.0
        last_tuner_value = None

        rpc_buffer = bytearray()
        serial_buffer = bytearray()
        pending_target = None  # ((bank, preset), index or None, timestamp)
        while True:
            tuner.ensure_started()
            now = time.monotonic()
            if now - last_status_sent >= 1.0:
                send(arduino, "C", 1)
                last_status_sent = now
            latest = tuner.drain()
            if latest is not None and now - last_tuner_sent > 0.11:
                # One short ASCII message; no FFT or pitch detection on the controller.
                if latest != last_tuner_value or now - last_tuner_sent > 0.5:
                    send(arduino, "N", latest[0], latest[1])
                    last_tuner_value = latest
                    last_tuner_sent = now
            waiting = arduino.in_waiting
            if waiting:
                serial_buffer.extend(arduino.read(min(waiting, 512)))
                while b"\n" in serial_buffer:
                    raw, _, rest = serial_buffer.partition(b"\n")
                    serial_buffer = bytearray(rest)
                    fields = raw.decode("utf-8", errors="replace").strip().split("|")
                    if not fields:
                        continue
                    if fields == ["H", "POWEROFF"]:
                        request_safe_poweroff(arduino)
                    elif fields[0] == "R":
                        send_bank(arduino, banks, mapping, visible_bank)
                        send_active(arduino, mapping, *current)
                    elif fields[0] == "L" and len(fields) == 2:
                        try:
                            page = int(fields[1])
                        except ValueError:
                            continue
                        if 0 <= page <= 65535:
                            send_preset_page(arduino, banks, visible_bank, page)
                    elif fields[0] == "S" and len(fields) == 4:
                        try:
                            sw, slot, idx = map(int, fields[1:])
                        except ValueError:
                            send(arduino, "U", "ERR")
                            continue
                        bank = next(b for b in banks if b["name"] == visible_bank)
                        if not (0 <= sw < 4 and 0 <= slot < 2 and
                                (idx == -1 or 0 <= idx < len(bank["presets"]))):
                            send(arduino, "U", "ERR")
                            continue
                        name = "" if idx == -1 else bank["presets"][idx]
                        previous = mapping[visible_bank][sw * 2 + slot]
                        mapping[visible_bank][sw * 2 + slot] = name
                        try:
                            save_mapping_atomic(mapping)
                        except (OSError, ValueError) as exc:
                            mapping[visible_bank][sw * 2 + slot] = previous
                            print(f"[SAVE ERROR] {exc}", flush=True)
                            send(arduino, "U", "ERR")
                            continue
                        send(arduino, "M", sw, slot, lcd_ascii(name, 18) if name else "----")
                        time.sleep(0.035)
                        send_active(arduino, mapping, *current)
                        time.sleep(0.035)
                        send(arduino, "U", "OK")
                        arduino.flush()
                        print(f"[SAVED] {visible_bank} FS{sw + 1}{'AB'[slot]} -> {name or '(empty)'}", flush=True)
                    elif fields[0] == "P" and len(fields) == 3:
                        try:
                            sw, slot = int(fields[1]), int(fields[2])
                        except ValueError:
                            continue
                        if not (0 <= sw < 4 and 0 <= slot < 2):
                            continue
                        target = mapping[visible_bank][sw * 2 + slot]
                        if not target:
                            print(f"[EMPTY] FS{sw + 1}{'AB'[slot]}", flush=True)
                            continue
                        expected = (visible_bank, target)
                        print(f"[PRESET] {expected}", flush=True)
                        if expected == current:
                            send_active(arduino, mapping, *current, preferred_index=sw * 2 + slot)
                            pending_target = None
                        else:
                            listener.sendall(rpc_message("setpreset", expected))
                            pending_target = (expected, sw * 2 + slot, time.monotonic())
                    elif fields[0] == "B" and len(fields) == 2:
                        try:
                            delta = int(fields[1])
                        except ValueError:
                            continue
                        if delta not in (-1, 1):
                            continue
                        if len(banks) < 2:
                            continue
                        target_bank = next_bank(banks, visible_bank, delta)
                        target_preset, index = select_bank_target(banks, mapping, target_bank)
                        expected = (target_bank, target_preset)
                        print(f"[BANK REQUEST] {expected}", flush=True)
                        if expected == current:
                            visible_bank = target_bank
                            send_bank(arduino, banks, mapping, visible_bank)
                            send_active(arduino, mapping, *current, preferred_index=index)
                            pending_target = None
                        else:
                            listener.sendall(rpc_message("setpreset", expected))
                            pending_target = (expected, index, time.monotonic())
                if len(serial_buffer) > 512:
                    serial_buffer.clear()

            try:
                chunk = listener.recv(8192)
            except socket.timeout:
                chunk = None
            if chunk == b"":
                raise ConnectionError("Guitarix RPC listener disconnected")
            if chunk:
                rpc_buffer.extend(chunk)
                while b"\n" in rpc_buffer:
                    raw, _, rest = rpc_buffer.partition(b"\n")
                    rpc_buffer = bytearray(rest)
                    try:
                        event = json.loads(raw)
                    except (ValueError, UnicodeDecodeError):
                        continue
                    if event.get("method") == "preset_changed":
                        args = event.get("params", [])
                        if len(args) < 2:
                            continue
                        current = (str(args[0]), str(args[1]))
                        if current[0] != visible_bank and current[0] in mapping:
                            visible_bank = current[0]
                            send_bank(arduino, banks, mapping, visible_bank)
                        preference = None
                        if pending_target is not None and pending_target[0] == current:
                            preference = pending_target[1]
                        send_active(arduino, mapping, *current, preferred_index=preference)
                        pending_target = None
                    elif "error" in event:
                        print("[GUITARIX ERROR]", event["error"], flush=True)
                        pending_target = None

            if len(rpc_buffer) > 1_000_000:
                rpc_buffer.clear()
            # Guitarix implementations can suppress an event when selecting an
            # already-active preset. Poll only when a request remains unconfirmed.
            if pending_target and time.monotonic() - pending_target[2] > 0.8:
                checked = current_from_guitarix()
                if checked != current:
                    current = checked
                    if current[0] != visible_bank and current[0] in mapping:
                        visible_bank = current[0]
                        send_bank(arduino, banks, mapping, visible_bank)
                    pref = pending_target[1] if pending_target[0] == current else None
                    send_active(arduino, mapping, *current, preferred_index=pref)
                elif checked == pending_target[0]:
                    send_active(arduino, mapping, *current, preferred_index=pending_target[1])
                else:
                    print(f"[WARN] Guitarix did not switch to {pending_target[0]}", flush=True)
                pending_target = None


def main():
    print(f"Guitarix {HOST}:{PORT}, controller {SERIAL_PORT}", flush=True)
    while True:
        try:
            # Hold the controller serial connection even while Guitarix is offline.
            with serial.Serial(SERIAL_PORT, SERIAL_BAUD, timeout=0, write_timeout=2) as arduino:
                time.sleep(1.5)  # controller USB connection stabilization
                while True:
                    try:
                        run_once(arduino)
                    except KeyboardInterrupt:
                        raise
                    except (OSError, ValueError, TypeError, RuntimeError, KeyError) as exc:
                        print(f"[RECONNECT] Guitarix: {type(exc).__name__}: {exc}", flush=True)
                        send(arduino, "C", 0)
                        time.sleep(0.5)
                        wait_for_guitarix_or_poweroff(arduino)
        except KeyboardInterrupt:
            print("\nStopped", flush=True)
            return
        except (OSError, serial.SerialException) as exc:
            print(f"[RECONNECT] controller serial: {exc}", flush=True)
            time.sleep(2)


if __name__ == "__main__":
    main()
