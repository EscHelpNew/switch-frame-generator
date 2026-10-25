"""Check the app's own GPU smoke log/capture; no console or screen access."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1] / "build"


class DesktopCheck(unittest.TestCase):
    def test_modes_report_separate_counters(self):
        log = (ROOT / "desktop-smoke.log").read_text()
        rows = re.findall(r"PERF mode=(.*?) render=([\d.]+) present_calls=([\d.]+) generated=([\d.]+)", log)
        self.assertTrue(rows)
        self.assertEqual({row[0] for row in rows}, {"LSFG 30>60", "NATIVE 60", "REAL 30"})
        for mode, render, present, generated in rows:
            if mode != "LSFG 30>60":
                self.assertEqual(float(generated), 0, mode)
        self.assertIn("DESKTOP EXIT frames=360 gl_error=0", log)

    def test_compute_image_uses_switch_top_down_coordinates(self):
        data = (ROOT / "desktop-compute.rgba").read_bytes()
        offset = (604 * 1280 + 50) * 4
        actual = data[offset:offset + 3]
        for value, wanted in zip(actual, (76, 217, 230)):
            self.assertLessEqual(abs(value - wanted), 5, tuple(actual))

    def test_floor_is_at_bottom_of_display(self):
        data = (ROOT / "desktop-capture.rgba").read_bytes()
        self.assertEqual(len(data), 1280 * 720 * 4)
        # GL capture is bottom-up; the scene's floor is at screen y=0.84*720.
        offset = ((719 - 604) * 1280 + 50) * 4
        actual = data[offset:offset + 3]
        for value, wanted in zip(actual, (76, 217, 230)):
            self.assertLessEqual(abs(value - wanted), 5, tuple(actual))


if __name__ == "__main__":
    unittest.main(verbosity=2)
