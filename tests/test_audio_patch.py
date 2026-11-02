#!/usr/bin/env python3
from pathlib import Path
import re
import unittest

SOURCE = Path(__file__).resolve().parents[1] / 'kore1-bridge.c'

class AudioPatchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE.read_text()

    def test_audio_params_no_extra_bank_byte(self):
        self.assertRegex(self.source, r'unsigned char command\[\]\s*=\s*\{0x09,0x01,0x02,')
        self.assertNotIn('0x09,0x00,0x01,0x02,', self.source)

    def test_zero_packet_guard_present(self):
        start = self.source.index('static void audio_send_silence_matching_rx(')
        end = self.source.index('/* v0.13 experimental stream priming:', start)
        block = self.source[start:end]
        self.assertIn('if(!audio_out_fixed)', block)
        self.assertIn('iso_packet_desc[i].length==0', block)
        self.assertLess(block.index('iso_packet_desc[i].length==0'), block.index('libusb_submit_transfer(slot->transfer)'))

if __name__ == '__main__':
    unittest.main(verbosity=2)
