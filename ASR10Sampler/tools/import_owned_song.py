#!/usr/bin/env python3
"""Create an ASR10 study bank referencing a user's existing local recording."""
from __future__ import annotations

import argparse
import aifc
import json
import math
from pathlib import Path
import shutil
import subprocess
import sys
import wave


DIRECT = {'.wav', '.aif', '.aiff', '.flac', '.mp3'}
ACCEPTED = DIRECT | {'.m4a'}


def duration_seconds(source: Path) -> float:
    suffix = source.suffix.lower()
    if suffix == '.wav':
        with wave.open(str(source), 'rb') as audio:
            return audio.getnframes() / audio.getframerate()
    if suffix in {'.aif', '.aiff'}:
        with aifc.open(str(source), 'rb') as audio:
            return audio.getnframes() / audio.getframerate()
    probe = shutil.which('ffprobe')
    if probe:
        cmd = [probe, '-v', 'error', '-show_entries', 'format=duration', '-of', 'default=nokey=1:noprint_wrappers=1', str(source)]
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=35, check=False)
        if result.returncode == 0:
            return float(result.stdout.strip())
    # macOS ships afinfo; its format is less stable, so use only a strict match.
    afinfo = shutil.which('afinfo')
    if afinfo:
        import re
        result = subprocess.run([afinfo, str(source)], capture_output=True, text=True, timeout=35, check=False)
        match = re.search(r'(?m)^\s*estimated duration:\s*([0-9.]+)\s*sec', result.stdout)
        if result.returncode == 0 and match:
            return float(match.group(1))
    raise ValueError('Cannot measure duration; install ffmpeg (ffprobe) or use WAV/AIFF')


def marker_times(raw: str | None, duration: float) -> list[float]:
    if raw is None:
        return [duration * i / 16 for i in range(17)]
    try:
        values = [float(item.strip()) for item in raw.split(',')]
    except ValueError as exc:
        raise ValueError('--chop-points must contain comma-separated seconds') from exc
    if len(values) != 17 or any(not math.isfinite(v) for v in values):
        raise ValueError('--chop-points must contain exactly 17 finite times')
    if abs(values[0]) > 0.00001 or abs(values[-1] - duration) > 0.01:
        raise ValueError('chop boundaries must begin at 0 and end at the song duration')
    if any(b <= a for a, b in zip(values, values[1:])):
        raise ValueError('chop boundaries must increase strictly')
    return values


def convert_audio(source: Path, destination: Path) -> Path:
    if destination.suffix.lower() != '.wav':
        raise ValueError('--converted-wav must end with .wav')
    if not any('expansion' in part.lower() for part in destination.parts):
        raise ValueError('conversion output must be explicitly located under an Expansion volume/folder')
    if destination.exists():
        raise FileExistsError(f'Conversion output already exists: {destination}')
    if destination == source:
        raise ValueError('Conversion output must differ from source')
    destination.parent.mkdir(parents=True, exist_ok=True)
    ffmpeg = shutil.which('ffmpeg')
    afconvert = shutil.which('afconvert')
    if ffmpeg:
        cmd = [ffmpeg, '-nostdin', '-hide_banner', '-loglevel', 'error', '-n', '-i', str(source), '-vn', '-ac', '2', '-ar', '44100', '-c:a', 'pcm_s16le', str(destination)]
    elif afconvert:
        cmd = [afconvert, '-f', 'WAVE', '-d', 'LEI16@44100', str(source), str(destination)]
    else:
        raise RuntimeError('Install ffmpeg or use macOS afconvert to convert this format')
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=1800, check=False)
    if result.returncode != 0 or not destination.is_file():
        destination.unlink(missing_ok=True)
        raise RuntimeError(f'Audio conversion failed ({result.returncode}): {result.stderr[-500:]}')
    return destination


def build_bank(source: Path, title: str, tempo: float, sections: list[str], points: list[float], duration: float) -> dict:
    normalized = [round(p / duration, 9) for p in points]
    # Slot zero is the full-song keyboard with the exact global 16 cuts.
    # The other seven slots are independent views of contiguous seventh-regions.
    local_equal = [i / 16 for i in range(17)]
    instruments = []
    for i in range(8):
        start = 0.0 if i == 0 else (i - 1) / 7
        end = 1.0 if i == 0 else i / 7
        instruments.append({
            'name': sections[i] if i < len(sections) else ('Full Song' if i == 0 else f'Phrase Zone {i}'),
            'path': str(source), 'rootNote': 60, 'volume': 0.8,
            'sampleStart': round(start, 9), 'sampleEnd': round(end, 9),
            'chopPoints': normalized if i == 0 else local_equal,
        })
    return {
        'name': title, 'title': title, 'tempo': tempo, 'selectedInstrument': 0,
        'chop': True, 'sequence': [[] for _ in range(16)],
        'sequence_description': 'Empty 16-step study pattern; program your own performance',
        'sourceDurationSeconds': round(duration, 6),
        'chopPointsSeconds': [round(p, 6) for p in points],
        'chopPoints': normalized,
        'instruments': instruments,
        'note': 'References user-owned local audio only; no audio is embedded in this manifest.',
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--out-bank', required=True, type=Path)
    parser.add_argument('--title', required=True)
    parser.add_argument('--tempo', type=float, default=120)
    parser.add_argument('--sections', help='Up to eight comma-separated zone labels')
    parser.add_argument('--chop-points', help='Exactly 17 increasing times in seconds, including 0 and full duration')
    parser.add_argument('--converted-wav', type=Path, help='Explicit Expansion destination for PCM WAV conversion')
    args = parser.parse_args(argv)
    try:
        source = args.source.expanduser().resolve(strict=True)
        if not source.is_file() or source.suffix.lower() not in ACCEPTED:
            raise ValueError('Source must be an existing WAV, AIFF, FLAC, MP3 or M4A file')
        if source.stat().st_size < 48:
            raise ValueError('Source audio file is too small')
        if not args.title.strip() or not math.isfinite(args.tempo) or not 20 <= args.tempo <= 300:
            raise ValueError('Provide a non-empty title and a tempo between 20 and 300 BPM')
        out = args.out_bank.expanduser().resolve()
        if out.suffix.lower() != '.json' or out.exists():
            raise ValueError('Output must be a new .json manifest path')
        if args.converted_wav:
            source = convert_audio(source, args.converted_wav.expanduser().resolve())
        elif source.suffix.lower() not in DIRECT:
            raise ValueError('M4A requires --converted-wav under Expansion; direct JUCE decoding is not supported')
        duration = duration_seconds(source)
        if not math.isfinite(duration) or duration <= 0:
            raise ValueError('Invalid source duration')
        markers = marker_times(args.chop_points, duration)
        sections = [s.strip() for s in args.sections.split(',')] if args.sections else []
        if len(sections) > 8 or any(not s for s in sections):
            raise ValueError('--sections expects 1 to 8 non-empty comma-separated names')
        bank = build_bank(source, args.title.strip(), args.tempo, sections, markers, duration)
        out.parent.mkdir(parents=True, exist_ok=True)
        with out.open('x', encoding='utf-8') as handle:
            json.dump(bank, handle, indent=2, ensure_ascii=False)
            handle.write('\n')
        print(f'Created {out} (8 zones, 16 slices, source reference: {source})')
        return 0
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as exc:
        parser.exit(2, f'Import failed: {exc}\n')


if __name__ == '__main__':
    sys.exit(main())
