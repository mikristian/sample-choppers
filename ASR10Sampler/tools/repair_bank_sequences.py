#!/usr/bin/env python3
"""Repair oversized original exercise patterns to the ASR-10's 16-step format.

Only touches banks with 17-32 already-valid MIDI-note steps. A byte-for-byte
backup is saved alongside each edited manifest. Audio files are untouched.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folder", type=Path)
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    repaired = 0
    for filename in sorted(args.folder.glob("*.json")):
        source = filename.read_bytes()
        data = json.loads(source)
        sequence = data.get("sequence")
        if not isinstance(sequence, list) or len(sequence) <= 16:
            continue
        if len(sequence) > 32 or any(
            not isinstance(step, list)
            or len(step) > 16
            or any(type(note) is not int or note < 0 or note > 127 for note in step)
            for step in sequence
        ):
            raise ValueError(f"Unrecognized pattern in {filename.name}")
        print(f"{'FIX' if args.apply else 'WOULD FIX'} {filename.name}: {len(sequence)} -> 16 steps")
        if args.apply:
            backup = filename.with_name(filename.name + ".before-16step.bak")
            if not backup.exists():
                backup.write_bytes(source)
            data["sequence"] = sequence[:16]
            tmp = filename.with_name(filename.name + ".tmp")
            try:
                tmp.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
                os.replace(tmp, filename)
            finally:
                if tmp.exists():
                    tmp.unlink()
        repaired += 1
    print(f"{repaired} manifests {'corrected' if args.apply else 'identified'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
