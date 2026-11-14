#!/usr/bin/env python3
"""Build offline JUCE audio tests using existing compiled project objects.

For the current low-space Mac: reuses CMake's compile flags and plugin object
files without generating the entire JUCE toolchain again. A normal CMake build
remains the supported option after sufficient free disk space is restored.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import re
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", nargs="?", help="Optional ASR-10 bank JSON to run against")
    parser.add_argument("--source", default="tests/test_smoke.cpp",
                        help="Targeted test source relative to project directory")
    args = parser.parse_args()
    flags_text = (BUILD / "CMakeFiles/ASR10SamplerLab.dir/flags.make").read_text()

    def part(name: str) -> list[str]:
        match = re.search(r"^" + re.escape(name) + r" = (.*)$", flags_text, re.M)
        if not match:
            parser.error(f"missing generated compile options: {name}")
        return shlex.split(match.group(1))

    binary = BUILD / "ASR10SmokeManual"
    test_object = BUILD / "ASR10SmokeManual.o"
    source = ROOT / args.source
    if not source.is_file() or ROOT not in source.resolve().parents:
        parser.error("test source must exist inside the ASR10Sampler project")
    build_command = [
        "/usr/bin/c++",
        *part("CXX_DEFINES"),
        *part("CXX_INCLUDES"),
        *part("CXX_FLAGS"),
        "-c",
        str(source),
        "-o",
        str(test_object),
    ]
    print("COMPILE offline sampler tests", flush=True)
    subprocess.run(build_command, cwd=BUILD, check=True)

    archive = BUILD / "CMakeFiles/ASR10SamplerLab.dir/link.txt"
    archive_line = shlex.split(archive.read_text().splitlines()[0])
    shared = [arg for arg in archive_line if arg.endswith(".o")]
    for arg in shared:
        if not (BUILD / arg).is_file():
            parser.error(f"missing precompiled JUCE object: {arg}")

    original = shlex.split(
        (BUILD / "CMakeFiles/ASR10SamplerLab_Standalone.dir/link.txt").read_text()
    )
    first_client_object = next(i for i, arg in enumerate(original) if arg.endswith(".o"))
    library = next(
        i for i, arg in enumerate(original) if arg.endswith("_SharedCode.a")
    )
    link_command = [
        *original[:first_client_object],
        str(test_object),
        *shared,
        "-o",
        str(binary),
        *original[library + 1 :],
    ]
    print("LINK standalone smoke test", flush=True)
    subprocess.run(link_command, cwd=BUILD, check=True)

    run = [str(binary)]
    if args.manifest:
        run.append(str(Path(args.manifest).expanduser().resolve()))
    print("TEST", *run[1:], flush=True)
    subprocess.run(run, cwd=BUILD, check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
