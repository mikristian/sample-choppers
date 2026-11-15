# Publication verification (2026-11-14)

## MASCHINE LAB 2D / 3D follow-up (2026-11-14)

Located the separately installed **MASCHINE LAB 2D** native macOS sampler on Jarvis and a matching, complete **MASCHINE LAB 3D** browser source project. The 2D app is illustrated in `docs/screenshots/maschine-lab-native-2d-redacted.png`: a genuine window-only capture with loaded audio waveform/metadata and private song/kit names obscured. The 3D source and locally vendored MIT-licensed Three.js modules are now included under `MaschineLab3D/`, with two historical Chrome UI screenshots. These are not the previously included CHOPROOM or ASR-10 samplers.

**Verified:** `node --check MaschineLab3D/app.js` passed; a transient localhost HTTP test returned full nonempty documents for `index.html`, `app.js`, `styles.css` and all four bundled Three.js modules. The 3D Chrome screenshots are archived from a previous November 7 source QA session with built-in generated demo sounds. The independently installed 2D Mac app launched for a fresh capture. **Not claimed:** an independent clean CMake/JUCE rebuild or Ableton AU/VST3 host test for MASCHINE LAB 2D. Its existing native C++ source retains hardcoded local private review-bank and song-workspace integrations; no complete native 2D source tree, private music bank or binaries are added here. Full native source publication requires a rights-safe, standalone cleanup.

Source content scan: passed. No private sample banks, recorded audio, copyrighted material, binary apps or absolute personal paths.

Local checks: `python3 -m unittest discover -s VintageSamplerLab -p 'test_*.py'` **passed (14/14)**.

Known blockers: JUCE 8 must be installed separately and supplied as `JUCE_ROOT` to configure NativeSampler and ASR10Sampler; CMake and an Apple development toolchain are needed. Mac/DAW plugin builds and host verification were not performed. Optional native plugin artwork is excluded. Public sample bank functionality requires a properly licensed user-provided bank. Integration tests tied to private audio files were omitted.

## CHOPROOM extension (2026-11-14)

CHOPROOM was added as a reviewed source-only subdirectory. Its private song restore shortcut and bundled demo menu were removed in the staged copy. Browser tests that required private sessions were replaced by equivalent synthetic fixtures. The application requires user-provided audio.

**Passed:** CHOPROOM JavaScript syntax, slicing, arrangement, marker, viewport, export, independent variants and collection tests; CHOPROOM device bridge Python compilation. Generated test ZIP files are ignored and untracked.

**Not verified:** Full browser/device interoperability, compatibility with the private live runtime, or physical pad routing. Standalone auto-chopping workflows are outside the scope of this collection.
