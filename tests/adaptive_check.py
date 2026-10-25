"""Checks measured adaptive preview output; never accesses a console."""
from collections import Counter
from pathlib import Path
import csv
import json
import unittest

OUT = Path(__file__).resolve().parents[1] / "build"
def rows(name):
    with (OUT / name).open(encoding="utf-8") as file:
        return list(csv.DictReader(file))


class AdaptiveChecks(unittest.TestCase):
    def test_target40_cap1_and_no_repeats_after_warmup(self):
        source = [r for r in rows("perf-adaptive40-source.csv") if float(r["wall_s"]) >= 2]
        frames = [r for r in rows("perf-adaptive40-present.csv") if float(r["wall_s"]) >= 2]
        duration = float(frames[-1]["wall_s"]) - 2
        self.assertGreater(len(frames) / duration, 39)
        self.assertLess(len(frames) / duration, 41)
        generated = Counter(r["source_id"] for r in frames if r["kind"] == "generated")
        self.assertTrue(generated)
        self.assertLessEqual(max(generated.values()), 1)
        self.assertFalse(any(r["kind"] == "repeat" for r in frames))
        self.assertTrue(all(int(r["generated_computed"]) <= 1 for r in source))
        ticks = [int(r["logical_tick"]) for r in frames]
        self.assertTrue(all(b == a + 1 for a, b in zip(ticks, ticks[1:])))
        metadata = json.loads((OUT / "perf-adaptive40-meta.json").read_text())
        self.assertEqual(metadata["target_fps"], 40)
        self.assertEqual(metadata["cap_generated_per_source"], 1)
        log = (OUT / "perf-adaptive40.log").read_text()
        self.assertIn("overflow=0 gl_error=0", log)

    def test_off_mode_shows_real_images_and_repeats(self):
        frames = [r for r in rows("perf-variable-off-present.csv") if float(r["wall_s"]) >= 2]
        self.assertTrue(any(r["kind"] == "repeat" for r in frames))
        self.assertFalse(any(r["kind"] == "generated" for r in frames))


if __name__ == "__main__":
    unittest.main(verbosity=2)
