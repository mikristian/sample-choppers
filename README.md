# Sampler Projects — source publication candidate

Source trees for `NativeSampler/` (JUCE-based Vintage Sampler Lab), `ASR10Sampler/` (JUCE-based ASR-10 Sampler Lab) and `VintageSamplerLab/` (legacy Python sequencer/sampler).

The collection also includes **`CHOPROOM/`** for local browser chopping, pad banks, multitrack arrangements and exports. These tools are source-only, use your own audio, and do not include private demo recordings or samples. Each has its own setup and test README.

## Build and test
The two CMake projects require a separately installed JUCE 8 source checkout: `cmake -S NativeSampler -B /tmp/native-sampler-build -DJUCE_ROOT=/path/to/JUCE`; similarly for `ASR10Sampler`. Build requires the matching CMake/compiler/Apple audio toolchain. Legacy Python tests can be run from the VintageSamplerLab directory with Python 3 and its optional audio dependencies.

No user recordings, stems, generated banks, purchased samples, private songs, downloaded installer executables, screenshots, or native app binaries are included. Some import tools and optional sample-bank workflows have been omitted because they were tied to local storage. A complete sample-browsing experience requires appropriately licensed user-provided audio.

## Limits
UI builds using optional image resources need original redistributable replacements. Synthesizer and plugin validation requires JUCE and actual target DAW/host setup. No claim of full installed-app parity is made.

CHOPROOM offline verification: `cd CHOPROOM && node test_slicing.js && node test_arranger.js && node test_export.js`.

The independently maintained MusicREA auto-chopper is **not part of this repository**. Existing NativeSampler source retains an optional import format for compatible bank manifests; that compatibility code is not the MusicREA application or its analysis engine.
