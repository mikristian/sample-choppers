"""Test synthesized test-tone clips, with no commercial audio input."""

import math
from pathlib import Path
import tempfile
import unittest
import wave

from sampler_engine import PROFILES, export_kit, process, probe


class EngineTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.wav = Path(self.tmp.name) / 'original-test-tone.wav'
        rate = 16000
        import array
        pcm = array.array('h',(int(14000*math.sin(2*math.pi*220*i/rate)) for i in range(rate)))
        with wave.open(str(self.wav), 'wb') as w:
            w.setnchannels(1);w.setsampwidth(2);w.setframerate(rate);w.writeframes(pcm.tobytes())

    def tearDown(self):
        self.tmp.cleanup()

    def test_sample_is_intact_after_export(self):
        original = self.wav.read_bytes()
        data = export_kit(self.wav,Path(self.tmp.name)/'out','SP1200-inspired',pad_count=16,duration_seconds=1)
        self.assertEqual(len(data['pads']),16)
        self.assertEqual(self.wav.read_bytes(),original)
        self.assertEqual(len(list((Path(self.tmp.name)/'out').glob('pad_*.wav'))),16)
        self.assertEqual(probe(Path(self.tmp.name)/'out'/'pad_01.wav')['rate'],44100)

    def test_pitch_doubles_speed(self):
        x=[math.sin(i/10) for i in range(1600)]
        plain=process(x,16000,'MPC2000XL-inspired',0)
        octave=process(x,16000,'MPC2000XL-inspired',12)
        self.assertAlmostEqual(len(plain)/len(octave),2,places=2)

    def test_vintage_profile_changes_rendered_sound(self):
        x=[math.sin(i/10) for i in range(1600)]
        a=process(x,16000,'MPC2000XL-inspired')
        b=process(x,16000,'S950-inspired')
        self.assertEqual(len(a),len(b))
        self.assertNotEqual(a,b)

    def test_bad_input_rejected(self):
        with self.assertRaises(ValueError):process([],44100)
        with self.assertRaises(ValueError):process([.1],44100,'unknown')
        with self.assertRaises(ValueError):export_kit(self.wav,Path(self.tmp.name)/'x','S950-inspired',pad_count=7)


if __name__=='__main__':
    unittest.main()
