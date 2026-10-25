"""Tests de check_tour.py.

Uso: uv run --with numpy --with pillow python -m unittest discover -s Tools/Verify -p "test_*.py"
"""

import json
import tempfile
import unittest
from pathlib import Path

import numpy as np
from PIL import Image

from check_tour import MIN_MEAN_LUMA, center_crop, check_tour, mean_luma


def write_tour(directory: Path, images: dict, complete: bool = True) -> None:
    shots = []
    for name, pixels in images.items():
        Image.fromarray(pixels).save(directory / f"{name}.png")
        shots.append({"name": name, "file": f"{name}.png", "saved": True})
    (directory / "tour.json").write_text(json.dumps({"complete": complete, "error": "", "shots": shots}), encoding="utf-8")


class LumaTest(unittest.TestCase):
    def test_grey_levels(self):
        self.assertAlmostEqual(mean_luma(np.full((4, 4, 3), 128, np.uint8)), 128.0, places=6)
        self.assertAlmostEqual(mean_luma(np.zeros((4, 4, 3), np.uint8)), 0.0)

    def test_center_crop_ignores_borders(self):
        pixels = np.zeros((100, 100, 3), np.uint8)
        pixels[40:60, 40:60] = 200
        self.assertAlmostEqual(mean_luma(center_crop(pixels)), 200.0, places=6)


class CheckTourTest(unittest.TestCase):
    def test_lit_shots_pass(self):
        with tempfile.TemporaryDirectory() as tmp:
            write_tour(Path(tmp), {"00_hub": np.full((60, 80, 3), 120, np.uint8)})
            self.assertTrue(check_tour(Path(tmp))["ok"])

    def test_black_or_black_center_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            dark_center = np.full((100, 100, 3), 120, np.uint8)
            dark_center[40:60, 40:60] = 0
            write_tour(Path(tmp), {"00_black": np.zeros((60, 80, 3), np.uint8), "01_center": dark_center})
            result = check_tour(Path(tmp))
            self.assertFalse(result["ok"])
            self.assertFalse(result["shots"][0]["ok"])
            self.assertFalse(result["shots"][1]["ok"])
            self.assertLess(result["shots"][1]["centerLuma"], MIN_MEAN_LUMA)

    def test_incomplete_or_missing_index_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertFalse(check_tour(Path(tmp))["ok"])
            write_tour(Path(tmp), {"00_hub": np.full((10, 10, 3), 120, np.uint8)}, complete=False)
            self.assertFalse(check_tour(Path(tmp))["ok"])


if __name__ == "__main__":
    unittest.main()
