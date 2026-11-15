# Publication verification (2026-11-14)

Source content scan: passed. No private sample banks, recorded audio, copyrighted material, binary apps or absolute personal paths.

Local checks: `python3 -m unittest discover -s VintageSamplerLab -p 'test_*.py'` **passed (14/14)**.

Known blockers: JUCE 8 must be installed separately and supplied as `JUCE_ROOT` to configure NativeSampler and ASR10Sampler; CMake and an Apple development toolchain are needed. Mac/DAW plugin builds and host verification were not performed. Optional native plugin artwork is excluded. Public sample bank functionality requires a properly licensed user-provided bank. Integration tests tied to private audio files were omitted.

## CHOPROOM extension (2026-11-14)

CHOPROOM was added as a reviewed source-only subdirectory. Its private song restore shortcut and bundled demo menu were removed in the staged copy. Browser tests that required private sessions were replaced by equivalent synthetic fixtures. The application requires user-provided audio.

**Passed:** CHOPROOM JavaScript syntax, slicing, arrangement, marker, viewport, export, independent variants and collection tests; CHOPROOM device bridge Python compilation. Generated test ZIP files are ignored and untracked.

**Not verified:** Full browser/device interoperability, compatibility with the private live runtime, or physical pad routing. Standalone auto-chopping workflows are outside the scope of this collection.
