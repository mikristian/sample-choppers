import unittest
from types import SimpleNamespace

from flx4_sampler_bridge import translate


def midi(kind, channel, note, velocity=127):
    return SimpleNamespace(type=kind, channel=channel, note=note, velocity=velocity)


class MappingTest(unittest.TestCase):
    def test_every_pad(self):
        # All physical pad modes must select the same eight slice slots.
        for deck_channel, target_base in ((7, 36), (9, 44)):
            for note_base in (0x10, 0x30, 0x50):  # PAD FX1, SAMPLER, PAD FX2
                for index in range(8):
                    self.assertEqual(translate(midi("note_on", deck_channel, note_base + index)),
                                     ("note_on", target_base + index, 127))
                    self.assertEqual(translate(midi("note_off", deck_channel, note_base + index)),
                                     ("note_off", target_base + index, 0))

    def test_deck_controls_shift_other_modes_ignored(self):
        for channel, note in ((0, 0x0B), (1, 0x0B), (6, 0x46),
                              (8, 0x10), (10, 0x50), (8, 0x30), (10, 0x30),
                              (9, 0x20), (7, 0x2F), (9, 0x38),
                              (7, 0x0F), (9, 0x18), (7, 0x4F), (9, 0x58)):
            self.assertIsNone(translate(midi("note_on", channel, note)))
        self.assertIsNone(translate(midi("control_change", 7, 0x30)))

    def test_zero_velocity_is_release(self):
        self.assertEqual(translate(midi("note_on", 9, 0x37, 0)), ("note_off", 51, 0))
        self.assertEqual(translate(midi("note_on", 7, 0x10, 0)), ("note_off", 36, 0))
        self.assertEqual(translate(midi("note_on", 9, 0x57, 0)), ("note_off", 51, 0))


if __name__ == "__main__":
    unittest.main()
