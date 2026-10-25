"""Tests de las funciones puras de compare_calibration.py.

Uso: uv run --with numpy --with pillow python -m unittest discover -s Tools/Verify -p "test_*.py"
"""

import unittest

import numpy as np

from compare_calibration import evaluate, masked_abs_diff, polygon_mask, scale_corners, to_percent


class PolygonMaskTest(unittest.TestCase):
    def test_square_area_and_erosion(self):
        corners = [[10, 10], [30, 10], [30, 30], [10, 30]]
        mask = polygon_mask((40, 40), corners, 0)
        self.assertTrue(mask[20, 20])
        self.assertFalse(mask[5, 5])
        eroded = polygon_mask((40, 40), corners, 3)
        self.assertLess(eroded.sum(), mask.sum())
        self.assertFalse(eroded[11, 11])  # el borde se quita
        self.assertTrue(eroded[20, 20])

    def test_outside_polygon_is_empty(self):
        mask = polygon_mask((20, 20), [[-50, -50], [-40, -50], [-40, -40]], 0)
        self.assertEqual(mask.sum(), 0)


class MaskedDiffTest(unittest.TestCase):
    def test_only_masked_pixels_count(self):
        a = np.zeros((4, 4, 3), dtype=np.uint8)
        b = np.zeros((4, 4, 3), dtype=np.uint8)
        b[0, 0] = 255  # fuera de la máscara
        b[2, 2] = 51
        mask = np.zeros((4, 4), dtype=bool)
        mask[2:, 2:] = True
        total, count = masked_abs_diff(a, b, mask)
        self.assertEqual(count, 12)
        self.assertAlmostEqual(total, 51 * 3)
        self.assertAlmostEqual(to_percent(total / count), 51 * 3 / 12 / 255 * 100)

    def test_shape_mismatch_raises(self):
        with self.assertRaises(ValueError):
            masked_abs_diff(np.zeros((2, 2, 3)), np.zeros((3, 2, 3)), np.ones((2, 2), dtype=bool))


class EvaluateTest(unittest.TestCase):
    def test_accepts_low_reference_and_high_control(self):
        verdict = evaluate(0.8, 5.0, 2.0, 3.0)
        self.assertTrue(verdict["accepted"])
        self.assertAlmostEqual(verdict["ratio"], 6.25)

    def test_rejects_high_reference_or_weak_control(self):
        self.assertFalse(evaluate(2.5, 20.0, 2.0, 3.0)["accepted"])
        self.assertFalse(evaluate(1.0, 2.9, 2.0, 3.0)["accepted"])


class ScaleCornersTest(unittest.TestCase):
    def test_scales_to_image_size(self):
        self.assertEqual(scale_corners([[640, 360]], [1280, 720], (640, 360)), [[320.0, 180.0]])
        self.assertEqual(scale_corners([[10, 20]], [100, 100], (100, 100)), [[10.0, 20.0]])


if __name__ == "__main__":
    unittest.main()
