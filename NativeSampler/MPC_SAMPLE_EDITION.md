# Vintage Sampler Lab — MPC Sample-inspired 2D edition

This is an original JUCE software instrument using the portable Akai Professional MPC Sample as a **visual and workflow reference**. It is not Akai firmware, a full MPC hardware emulator, or an official Akai product. It is developed separately from MASCHINE LAB 2D.

## Launch and build

The staged macOS standalone lives at:

`build-mpc-sample/VintageSamplerLab_artefacts/Release/Standalone/Vintage Sampler Lab.app`

The updated `~/Applications/Vintage Sampler Lab.app` is installed as **v0.3.2** with a cream-and-waveform icon. Double-click **Launch MPC Sample Edition.command** to open it. Previous v0.3.1 standalone, AU, VST3 and settings were backed up under `NativeSampler/backups/pad-led-motion-20261107/`.

Build sources remain in `Source/PluginEditor.cpp/.h`, `Source/PluginProcessor.cpp/.h`, and `Source/SamplerEngine.h`. The isolated CMake build directory is `build-mpc-sample/`. Formats compiled in this build: Standalone, AU, VST3.

## Controls currently available

- **SAMPLE:** trigger the 16 RGB pads. Double-click any pad, or click **SAMPLE SELECT**, to import audio. **IMPORT KIT** loads an existing 16-file `pad_01.wav`…`pad_16.wav` folder. Original source files remain unmodified.
- **LCD waveform:** choose a pad; adjust its own START and END using the first two knobs or drag its waveform handles. K3 controls pitch. Each sample displays its real precomputed waveform peaks.
- **CHOP:** choose a source pad, press CHOP and the **CHOP 16** soft key, then confirm. Sixteen slices share the same immutable source audio and preserve their own start/end ranges. This replaces the current pad assignments; save first if you want to retain a prior kit.
- **MUTE:** press MUTE and tap any pad to toggle its playback state.
- **16 LEVELS:** tap the button; pads 01–16 trigger the selected sample using ascending velocities.
- **SEQ:** view and edit a 16-step loop of the selected sound by tapping its illuminated steps. Use SAMPLE to choose a sound before returning to SEQ. **REC** begins live step recording while SAMPLE mode is active; **PLAY** and **STOP** control the loop. **TAP TEMPO** updates its rate and K1 adjusts BPM on SEQ screen. **ERASE** clears the pattern.
- **ENV** soft key adjusts attack, release and sample play length. **MIDI** soft key exposes keyboard mode, root note, loop and gate settings. Fader changes onscreen pad velocity.
- **PROJECT:** soft keys **SAVE** and **OPEN** read/write a local `.vsl` file holding original sample paths, pad labels, trims, mute states, tempo, steps and parameters. Original WAVs are referenced by path and not copied into the project.
- **NOTE REPEAT:** enable the button, then hold a pad for tempo-synced sixteenth notes.
- **BANKS A–H:** press PAD BANK to cycle or SHIFT followed by pads 1–8 to select any of eight independent 16-pad banks. Every bank retains its own sample assignments, mute states, trims and step patterns, with all banks stored in project state.
- **Moving drum-pad LEDs:** the physical-style matrix uses 88×88 square pads with 4-pixel gaps. Every playable pad has moving multicolor light streaks circling the perimeter, with brighter trails on press or playback. Only the 16 pads animate; mode, transport, soft-key, and utility buttons have neutral static borders. Lighting is visual feedback and does not affect audio processing.
- **PAD FX / KNOB FX:** both open an FX page with four serial master effect slots. K1 chooses the slot, K2 adjusts amount, K3 adjusts wet blend. The LCD soft keys cycle BYPASS, LOWPASS, HIGHPASS, DRIVE and BITCRUSH effects. These are original basic DSP effects.

## Current limits

The visual layout resembles the MPC Sample, but the following hardware functions are **not yet implemented** in this software: Akai's full factory library of 60 onboard FX; resampling; live microphone/line input sampling; time stretch/warp; swing/time correction; retrospective sequence recall; MPC3 interchange; and polyphonic pad aftertouch. Hardware battery, built-in speaker, physical MIDI/USB connectors and microSD support are platform features, not reproduced by a desktop UI. The implemented effects use four independent serial master slots with four original DSP types, rather than Akai's exact effect algorithms.

This prototype should not be described as fully feature-identical to the MPC Sample.

## Validation

- Native engine test: `clang++ -std=c++17 -O2 tests/test_engine.cpp -o build-mpc-sample/test_engine_mpc && build-mpc-sample/test_engine_mpc`
- GUI Standalone, AU and VST3 compiled successfully on macOS arm64 with JUCE 8.0.17.
- The v0.3.2 standalone was launched and visually checked; snapshots `build-mpc-sample/akai-v032-led-frame-a.png` and `akai-v032-led-frame-c.png` show changing drum-pad outlines while both regular button regions are pixel-identical between captures.
- Native macOS Audio Unit validation completed successfully: `auval -v aumu VSLb Mkrs` (audio rendering, parameter updates and MIDI).
- MASCHINE LAB v0.6.0 Audio Unit passed `auval -v aumu M2DL Mkrs` after its photo-based front-panel update.
