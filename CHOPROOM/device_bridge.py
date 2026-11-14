#!/usr/bin/env python3
"""Loopback-only CHOPROOM device status bridge.

Serves no audio and never writes to Rekordbox, the DJ database or project
files. On explicit button press it can ask macOS to open Rekordbox or a
fixed-catalog instrument from the already installed local Music Hub.

The page is served from :49771, this bridge uses :49772. The only accepted
browser Origins are CHOPROOM's local origins. No arbitrary file paths or
commands are accepted from the browser.
"""
from __future__ import annotations

import json
import subprocess
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

HOST = "127.0.0.1"
PORT = 49772
HUB = "http://127.0.0.1:18769"
ALLOWED_ORIGINS = {
    "http://127.0.0.1:49771",
    "http://localhost:49771",
}
ALLOWED_INSTRUMENTS = {
    "vintage-sampler", "maschine-lab", "asr10-sampler",
    "flx4-flat", "flx4-live",
}
REKORDBOX_APP = "/Applications/rekordbox 7/rekordbox.app"


def _hub_json(route: str):
    assert route in (
        "/api/music-hub/instruments",
        "/api/music-hub/decks",
        "/api/music-hub/sampler/status",
    )
    try:
        req = Request(HUB + route, headers={"Accept": "application/json"})
        with urlopen(req, timeout=1.4) as response:
            data = response.read(98304)
            if response.status != 200 or len(data) >= 98304:
                return None
            parsed = json.loads(data)
            return parsed if isinstance(parsed, dict) else None
    except (URLError, TimeoutError, ValueError, OSError):
        return None


def _rekordbox_running():
    try:
        result = subprocess.run(
            ["/bin/ps", "-axo", "command="],
            capture_output=True, text=True, timeout=2,
            check=True,
        )
        needle = REKORDBOX_APP + "/Contents/MacOS/rekordbox"
        return any(line.strip() == needle or line.strip().startswith(needle + " ")
                   for line in result.stdout.splitlines())
    except (OSError, subprocess.SubprocessError):
        return False


def get_status():
    instruments = _hub_json("/api/music-hub/instruments")
    decks = _hub_json("/api/music-hub/decks")
    sampler = _hub_json("/api/music-hub/sampler/status")
    return {
        "ok": True,
        "rekordbox": {
            "running": _rekordbox_running(),
            "bridgeConnected": bool(instruments and
                                    instruments.get("rekordboxBridgeConnected") is True),
            "verifiedDecks": bool(decks and decks.get("verified") is True),
            "decks": decks.get("decks", {}) if isinstance(decks, dict) else {},
        },
        "musicHub": {
            "reachable": instruments is not None,
            "bridgeAvailable": bool(instruments and instruments.get("bridgeAvailable") is True),
            "items": instruments.get("items", []) if instruments else [],
        },
        "sampler": {
            "available": bool(sampler and sampler.get("ok") is True),
            "bank": sampler.get("bank") if sampler else None,
            "physicalPressCount": (
                sampler.get("bridge", {}).get("physical", {}).get("pad_presses_received")
                if sampler else None
            ),
        },
        "note": "App launch and status are real. Audio/MIDI routing is NOT implied by an app running.",
    }


def launch_instrument(instrument_id):
    if instrument_id not in ALLOWED_INSTRUMENTS:
        return 400, {"error": "Unknown instrument"}
    payload = json.dumps({"id": instrument_id}).encode("utf-8")
    req = Request(
        HUB + "/api/music-hub/instruments/launch", data=payload,
        headers={"Content-Type": "application/json"}, method="POST",
    )
    try:
        with urlopen(req, timeout=9) as response:
            return response.status, json.loads(response.read(8192))
    except HTTPError as exc:
        return exc.code, {"error": "Music Hub could not open that installed instrument"}
    except (URLError, TimeoutError, OSError, ValueError):
        return 503, {"error": "Local instrument launcher is unavailable"}


class Handler(BaseHTTPRequestHandler):
    server_version = "CHOPROOMDeviceBridge/1.0"
    def _is_allowed(self):
        if self.client_address[0] not in ("127.0.0.1", "::1"):
            return False
        host = self.headers.get("Host", "")
        if host not in (f"127.0.0.1:{PORT}", f"localhost:{PORT}"):
            return False
        origin = self.headers.get("Origin")
        return not origin or origin in ALLOWED_ORIGINS

    def _headers(self, status=200, kind="application/json; charset=utf-8", size=0):
        self.send_response(status)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(size))
        self.send_header("Cache-Control", "no-store")
        origin = self.headers.get("Origin")
        if origin in ALLOWED_ORIGINS:
            self.send_header("Access-Control-Allow-Origin", origin)
            self.send_header("Vary", "Origin")
            self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
            self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()

    def _json(self, status, data):
        body = json.dumps(data, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
        self._headers(status, size=len(body))
        self.wfile.write(body)

    def do_OPTIONS(self):
        if not self._is_allowed():
            self._json(403, {"error": "Local CHOPROOM page required"})
        elif self.path in ("/api/rekordbox/open", "/api/instruments/launch"):
            self._headers(204)
        else:
            self._json(404, {"error": "Unknown endpoint"})

    def do_GET(self):
        if not self._is_allowed():
            return self._json(403, {"error": "Local CHOPROOM page required"})
        if self.path == "/health":
            return self._json(200, {"ok": True, "bridge": "CHOPROOM"})
        if self.path == "/api/status":
            return self._json(200, get_status())
        return self._json(404, {"error": "Unknown endpoint"})

    def do_POST(self):
        if not self._is_allowed():
            return self._json(403, {"error": "Local CHOPROOM page required"})
        if self.path not in ("/api/rekordbox/open", "/api/instruments/launch"):
            return self._json(404, {"error": "Unknown endpoint"})
        if self.headers.get("Content-Type", "").split(";")[0].strip() != "application/json":
            return self._json(415, {"error": "Use application/json"})
        raw_length = self.headers.get("Content-Length", "")
        if not raw_length.isdecimal() or int(raw_length) > 1024:
            return self._json(413, {"error": "Invalid body size"})
        try:
            payload = json.loads(self.rfile.read(int(raw_length)))
        except (ValueError, UnicodeDecodeError):
            return self._json(400, {"error": "Invalid JSON"})
        if not isinstance(payload, dict):
            return self._json(400, {"error": "JSON object required"})
        if self.path == "/api/instruments/launch":
            if set(payload.keys()) != {"id"}:
                return self._json(400, {"error": "Expected installed instrument ID"})
            status, result = launch_instrument(payload["id"])
            return self._json(status, result)
        if payload:
            return self._json(400, {"error": "Open Rekordbox does not accept arguments"})
        try:
            result = subprocess.run(
                ["/usr/bin/open", "-a", REKORDBOX_APP],
                capture_output=True, text=True, timeout=8,
            )
            if result.returncode:
                raise OSError("macOS refused to open Rekordbox")
            return self._json(200, {
                "openRequested": True, "connected": False,
                "note": "Rekordbox open request accepted; DJ transport/MIDI routing remains separate.",
            })
        except (OSError, subprocess.SubprocessError):
            return self._json(503, {"error": "Cannot open the installed Rekordbox app"})

    def log_message(self, format, *args):
        pass


if __name__ == "__main__":
    ThreadingHTTPServer((HOST, PORT), Handler).serve_forever()
