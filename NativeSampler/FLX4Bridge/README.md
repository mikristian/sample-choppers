# DDJ-FLX4 PAD FX → Ableton Simpler / separate sampler engines

Uses the DDJ-FLX4's documented **PAD FX 1, PAD FX 2, or SAMPLER** performance
pad messages. Press **PAD FX** on the physical FLX4 to trigger sampler chops;
Deck 1 pads play notes 36–43 (chops 1–8), and Deck 2 plays notes 44–51
(chops 9–16), in **Ableton Live Simpler** or the independent sampler.

Double-click **Launch FLX4 Vintage Sampler.command** to run the bridge, open
Vintage Sampler Lab, and import the bundled 16-chop song kit. Each launch
copies the existing standalone sampler settings to a timestamped backup first.
This launcher uses the already configured Rekordbox Python environment and
macOS Accessibility automation for the native file chooser. macOS may ask
you to grant Terminal permission to control the sampler.

Install using the Rekordbox bridge's existing Python environment where
`mido` and `python-rtmidi` are already available, or install them in a dedicated
virtual environment:

```
python3 -m venv .venv
.venv/bin/python -m pip install mido python-rtmidi
.venv/bin/python FLX4Bridge/flx4_sampler_bridge.py --list
.venv/bin/python FLX4Bridge/flx4_sampler_bridge.py
```

In the **Vintage Sampler Lab standalone** Audio/MIDI Settings, enable MIDI
input **FLX4 to Vintage Sampler Lab**, keep the desired audio output selected,
and switch the sampler mapping to **16 Pads** (not Keyboard). In an AU/VST3
host, route this virtual MIDI port into its sampler instrument track instead.
The output port exists only while the bridge is running.

## Music Hub mode and output routing

The old embedded Vintage Sampler panel was removed from Music Hub. To select
an output engine, double-click one of the standalone `Enable *.command`
launchers (copies are also in the **Barbara Mason – Chop Lab** Ableton project
folder). **Enable Ableton Simpler PAD FX.command** selects the `ableton`
engine; **Enable Direct FLX4 Sampler.command** restores the independent
44.1-kHz stereo sample mixer to DDJ-FLX4 MASTER. Vintage Sampler Lab MIDI
remains available as another engine through the local backend API.

For Ableton Simpler mode, the bridge creates a virtual CoreMIDI source named
`FLX4 PAD FX to Ableton`. In Live Preferences → Link, Tempo & MIDI, enable
**Track input** for that source. On the dedicated `07 | BARBARA 16 SLICES –
FLX4` MIDI track, set **MIDI From** to that input and **Monitor = In**, or
**Auto + Arm** the track. Put the full audio in Simpler, Slice → **Region** →
**16 Regions**, **Trigger** and **Poly**. You can refine the 16 marker positions
manually later. Leave the full original audio arrangement untouched and save
the Live set. Use the DDJ-FLX4 audio output with MASTER L/R connected to
powered speakers.

On the DDJ-FLX4, press the physical **PAD FX** mode button
(PAD FX 1 or PAD FX 2) for each deck: left eight performance pads map to
chops 1–8, right eight map to chops 9–16. Physical **SAMPLER** mode also
works as an alternative. Both decks can trigger independent chops concurrently
(1–8 on the left and 9–16 on the right). Play/Load/Browse remain dedicated to DJ operation.

The bridge checks Music Hub's persisted mode and engine on loopback HTTP. In
**Deck** mode it ignores sampler-pad MIDI; in **Sampler + Ableton** it sends
one MIDI note on channel 1 to `FLX4 PAD FX to Ableton` without creating a
second audio voice; in **Sampler + Direct** it mixes
the 16 WAVs as independent one-shots to the FLX4 MASTER pair (channels 1–2),
leaving headphone channels 3–4 quiet; in **Sampler + VSL** it sends notes
36–51 only to Vintage Sampler Lab; in **Sampler + Browser** it notifies Music
Hub to play its local WAV preview. It never routes a physical press to two
different audio engines at once. If Music
Hub's mode service is inaccessible, FLX4 sampler hardware input is disabled
until it reconnects (the separate localhost `/trigger` endpoint remains
available for explicit MIDI diagnostics).

In Vintage Sampler Lab's native **Options → Audio/MIDI Settings** enable only
MIDI input **FLX4 to Vintage Sampler Lab** (not raw `DDJ-FLX4`), and select
**DDJ-FLX4 / MASTER L + R** as the audio output, 44100 Hz. The Mac currently
recognizes the FLX4 as its default output. Confirm that speakers are connected
to the FLX4 RCA MASTER OUT and powered on; a software audio route cannot prove
external speaker sound without a physical listening check. The native bridge
exposes live LEFT and RIGHT hardware pad counters at
`http://127.0.0.1:18778/status`. Physical MPC Sample
hardware is a different device and must be connected over USB/microSD before
its internal pads/SD card can be accessed.

Music Hub can trigger the same pads via the loopback endpoint while the bridge
is running: `POST http://127.0.0.1:18778/trigger` with JSON
`{"action":"trigger","pad":1,"velocity":127}`. Pads are 1–16; `stop`
is also accepted as an action. `GET /status` checks the bridge. Only local
requests are accepted, and no cross-origin browser access is enabled; Music
Hub's local server should proxy calls. `--http-port 0` disables this endpoint.

This MIDI bridge passively listens to the controller and forwards only
unshifted PAD FX 1 / PAD FX 2 / SAMPLER performance-pad notes; it does not remap deck transport,
browser navigation, hot cues, FX, or the controller's output ports. A DJ app
such as Rekordbox may continue to act on the same hardware sampler notes, so
disable or adjust its own sampler pad actions if double playback is heard.
CoreMIDI must permit another listener alongside Rekordbox; the physical test
must confirm this on the connected FLX4. This does not mix or route audio into
Rekordbox: route both applications to the desired speakers/output device.

Reference: [Pioneer DJ DDJ-FLX4 MIDI Message List](https://www.pioneerdj.com/-/media/pioneerdj/software-info/controller/ddj-flx4/ddj-flx4_midi_message_list_e1.pdf),
PAD FX 1 pads 0x10–0x17, SAMPLER pads 0x30–0x37, PAD FX 2 pads
0x50–0x57, Deck 1 MIDI channel 8 and Deck 2 MIDI channel 10 (one-based).
All three modes map to sampler notes 36–51. Note: if Rekordbox still uses its
own PAD FX actions, pressing a pad may also activate a Rekordbox effect. Turn
off/remap those Rekordbox PAD FX assignments if you want sampler-only sound.

Mapping tests: `python3 -m unittest discover -s FLX4Bridge -p 'test_*.py'`.

## Direct audio dependencies and recovery

The `FLX4Bridge/.vendor/` folder contains `sounddevice` (PortAudio) and `cffi`
for the existing Rekordbox Python 3.9 environment. It uses the already
installed NumPy. The actual Mac has been verified to support opening the FLX4
four-channel audio device at 44.1kHz. On a fresh Mac, reinstall these
packages to the same Python environment. Start the bridge using the existing
`Launch FLX4 Vintage Sampler.command` if it is not running. Do not start a
second overlapping bridge instance. The physical Akai MPC Sample hardware is
a separate device that has not been connected over USB yet.
