"""Synthetic original test cases for the playable 16-pad MIDI sequencer."""

from pathlib import Path
import tempfile
import unittest

from pad_sequencer import DEFAULT_NOTE,PAD_KEYS,PadPattern,vlq


class PadSequenceTests(unittest.TestCase):
    def test_four_by_four_key_mapping(self):
        self.assertEqual(len(PAD_KEYS),16)
        self.assertEqual(len(set(PAD_KEYS)),16)
        self.assertEqual(PAD_KEYS[0],'1')
        self.assertEqual(PAD_KEYS[-1],'v')

    def test_quantized_loop_recording_and_wrapping(self):
        pattern=PadPattern(120,2)
        self.assertEqual(pattern.steps,32)
        self.assertAlmostEqual(pattern.step_seconds,0.125)
        self.assertEqual(pattern.record_at(3,0.255),2)
        self.assertIn((3,2),pattern.hits)
        self.assertEqual(pattern.record_at(5,4.0),0)
        self.assertIn((5,0),pattern.hits)

    def test_invalid_bounds(self):
        for bpm,bars in ((0,2),(241,2),(120,3),(120,16)):
            with self.subTest(bpm=bpm,bars=bars):
                with self.assertRaises(ValueError):PadPattern(bpm,bars)
        pattern=PadPattern()
        for pad,step in ((16,0),(-1,1),(2,99)):
            with self.assertRaises(ValueError):pattern.set_hit(pad,step)

    def test_individual_hits_toggle_and_json_roundtrip(self):
        pattern=PadPattern(132,2)
        self.assertTrue(pattern.toggle_hit(0,0))
        self.assertFalse(pattern.toggle_hit(0,0))
        pattern.set_hit(0,0)
        pattern.set_hit(15,31)
        with tempfile.TemporaryDirectory() as folder:
            p=Path(folder)/'pattern.json'
            pattern.write_json(p)
            recovered=PadPattern.from_json(p)
        self.assertEqual(pattern.hits,recovered.hits)
        self.assertEqual(recovered.data()['hits'][1]['midi_note'],DEFAULT_NOTE+15)

    def test_midi_export_can_be_read_by_mido_if_present(self):
        with tempfile.TemporaryDirectory() as folder:
            p=Path(folder)/'pattern.mid'
            pattern=PadPattern(132,2,{(0,0),(3,4),(15,31)})
            pattern.write_midi(p,repeat=2)
            data=p.read_bytes()
            self.assertTrue(data.startswith(b'MThd\x00\x00\x00\x06\x00\x00\x00\x01\x01\xe0'))
            self.assertIn(b'MTrk',data)
            self.assertIn(b'\xff\x2f\x00',data)
            try:
                import mido
            except ImportError:
                return
            midi=mido.MidiFile(str(p))
            notes=[msg.note for msg in midi.tracks[0] if msg.type=='note_on']
            noteoffs=[msg.note for msg in midi.tracks[0] if msg.type=='note_off']
            self.assertEqual(len(notes),6)
            self.assertEqual(len(noteoffs),6)
            self.assertEqual(set(notes),{36,39,51})
            self.assertEqual(midi.ticks_per_beat,480)

    def test_vlq_boundaries(self):
        self.assertEqual(vlq(0),b'\x00')
        self.assertEqual(vlq(127),b'\x7f')
        self.assertEqual(vlq(128),b'\x81\x00')
        with self.assertRaises(ValueError):vlq(-1)


if __name__=='__main__':
    unittest.main()
