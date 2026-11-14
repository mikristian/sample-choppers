"""Low-latency, polyphonic 16-chop audio output directly to DDJ-FLX4 MASTER.

This is a fallback for the Mac's Vintage Sampler Lab when a re-created
virtual MIDI port is not picked up by its running JUCE audio device manager.
The original source recording is never modified.  The physical MPC Sample is
not needed: this plays the same 16 WAV pads from the Expansion music workflow.

Uses PortAudio via a locally installed sounddevice wheel. Output channels
1/2 are FLX4 MASTER L/R; channels 3/4 remain quiet for headphone cue routing.
"""
from __future__ import annotations

import queue
import sys
import time
from pathlib import Path
import wave

_VENDOR = Path(__file__).resolve().parent / ".vendor"
if _VENDOR.is_dir() and str(_VENDOR) not in sys.path:
    sys.path.insert(0, str(_VENDOR))

try:
    import numpy as np
    import sounddevice as sd
except ImportError as exc:
    raise ImportError("Direct FLX4 audio requires numpy, sounddevice, cffi; "
                      "install sounddevice/cffi into FLX4Bridge/.vendor") from exc


def default_kit():
    return (Path(__file__).resolve().parents[2] / "akai-sample-packs"
            / "Barbara_Mason_Yes_Im_Ready" / "VSL_16_PAD_KIT")


def load_wav_kit(path):
    root = Path(path)
    files = tuple(root / f"pad_{pad:02d}.wav" for pad in range(1, 17))
    if not root.is_dir():
        raise FileNotFoundError(f"16-chop kit missing: {root}")
    output = []
    for file in files:
        if not file.is_file():
            raise FileNotFoundError(f"Missing sampler slice: {file}")
        with wave.open(str(file), "rb") as wav:
            channels, rate, width = wav.getnchannels(), wav.getframerate(), wav.getsampwidth()
            if channels != 2 or rate != 44100 or width != 2 or wav.getcomptype() != "NONE":
                raise ValueError(f"Expected stereo 16-bit 44.1kHz uncompressed PCM WAV: {file}")
            data = wav.readframes(wav.getnframes())
        frames = np.frombuffer(data, dtype="<i2").reshape(-1, 2).astype("float32")
        frames *= (1.0 / 32768.0)
        if not len(frames):
            raise ValueError(f"Audio file is empty: {file}")
        output.append(frames)
    return tuple(output)


class DirectFLX4Audio:
    def __init__(self, kit_path=None, device_name="DDJ-FLX4", master_gain=0.85,
                 audio_backend=sd, samples=None):
        self.samples = tuple(samples) if samples is not None else load_wav_kit(kit_path or default_kit())
        if len(self.samples) != 16:
            raise ValueError("Direct sampler requires exactly 16 distinct pad WAV files")
        self.sd = audio_backend
        self.device_name = device_name
        self.master_gain = float(master_gain)
        if not 0 < self.master_gain <= 1:
            raise ValueError("master gain must be in (0,1]")
        self.requests = queue.SimpleQueue()
        self.voices = []
        self.stream = None
        self.device_index = None
        self.device_label = None
        self.audio_callbacks = 0
        self.last_peak = 0.0
        self.highest_peak = 0.0
        self.last_audible_at = None
        self.audible_blocks = 0
        self.last_error = None
        self.triggered = 0
        self.last_pad = None

    def start(self):
        if self.stream is not None:
            return
        candidates = [(i, dev) for i, dev in enumerate(self.sd.query_devices())
                      if self.device_name.casefold() in str(dev.get("name", "")).casefold()
                      and dev.get("max_output_channels", 0) >= 4]
        if len(candidates) != 1:
            raise RuntimeError(f"Expected exactly one 4-channel {self.device_name} audio output; "
                               f"found {len(candidates)}")
        self.device_index, device = candidates[0]
        self.device_label = str(device["name"])
        self.sd.check_output_settings(device=self.device_index, channels=4,
                                      samplerate=44100, dtype="float32")
        self.stream = self.sd.OutputStream(device=self.device_index, samplerate=44100,
                                           channels=4, blocksize=512, dtype="float32",
                                           callback=self._callback)
        try:
            self.stream.start()
        except Exception:
            self.stream.close()
            self.stream = None
            raise

    def trigger(self, pad, velocity=127):
        if type(pad) is not int or not 1 <= pad <= 16:
            raise ValueError("pad must be 1 to 16")
        if type(velocity) is not int or not 1 <= velocity <= 127:
            raise ValueError("velocity must be 1 to 127")
        self.requests.put(("trigger", pad - 1, velocity / 127.0))
        self.triggered += 1
        self.last_pad = pad

    def stop_all(self):
        self.requests.put(("stop", -1, 0.0))

    def status(self):
        return {"ready": bool(self.stream is not None and self.stream.active),
                "device": self.device_label, "channels": "MASTER L/R (1-2)",
                "sample_rate": 44100, "pad_count": len(self.samples),
                "audio_callbacks": self.audio_callbacks, "active_voices": len(self.voices),
                "triggered": self.triggered, "last_pad": self.last_pad,
                "last_peak": round(self.last_peak, 4),
                "highest_peak": round(self.highest_peak, 4),
                "last_audible_at": self.last_audible_at,
                "audible_blocks": self.audible_blocks,
                "last_error": self.last_error}

    def _callback(self, outdata, frames, _time, status):
        outdata.fill(0.0)
        self.audio_callbacks += 1
        if status:
            self.last_error = str(status)[:160]
        while True:
            try:
                action, index, velocity = self.requests.get_nowait()
            except queue.Empty:
                break
            if action == "stop":
                self.voices.clear()
            elif action == "trigger":
                # Independent voices: left and right deck chops can overlap.
                # Limit to 32 voices to bound callback work in a pad mash.
                if len(self.voices) >= 32:
                    self.voices.pop(0)
                self.voices.append([self.samples[index], 0, velocity])

        # Mix into the MASTER stereo pair and leave 3/4 available for cue.
        master = outdata[:, :2]
        alive = []
        for sample, position, velocity in self.voices:
            count = min(frames, len(sample) - position)
            if count > 0:
                master[:count] += sample[position:position + count] * velocity
                position += count
            if position < len(sample):
                alive.append([sample, position, velocity])
        self.voices = alive
        master *= self.master_gain
        # Soft peak limiting avoids hard clipping when both decks play at once.
        np.tanh(master, out=master)
        self.last_peak = float(np.max(np.abs(master))) if len(master) else 0.0
        if self.last_peak > 0.002:
            self.highest_peak = max(self.highest_peak, self.last_peak)
            self.audible_blocks += 1
            self.last_audible_at = time.time()

    def close(self):
        if self.stream is not None:
            self.stop_all()
            self.stream.stop()
            self.stream.close()
            self.stream = None
