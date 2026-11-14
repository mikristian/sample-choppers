import unittest

import numpy as np

from direct_flx4_audio import DirectFLX4Audio


class DirectFLX4MixerTests(unittest.TestCase):
    def setUp(self):
        # Synthetic audio keeps the tests entirely offline and quiet.
        self.slices = [np.full((1024, 2), (i + 1) / 60.0, dtype=np.float32)
                       for i in range(16)]
        self.engine = DirectFLX4Audio(samples=self.slices)

    def audio_frame(self, frames=512):
        output = np.empty((frames, 4), dtype=np.float32)
        self.engine._callback(output, frames, None, None)
        return output

    def test_left_and_right_decks_play_simultaneously(self):
        self.engine.trigger(1)
        self.engine.trigger(9)
        result = self.audio_frame()
        desired = np.tanh((self.slices[0][:512] + self.slices[8][:512]) * 0.85)
        np.testing.assert_allclose(result[:, :2], desired, atol=1e-6)
        np.testing.assert_array_equal(result[:, 2:], np.zeros((512, 2), dtype=np.float32))
        self.assertEqual(self.engine.status()["active_voices"], 2)
        self.assertGreater(self.engine.status()["last_peak"], 0)

    def test_all_sixteen_can_overlap(self):
        for pad in range(1, 17):
            self.engine.trigger(pad)
        self.audio_frame()
        self.assertEqual(self.engine.status()["active_voices"], 16)
        self.assertEqual(self.engine.status()["triggered"], 16)
        self.audio_frame()
        self.assertEqual(self.engine.status()["active_voices"], 0)

    def test_one_shots_and_stop_all(self):
        self.engine.trigger(5, 92)
        self.audio_frame()
        self.engine.stop_all()
        stopped = self.audio_frame()
        self.assertEqual(self.engine.status()["active_voices"], 0)
        self.assertEqual(float(np.max(np.abs(stopped))), 0.0)

    def test_invalid_pads_are_rejected(self):
        for pad in (0, 17, "1", True):
            with self.assertRaises(ValueError):
                self.engine.trigger(pad)


if __name__ == "__main__":
    unittest.main()
