# ASR-10 Sampler Lab

A standalone macOS / AU / VST3 sampler with a detailed front-panel recreation inspired by the black 1992 Ensoniq ASR-10 Advanced Sampling Recorder.

This is a separate clean-room instrument built with JUCE. The historical layout is a usability and visual reference. The synthesizer and effects use original DSP; no Ensoniq operating system, ROM, proprietary algorithm, sample library, or disk image is included. It does **not** boot or emulate original ASR-10 firmware.

## Build on this Mac

The pinned local JUCE 8 checkout is autodetected relative to this workspace. Configure and compile from this directory:

```sh
python3 -m cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
python3 -m cmake --build build --target ASR10SamplerLab_Standalone -j 4
```

Optional AU and VST3 targets:

```sh
python3 -m cmake --build build --target ASR10SamplerLab_AU ASR10SamplerLab_VST3 -j 4
```

The standalone app is generated under `build/ASR10SamplerLab_artefacts/Release/Standalone/`. AU/VST3 artifacts are generated beside it, and are not automatically installed.

To open the Mac app, double-click `Launch ASR-10 Sampler Lab.command` in this project folder or run:

```sh
open "build/ASR10SamplerLab_artefacts/Release/Standalone/ASR-10 Sampler Lab.app"
```

The current macOS screenshot is saved in `assets/ASR10SamplerLab-preview.png`, and the original app icon in `assets/ASR10SamplerLab.png` / `.icns`.

To run the optional offline processor test (sample import, audio output, save/reopen):

```sh
python3 -m cmake -S . -B build -DASR10_BUILD_TESTS=ON
python3 -m cmake --build build --target ASR10Smoke -j 2
build/ASR10Smoke
```

## User workflow

The 61-key front panel accepts mouse and computer-keyboard playing and external MIDI note messages. Use one of eight instrument buttons to select a sound, load a WAV or AIFF, adjust the root note, loop and envelope settings, and play melodically. Mode, page, data-entry, and transport controls are presented in the physical arrangement of the 1992 keyboard. Host projects save parameter settings and sample file paths.

The Record transport button records **note events in a basic 16-step sequence**, not microphone audio input. Audio track selectors, the drawn floppy drive, and modulation wheel artwork are currently interface representations rather than full hardware-equivalent routing or disk functions. A generated tonal sound is available on instrument 1 to test the keyboard immediately.

For the private Kanye-era practice library, including an expanding set of
independently arranged **BANK** presets, original 16-step practice beats, and the new
sixteen-key **CHOP** workflow, see
[`KANYE_STYLE_SAMPLE_BANKS.md`](KANYE_STYLE_SAMPLE_BANKS.md).

For faster navigation click **BANKS**, choose **REAL SONGS / 106 TRACKS**
(alphabetical A–F, G–L, M–R, S–Z), or one of the three practice-bank
submenus. Press **LOAD** followed by the front-panel left/right cursor to
move to the next/previous bank; switching from a real-song session stays
among the real songs. The computer shortcuts are **Command-B** for the
popup and **Option-Left / Option-Right** for adjacent banks.
In **CHOP** mode the sixteen live trigger keys are numbered and the last
triggered slice lights up. Press **WAVE** on the front-panel parameter keypad,
or **Option-W**, to see the selected sample's LCD waveform with trim and slice
boundaries. Drag the start or end boundary directly on the LCD waveform to
isolate the section you want to chop; **Shift-click** the waveform to move
its end boundary. Hold **Option** while dragging a vertical interior chop
marker to move the boundary between its adjacent slices. Both the per-slot
trim and all sixteen note cuts persist in the host project. The standalone
app remembers the last selected bank in `~/Library/ASR10SamplerLab/last-bank.txt`;
AU/VST3 project restoration uses the host's saved state.

### Load and chop your real music

1. Open **BANKS → REAL SONGS** to choose from **106 existing local-song
   study banks**, including *Bully*, *Good Morning*, *Good Life*, *Bound 2*,
   *Preacher Man* and *Can't Tell Me Nothing*. To add another recording,
   choose **BANKS → IMPORT MY SONG AUDIO…** or press **Command-I**, then
   select a local WAV, AIFF, FLAC or MP3 you have permission to use.
   The app stores only JSON instructions; all audio stays at its original path.
2. Track **1** loads the full song across sixteen playable keyboard chops,
   keys **C3–D#4**. Tracks **2–8** divide it into seven consecutive phrase
   regions, each with sixteen individual chops. Select a track then press
   **WAVE** to see its waveform. Use **Option-drag** to move a chop boundary,
   ordinary dragging to change trim, and **REC / PLAY** to make a new phrase.
3. Select **BANKS → SAVE MY SONG CHOP EDITS** or press **Command-Shift-S**
   to save your edited marker coordinates, track settings, and trim to the
   corresponding `owned-*.json` on Expansion. Existing factory/royalty-free
   banks are protected from this save action.
4. For a complete album folder of supported local tracks, use
   [`tools/import_owned_album.py`](tools/import_owned_album.py); see
   [`OWNED_SONG_IMPORT.md`](OWNED_SONG_IMPORT.md) for the CLI and
   [`REAL_SONG_QUEUE.md`](REAL_SONG_QUEUE.md) for the Bully/classics queue.
   Streaming subscription `.m4p` files cannot be imported by this sampler.

The original [Akademiks-stream sample chopping video](Reference%20Videos/YE%20Chopping%20Sample%20-%20Akademiks%20Stream.webloc)
is bookmarked in [`Reference Videos/`](Reference%20Videos/). Its historical
exact source timeline and keyboard chops are not verified by this project.

The private Expansion library currently includes **139 indexed banks:
106 real-song study banks (105 from the existing DJ music collection and one
from the Mac Music library) plus 33 original practice banks**. The
independently sourced practice collection also has **25 sample packs and
8,512 playable audio files**.
Publisher provenance, rights terms and checksums for the latest six packs are
documented in [`EXTRA_PACKS.md`](EXTRA_PACKS.md). Commercial study songs are
existing local recordings referenced by absolute path; none is embedded in
the app or plugin bundles. Song banks begin with simple evenly spaced
practice chops and do **not** claim to replicate the artist's original
chopping, sample sources, or timing. See
[`REAL_SONG_QA_20261107.md`](REAL_SONG_QA_20261107.md) for verification.

### Historical references

- Ensoniq ASR-10 Musician's Manual (1992), section 1: front panel controls and architecture: https://www.samplekings.com/pdfs/ENSONIQ_ASR10%20User%20Manual.pdf
- High-resolution front-panel photography: https://evolutionmusicstore.com/products/ensoniq-asr-10-sampler

Historical ASR-10 features such as its original 24-bit effects algorithms, disk image loading, full wavesample layering and hardware timing fidelity require substantially more emulation work. The current app should be evaluated as a modern playable sampler with a closely studied hardware interface.
