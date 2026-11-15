# Sampler Projects

A collection of source experiments for music sampling, slicing, sequencing and plugin development. No audio samples or finished plugin binaries are bundled; bring recordings that you own or are licensed to use.

## Projects and architecture

| Folder | Technology | Scope |
| --- | --- | --- |
| [`CHOPROOM/`](CHOPROOM/) | Browser HTML, CSS and JavaScript; optional Python device bridge | Source audio editor, slicing markers, pad banks, arrangements, local session tools and export logic. |
| [`NativeSampler/`](NativeSampler/) | JUCE 8, CMake, C++ | Native sampler engine, plugin interface, effects, MIDI/FLX4 bridge experiments and engine tests. |
| [`ASR10Sampler/`](ASR10Sampler/) | JUCE 8, CMake, C++ | ASR-style instrument/plugin experiments including settings and bank/clip test suites. |
| [`VintageSamplerLab/`](VintageSamplerLab/) | Python | Legacy sampler, pad sequencer, keyzones and UI experiments. |

Each component is independent. There is no unified installer or evidence that all source trees reproduce locally installed applications.

## Run the browser editor

```sh
cd CHOPROOM
python3 -m http.server 8080
# http://127.0.0.1:8080/
```

Choose your own browser-supported audio; the source release has no demonstration songs. See [CHOPROOM's README](CHOPROOM/README.md) for details about slicing, arrangements and export limitations.

## Build native projects

JUCE 8 must be available separately along with CMake and a compatible C++/platform audio toolchain. Set `JUCE_ROOT` to your own JUCE source checkout:

```sh
cmake -S NativeSampler -B /tmp/native-sampler-build -DJUCE_ROOT=/path/to/JUCE
cmake --build /tmp/native-sampler-build
cmake -S ASR10Sampler -B /tmp/asr10-sampler-build -DJUCE_ROOT=/path/to/JUCE
cmake --build /tmp/asr10-sampler-build
```

Native plugin/AU/VST and real host tests need the target DAW, audio devices and platform SDKs. Project-specific requirements are in the subfolder READMEs and CMake files.

## Tests

```sh
python3 -m unittest discover -s VintageSamplerLab -p 'test_*.py'
cd CHOPROOM
node test_slicing.js
node test_arranger.js
node test_export.js
node test_marker_controls.js
node test_track_view.js
node test_chop_variants.js
node test_chop_collection.js
```

See [STATUS.md](STATUS.md) for the audited test outcomes. Native C++ builds have not been verified in this source-only staging environment.

## Release scope

No personal recordings, copyrighted music, sample libraries, audio stems, artwork with uncertain redistribution rights, cached render files, or compiled apps are included. Some optional features that depended on a particular local file layout have been omitted. Users must supply legally usable audio and their own environment configuration.

See [third-party notices](THIRD_PARTY_NOTICES.md) for dependencies and attribution.

## UI preview

![CHOPROOM source editor with no audio loaded](docs/screenshots/choproom-empty-state.png)

*Actual browser capture of the staged CHOPROOM empty state, 2026-11-14. No demo audio, user samples or hardware are present. This image confirms the UI renders, not that browser audio export or external devices have passed integration testing.*

### CHOPROOM editor and pad workspace

![CHOPROOM source editor with waveform workspace and slicing controls](docs/screenshots/choproom-workspace.png)

![CHOPROOM source editor including the lower pad workspace](docs/screenshots/choproom-pads-workspace.png)

Both additional images are actual November 14, 2026 headless Chrome screenshots of the staged `CHOPROOM/index.html` running without loaded audio. The 16-pad bank and slicing panels are visible in their empty state. These do not demonstrate successful audio rendering, device communication or export.

### Native sampler, ASR and vintage interfaces

`NativeSampler` and `ASR10Sampler` contain JUCE/C++ editor sources; `VintageSamplerLab` contains Python UI sources. A faithful screenshot of their actual boards requires building and launching each native interface with its proper runtime dependencies. No verified redistributable native-board capture was available during this pass, so this gallery does not substitute a fabricated board screenshot.
