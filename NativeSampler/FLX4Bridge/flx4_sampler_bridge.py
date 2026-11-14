#!/usr/bin/env python3
"""Route DDJ-FLX4 PAD FX pads exclusively to Ableton, VSL or direct audio.

The bridge observes the hardware MIDI input; it never sends messages back to
the DDJ-FLX4 or alters the software's existing deck/control MIDI routing.
"""
from __future__ import annotations

import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import heapq
import json
import queue
import signal
import sys
import threading
import time
import urllib.request

OUTPUT_PORT = "FLX4 to Vintage Sampler Lab"
ABLETON_PORT = "FLX4 PAD FX to Ableton"
HUB_API = "http://127.0.0.1:18769/api/music-hub/sampler"
# Unshifted FLX4 pad note ranges. PAD FX 1/2 use distinct MIDI notes from
# the same physical pads, so leaving the sampler-only range mapped meant
# pressing PAD FX produced no output. Keep SAMPLER mode for compatibility.
PAD_NOTE_BASES = {0x10: "padfx1", 0x30: "sampler", 0x50: "padfx2"}


def translate(message):
    """Return (note_on|note_off, note, velocity), or None for all other input.

    On the FLX4 unshifted performance pads use MIDI channels 8 and 10
    (zero-based 7, 9). Note ranges are PAD FX1 0x10..0x17,
    SAMPLER 0x30..0x37, and PAD FX2 0x50..0x57. They all map to
    the same eight chop positions per deck. Shifted channels 9/11 remain
    reserved for Rekordbox, as do transport and load messages.
    """
    if message.type not in ("note_on", "note_off"):
        return None
    channel = getattr(message, "channel", -1)
    note = getattr(message, "note", -1)
    if channel not in (7, 9):
        return None
    offsets = [note - base for base in PAD_NOTE_BASES if base <= note <= base + 7]
    if len(offsets) != 1:
        return None
    target = 36 + (0 if channel == 7 else 8) + offsets[0]
    velocity = max(0, min(127, int(getattr(message, "velocity", 0))))
    kind = "note_on" if message.type == "note_on" and velocity else "note_off"
    return kind, target, velocity if kind == "note_on" else 0


class MusicHubRoute:
    """Nonblocking controller-to-Music-Hub link, fail-closed when Hub is down.

    Mode/engine polling and browser event forwarding run off the MIDI thread.
    Never forward the same physical note to both the browser and VSL.
    """
    def __init__(self, base=HUB_API):
        self.base = base
        self.state = ("deck", "vsl")
        self.events = queue.Queue(maxsize=128)
        self.stop_requested = threading.Event()
        self.threads = [threading.Thread(target=self._poll, daemon=True),
                        threading.Thread(target=self._forward, daemon=True)]

    def start(self):
        for thread in self.threads:
            thread.start()

    def close(self):
        self.stop_requested.set()
        for thread in self.threads:
            thread.join(timeout=0.4)

    def _poll(self):
        while not self.stop_requested.is_set():
            try:
                with urllib.request.urlopen(self.base + "/status", timeout=0.18) as response:
                    result = json.load(response)
                mode, engine = result.get("mode"), result.get("engine")
                if mode not in ("deck", "sampler") or engine not in ("vsl", "browser", "direct", "ableton"):
                    raise ValueError("invalid Music Hub mode/engine")
                self.state = mode, engine
            except (OSError, ValueError, TypeError, json.JSONDecodeError):
                self.state = ("deck", "vsl")
            self.stop_requested.wait(0.2)

    def emit_browser(self, pad, pressed):
        try:
            self.events.put_nowait((pad, bool(pressed)))
        except queue.Full:
            pass

    def _forward(self):
        while not self.stop_requested.is_set():
            try:
                pad, pressed = self.events.get(timeout=0.1)
            except queue.Empty:
                continue
            payload = json.dumps({"pad": pad, "pressed": pressed}).encode("utf-8")
            request = urllib.request.Request(self.base + "/physical-event", data=payload,
                                             headers={"Content-Type": "application/json"})
            try:
                with urllib.request.urlopen(request, timeout=0.2):
                    pass
            except OSError:
                # Deliberately do not fall back to the VSL engine: that can
                # produce duplicate or unexpected audio when Hub recovers.
                pass


def start_local_api(event_queue, port, monitor=None):
    """Accept local Music Hub triggers; only the MIDI loop writes to the port."""
    class Handler(BaseHTTPRequestHandler):
        def reply(self, status, payload):
            body = json.dumps(payload).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            if self.path == "/status":
                return self.reply(200, {"ok": True, "pads": 16, "output": OUTPUT_PORT,
                                        "ableton_output": ABLETON_PORT,
                                        "input_modes": list(PAD_NOTE_BASES.values()),
                                        "physical": dict(monitor or {})})
            return self.reply(404, {"error": "unknown route"})

        def do_POST(self):
            if self.path != "/trigger":
                return self.reply(404, {"error": "unknown route"})
            try:
                size = int(self.headers.get("Content-Length", "0"))
                if size < 1 or size > 1024:
                    raise ValueError("invalid body size")
                data = json.loads(self.rfile.read(size))
                if not isinstance(data, dict):
                    raise ValueError("expected object")
                pad = data.get("pad")
                velocity = data.get("velocity", 127)
                action = data.get("action", "trigger")
                if type(pad) is not int or not 1 <= pad <= 16:
                    raise ValueError("pad must be integer 1..16")
                if type(velocity) is not int or not 1 <= velocity <= 127:
                    raise ValueError("velocity must be integer 1..127")
                if action not in ("trigger", "stop"):
                    raise ValueError("action must be trigger or stop")
            except (ValueError, json.JSONDecodeError, UnicodeDecodeError) as exc:
                return self.reply(400, {"error": str(exc)})
            try:
                event_queue.put_nowait((action, pad - 1, velocity))
            except queue.Full:
                return self.reply(503, {"error": "sampler event queue full"})
            return self.reply(200, {"ok": True, "pad": pad, "action": action})

        def log_message(self, _format, *_args):
            pass

    server = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", default="DDJ-FLX4", help="Substring of CoreMIDI input port name")
    parser.add_argument("--output", default=OUTPUT_PORT, help="Name for virtual MIDI output")
    parser.add_argument("--ableton-output", default=ABLETON_PORT,
                        help="Name for Ableton Simpler virtual MIDI output")
    parser.add_argument("--list", action="store_true", help="List MIDI inputs and exit")
    parser.add_argument("--http-port", type=int, default=18778,
                        help="Local Music Hub trigger API port (0 disables; default 18778)")
    args = parser.parse_args(argv)

    try:
        import mido
        import rtmidi  # noqa: F401 - required for mido's CoreMIDI virtual port
    except ImportError:
        parser.error("Install dependencies: python3 -m pip install mido python-rtmidi")

    mido.set_backend("mido.backends.rtmidi")
    names = [name for name in mido.get_input_names() if isinstance(name, str)]
    if args.list:
        for name in names:
            print(name)
        return 0

    matches = [name for name in names if args.input.lower() in name.lower()]
    if len(matches) != 1:
        print(f"Expected exactly one FLX4 MIDI input matching {args.input!r}; found {matches!r}.", file=sys.stderr)
        print("Available inputs:", names, file=sys.stderr)
        return 2

    running = True

    def stop(_signum, _frame):
        nonlocal running
        running = False

    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)
    # The output port is a separate virtual source that standalone JUCE hosts
    # can explicitly enable in Audio/MIDI settings; no device stealing.
    with mido.open_output(args.output, virtual=True) as output, mido.open_output(args.ableton_output, virtual=True) as ableton_output:
        with mido.open_input(matches[0]) as controller:
            print(f"Listening: {matches[0]} | virtual MIDI: {args.output}", flush=True)
            print("PAD FX 1 / PAD FX 2 / SAMPLER: left deck pads -> 1-8, right deck pads -> 9-16.", flush=True)
            # Remember each held note's original engine so switching modes
            # mid-press still releases the correct voice without stuck notes.
            held = {}
            hub = MusicHubRoute()
            hub.start()
            direct_audio = None
            next_direct_retry = 0.0
            next_audio_monitor_at = 0.0
            last_route = ("deck", "vsl")
            queued = queue.Queue(maxsize=256)
            deadlines = []  # (deadline, MIDI note, engine that received note_on)
            monitor = {"pad_presses_received": 0, "pad_presses_forwarded": 0,
                       "last_pad": None, "last_pad_mode": None, "last_midi_at": None,
                       "left_deck_presses": 0, "right_deck_presses": 0,
                       "direct_audio": {"ready": False, "last_error": None}}
            server = start_local_api(queued, args.http_port, monitor) if args.http_port else None
            if server:
                print(f"Music Hub API: http://127.0.0.1:{args.http_port}/trigger", flush=True)
            try:
                while running:
                    now = time.monotonic()
                    route = hub.state
                    if route != last_route:
                        if direct_audio is not None and last_route == ("sampler", "direct"):
                            # Release the FLX4 CoreAudio device when Ableton
                            # owns playback; no hidden second audio stream.
                            direct_audio.close()
                            direct_audio = None
                            monitor["direct_audio"] = {"ready": False, "reason": "Ableton/VSL/browser owns sampler output"}
                        last_route = route
                    if route == ("sampler", "direct") and direct_audio is None and now >= next_direct_retry:
                        try:
                            from direct_flx4_audio import DirectFLX4Audio
                            direct_audio = DirectFLX4Audio()
                            direct_audio.start()
                            monitor["direct_audio"] = direct_audio.status()
                            print("Direct FLX4 audio: 16 chops, 4-channel stream, MASTER 1/2.", flush=True)
                        except Exception as exc:
                            direct_audio = None
                            monitor["direct_audio"] = {"ready": False, "last_error": str(exc)[:180]}
                            print(f"Direct FLX4 audio unavailable: {exc}", file=sys.stderr, flush=True)
                            next_direct_retry = now + 5.0
                    if direct_audio is not None and now >= next_audio_monitor_at:
                        monitor["direct_audio"] = direct_audio.status()
                        next_audio_monitor_at = now + 0.15
                    while deadlines and deadlines[0][0] <= now:
                        _, note, dest = heapq.heappop(deadlines)
                        (ableton_output if dest == "ableton" else output).send(
                            mido.Message("note_off", channel=0, note=note, velocity=0))
                    for _ in range(32):
                        try:
                            action, index, velocity = queued.get_nowait()
                        except queue.Empty:
                            break
                        note = 36 + index
                        if hub.state == ("sampler", "direct"):
                            if direct_audio is not None:
                                if action == "trigger":
                                    direct_audio.trigger(index + 1, velocity)
                                else:
                                    direct_audio.stop_all()
                        elif hub.state == ("sampler", "ableton"):
                            if action == "trigger":
                                ableton_output.send(mido.Message("note_on", channel=0, note=note, velocity=velocity))
                                heapq.heappush(deadlines, (time.monotonic() + 0.16, note, "ableton"))
                            else:
                                ableton_output.send(mido.Message("note_off", channel=0, note=note, velocity=0))
                        elif action == "trigger":
                            output.send(mido.Message("note_on", channel=0, note=note, velocity=velocity))
                            heapq.heappush(deadlines, (time.monotonic() + 0.12, note, "vsl"))
                        else:
                            output.send(mido.Message("note_off", channel=0, note=note, velocity=0))
                    for msg in controller.iter_pending():
                        mapped = translate(msg)
                        if mapped is None:
                            continue
                        kind, note, velocity = mapped
                        if kind == "note_on":
                            monitor["pad_presses_received"] += 1
                            if msg.channel == 7:
                                monitor["left_deck_presses"] += 1
                            else:
                                monitor["right_deck_presses"] += 1
                            monitor["last_pad"] = note - 35
                            monitor["last_pad_mode"] = next(
                                (name for base, name in PAD_NOTE_BASES.items()
                                 if base <= msg.note <= base + 7), None)
                            monitor["last_midi_at"] = time.time()
                        # Ignore repeated note-on messages that would create
                        # stuck gated notes if the controller misses note-off.
                        if kind == "note_on":
                            if note in held:
                                continue
                            mode, engine = hub.state
                            if mode != "sampler":
                                continue
                            held[note] = engine
                        else:
                            engine = held.pop(note, None)
                            if engine is None:
                                continue
                        if engine == "browser":
                            hub.emit_browser(note - 35, kind == "note_on")
                        elif engine == "direct":
                            # Each pad is a one-shot: release merely permits
                            # retriggering, so quick taps play the full chop.
                            if kind == "note_on" and direct_audio is not None:
                                direct_audio.trigger(note - 35, velocity)
                        elif engine == "ableton":
                            ableton_output.send(mido.Message(kind, channel=0, note=note, velocity=velocity))
                        else:
                            output.send(mido.Message(kind, channel=0, note=note, velocity=velocity))
                        if kind == "note_on":
                            monitor["pad_presses_forwarded"] += 1
                    time.sleep(0.002)
            finally:
                if server:
                    server.shutdown()
                    server.server_close()
                for note, engine in sorted(held.items()):
                    if engine == "browser":
                        hub.emit_browser(note - 35, False)
                    elif engine == "ableton":
                        ableton_output.send(mido.Message("note_off", channel=0, note=note, velocity=0))
                    elif engine == "vsl":
                        output.send(mido.Message("note_off", channel=0, note=note, velocity=0))
                for _, note, dest in deadlines:
                    (ableton_output if dest == "ableton" else output).send(
                        mido.Message("note_off", channel=0, note=note, velocity=0))
                if direct_audio is not None:
                    direct_audio.close()
                # Allow pending release notifications to be sent before the
                # low-priority HTTP forwarding thread is stopped.
                until = time.monotonic() + 0.2
                while not hub.events.empty() and time.monotonic() < until:
                    time.sleep(0.01)
                hub.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
