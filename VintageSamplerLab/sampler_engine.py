"""Small clean-room vintage-flavoured sample chopper.

This is *not* firmware/hardware emulation or a verified bit-exact reproduction.
It implements reusable audio operations and pad-kit export using only Python's
standard library. Authored source WAV is never modified.
"""

from array import array
from dataclasses import asdict, dataclass
import json
import math
from pathlib import Path
import sys
import wave


@dataclass(frozen=True)
class Profile:
    name: str
    samplerate: int
    bits: int
    filter_cutoff: int
    drive: float = 1.0
    description: str = ''


PROFILES = {
    'MPC2000XL-inspired': Profile('MPC2000XL-inspired', 44100, 16, 18000, 1, 'Clean 16-bit sampling and 16-pad chop workflow.'),
    'ASR10-inspired': Profile('ASR10-inspired', 44100, 16, 17000, 1, 'Keyboard-sampler repitching workflow; no proprietary ASR-10 effects or firmware.'),
    'SP1200-inspired': Profile('SP1200-inspired', 26040, 12, 7700, 1.06, '12-bit lower-rate texture; approximation, not converter emulation.'),
    'S950-inspired': Profile('S950-inspired', 12000, 12, 4700, 1.04, '12-bit sample-rate/filter character; approximation, not original S950 circuits.'),
    'SP404-inspired': Profile('SP404-inspired', 44100, 16, 16000, 1, 'Pad-based resampling with simplified softened output; no Roland firmware/effects.'),
}


def probe(path):
    with wave.open(str(path), 'rb') as w:
        if w.getcomptype() != 'NONE' or w.getsampwidth() != 2:
            raise ValueError('Please provide an uncompressed 16-bit PCM WAV (export from Ableton first).')
        return {'rate': w.getframerate(), 'channels': w.getnchannels(),
                'duration': round(w.getnframes() / w.getframerate(), 3)}


def read_excerpt(path, start_seconds=0, duration_seconds=8):
    path = Path(path)
    with wave.open(str(path), 'rb') as w:
        if w.getcomptype() != 'NONE' or w.getsampwidth() != 2:
            raise ValueError('Unsupported input: supply 16-bit PCM WAV. Convert MP3/24-bit WAV through Ableton.')
        if not 1 <= w.getnchannels() <= 2:
            raise ValueError('Only mono/stereo input is supported.')
        rate = w.getframerate()
        if not 0 <= start_seconds < w.getnframes() / rate:
            raise ValueError('Start time lies outside the sample.')
        if not 0 < duration_seconds <= 32:
            raise ValueError('Choose an excerpt between 0 and 32 seconds long.')
        start = int(start_seconds * rate)
        total = min(int(duration_seconds * rate), w.getnframes() - start)
        w.setpos(start)
        pcm = array('h')
        pcm.frombytes(w.readframes(total))
        if sys.byteorder != 'little':
            pcm.byteswap()
        if w.getnchannels() == 2:
            samples = [(pcm[i] + pcm[i+1]) / 65536.0 for i in range(0, len(pcm), 2)]
        else:
            samples = [v / 32768.0 for v in pcm]
        return samples, rate


def linear_sample(samples, position):
    if not samples or position < 0 or position >= len(samples):
        return 0.0
    a = int(position)
    if a + 1 >= len(samples):
        return samples[a]
    t = position - a
    return samples[a] * (1 - t) + samples[a+1] * t


def lowpass(samples, rate, cutoff):
    cutoff = min(float(cutoff), rate * 0.42)
    alpha = 1 - math.exp(-2 * math.pi * cutoff / rate)
    filtered = []
    y = 0.0
    for x in samples:
        y += alpha * (x - y)
        filtered.append(y)
    return filtered


def process(samples, rate, profile_name='MPC2000XL-inspired', semitones=0, out_rate=44100):
    """Render varispeed, prefilter, quantize and playback at target rate.

    The crude converter/filter approximations deliberately avoid claiming
    bit-accurate hardware behavior. For source fidelity, keep the original WAV.
    """
    if not samples:
        raise ValueError('Empty sample')
    if profile_name not in PROFILES:
        raise ValueError('Unknown profile')
    if not -36 <= semitones <= 36:
        raise ValueError('Pitch outside the supported range')
    profile = PROFILES[profile_name]
    # Anti-alias the input before changing its stored sample rate.
    conditioned = lowpass(samples, rate, min(profile.filter_cutoff, profile.samplerate * 0.40))
    n = max(1, round(len(samples) * profile.samplerate / rate))
    captured = [linear_sample(conditioned, i * rate / profile.samplerate) for i in range(n)]
    max_code = (1 << (profile.bits-1)) - 1
    captured = [max(-1.0, min(1.0, v * profile.drive)) for v in captured]
    captured = [round(v * max_code) / max_code for v in captured]
    speed = 2 ** (float(semitones) / 12)
    out_frames = max(1, int(round(len(captured) / profile.samplerate * out_rate / speed)))
    out = [linear_sample(captured, i * profile.samplerate * speed / out_rate) for i in range(out_frames)]
    # A short boundary fade reduces clicks without changing the total timing.
    fade = min(len(out)//2, max(1, round(out_rate * 0.002)))
    for i in range(fade):
        factor = i / max(1, fade)
        out[i] *= factor
        out[-i-1] *= factor
    return out


def save_wav(path, samples, rate=44100):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    pcm = array('h', (int(max(-1., min(.999969, v)) * 32767) for v in samples))
    if sys.byteorder != 'little':
        pcm.byteswap()
    with wave.open(str(path), 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm.tobytes())
    return path


def export_kit(source, destination, profile_name, semitones=0, start_seconds=0,
               duration_seconds=8, pad_count=16):
    if pad_count not in (8, 16):
        raise ValueError('Pad count must be 8 or 16')
    samples, rate = read_excerpt(source, start_seconds, duration_seconds)
    if len(samples) < pad_count:
        raise ValueError('Excerpt is too short to split into this many pads.')
    dest = Path(destination)
    dest.mkdir(parents=True, exist_ok=True)
    pads = []
    for index in range(pad_count):
        lo = (index * len(samples)) // pad_count
        hi = ((index + 1) * len(samples)) // pad_count
        result = process(samples[lo:hi], rate, profile_name, semitones)
        filename = f'pad_{index+1:02d}.wav'
        save_wav(dest / filename, result)
        pads.append({'pad':index+1, 'midi_note':36+index, 'file':filename,
                     'startSeconds':round(start_seconds+lo/rate,5),
                     'endSeconds':round(start_seconds+hi/rate,5)})
    output = {'format':'VintageSamplerLab-Kit-1','description':'Modeled styles; not hardware/firmware emulations',
              'sourceFilename':Path(source).name,'profile':asdict(PROFILES[profile_name]),
              'sourceSampleRate':rate,'pitchSemitones':semitones,'padCount':pad_count,
              'outputSampleRate':44100,'pads':pads}
    (dest / 'kit.json').write_text(json.dumps(output,indent=2)+'\n')
    return output
