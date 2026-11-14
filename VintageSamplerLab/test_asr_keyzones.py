"""Tests for the original keyboard keyzone/velocity sampler, no vendor ROMs."""

from array import array
import math
from pathlib import Path
import tempfile
import unittest
import wave

from asr_keyzones import KeyZone, KeyboardProgram


class ASRPrototypeTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory()
        self.wav=Path(self.tmp.name)/'original-tone.wav'
        rate=16000
        tone=array('h',(int(math.sin(2*math.pi*220*i/rate)*3500) for i in range(rate)))
        with wave.open(str(self.wav),'wb') as f:
            f.setnchannels(1);f.setsampwidth(2);f.setframerate(rate);f.writeframes(tone.tobytes())
    def tearDown(self):self.tmp.cleanup()

    def test_keyzone_root_and_octave_speed(self):
        program=KeyboardProgram(zones=[KeyZone(str(self.wav),60,36,96,duration_seconds=1)])
        root=program.render(60,100)
        octave=program.render(72,100)
        self.assertAlmostEqual(len(root)/len(octave),2,delta=0.02)
        self.assertEqual(len(program.render(96,100))>1000,True)

    def test_velocity_and_zone_boundaries(self):
        program=KeyboardProgram(zones=[KeyZone(str(self.wav),60,48,84,duration_seconds=1)])
        loud=program.render(60,127)
        quiet=program.render(60,40)
        self.assertGreater(max(map(abs,loud)),max(map(abs,quiet)))
        with self.assertRaises(ValueError):program.render(47,100)
        with self.assertRaises(ValueError):program.render(85,100)

    def test_multizone_and_program_roundtrip(self):
        program=KeyboardProgram('Two layer test',zones=[
            KeyZone(str(self.wav),60,48,72,duration_seconds=0.6),
            KeyZone(str(self.wav),72,72,84,duration_seconds=0.6),
        ])
        self.assertTrue(program.render(72,90))
        target=Path(self.tmp.name)/'program.json'
        program.save_program(target)
        recovered=KeyboardProgram.load_program(target)
        self.assertEqual(recovered.name,'Two layer test')
        self.assertEqual(len(recovered.zones),2)

    def test_no_original_audio_replaced_on_note_export(self):
        program=KeyboardProgram(zones=[KeyZone(str(self.wav),60,36,96,duration_seconds=0.6)])
        previous=self.wav.read_bytes()
        output=program.export_note(Path(self.tmp.name)/'rendered.wav',72,100)
        with wave.open(str(output),'rb') as f:
            self.assertEqual(f.getframerate(),44100)
        self.assertEqual(self.wav.read_bytes(),previous)


if __name__=='__main__':unittest.main()
