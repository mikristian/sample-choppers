# CHOPROOM (source distribution)

Local browser audio chopping, pad banks, manual markers, timeline arrangement, MIDI input previews and WAV/ZIP exports. No audio file is bundled. The private demo and saved-song restore shortcut were removed from this copy; use **Choose audio** and **Open session** with your own files instead.

For a quick local preview, serve this folder on loopback: `python3 -m http.server 49771 --bind 127.0.0.1` and open `http://127.0.0.1:49771/`. Optional app status helper: `python3 device_bridge.py`, which queries a separately running Music Hub on local ports. No physical controller connection is implied by an app status report.

Offline tests: `node test_slicing.js`, `node test_marker_controls.js`, `node test_track_view.js`, `node test_arranger.js`, `node test_export.js`, `node test_chop_collection.js`, `node test_chop_variants.js`. Some export tests generate ZIP test files locally; generated data are excluded by repository ignore rules.

Export is a new audio derivative, leaving source files intact. No original song, session, historical export, external sample, screenshots or standalone app bundle is included.
