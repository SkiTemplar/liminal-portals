"""Tests de check_tour.py y make_lamina.py.

Uso: uv run --with numpy --with pillow python -m unittest discover -s Tools/Verify -p "test_*.py"
"""

import json
import tempfile
import unittest
from pathlib import Path

import numpy as np
from PIL import Image

from check_tour import (MAX_DARK_FOREGROUND, MIN_LIT_WALL_LUMA, MIN_MEAN_LUMA, center_crop, check_tour,
                         mean_luma)
from make_lamina import THUMB_SIZE, make_lamina

# Vista de puerta sintética de 200 × 160: hueco [80, 120] × [40, 120], marco +10 px, pared +20 px.
PORTAL = [[80, 120], [120, 120], [120, 40], [80, 40]]
FRAME = [[70, 120], [130, 120], [130, 30], [70, 30]]
WALL = [[50, 120], [150, 120], [150, 10], [50, 10]]


def noisy(shape, base, spread, seed=0):
    rng = np.random.default_rng(seed)
    return np.clip(base + rng.integers(-spread, spread + 1, shape), 0, 255).astype(np.uint8)


def door_image(portal_pixels: np.ndarray, frame_value=90, wall_value=200) -> np.ndarray:
    image = noisy((160, 200, 3), 120, 30)
    image[10:120, 50:150] = wall_value
    image[30:120, 70:130] = frame_value
    image[40:120, 80:120] = portal_pixels
    return image


def write_tour(directory: Path, images: dict, complete: bool = True, extra: dict | None = None) -> None:
    shots = []
    for name, pixels in images.items():
        Image.fromarray(pixels).save(directory / f"{name}.png")
        shot = {"name": name, "file": f"{name}.png", "saved": True}
        shot.update((extra or {}).get(name, {}))
        shots.append(shot)
    (directory / "tour.json").write_text(json.dumps({"complete": complete, "error": "", "shots": shots}), encoding="utf-8")


def door_fields(kind: str) -> dict:
    return {"kind": kind, "biome": "door0_Jungle", "viewport": [200, 160],
            "portalCorners": PORTAL, "frameCorners": FRAME, "wallCorners": WALL}


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
            write_tour(Path(tmp), {"00_hub": noisy((60, 80, 3), 120, 40)})
            self.assertTrue(check_tour(Path(tmp))["ok"])

    def test_black_or_black_center_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            dark_center = noisy((100, 100, 3), 120, 40)
            dark_center[40:60, 40:60] = 0
            write_tour(Path(tmp), {"00_black": np.zeros((60, 80, 3), np.uint8), "01_center": dark_center})
            result = check_tour(Path(tmp))
            self.assertFalse(result["ok"])
            self.assertFalse(result["shots"][0]["ok"])
            self.assertFalse(result["shots"][1]["ok"])
            self.assertLess(result["shots"][1]["centerLuma"], MIN_MEAN_LUMA)

    def test_uniform_image_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            write_tour(Path(tmp), {"00_flat": np.full((60, 80, 3), 120, np.uint8)})
            result = check_tour(Path(tmp))
            self.assertFalse(result["ok"])
            self.assertLess(result["shots"][0]["lumaStd"], 1.0)

    def test_incomplete_or_missing_index_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertFalse(check_tour(Path(tmp))["ok"])
            write_tour(Path(tmp), {"00_hub": noisy((10, 10, 3), 120, 40)}, complete=False)
            self.assertFalse(check_tour(Path(tmp))["ok"])


class DoorRegionTest(unittest.TestCase):
    def test_portal_showing_another_world_passes(self):
        with tempfile.TemporaryDirectory() as tmp:
            image = door_image(noisy((80, 40, 3), 170, 60, seed=3))
            write_tour(Path(tmp), {"01_door": image}, extra={"01_door": door_fields("from_hub")})
            result = check_tour(Path(tmp))
            self.assertTrue(result["ok"], result)
            self.assertGreater(result["shots"][0]["door"]["portalFrameDiff"], 50.0)

    def test_portal_like_frame_or_flat_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            same = door_image(noisy((80, 40, 3), 90, 3, seed=4))
            flat = door_image(np.full((80, 40, 3), 200, np.uint8))
            write_tour(Path(tmp), {"01_same": same, "02_flat": flat},
                       extra={"01_same": door_fields("from_hub"), "02_flat": door_fields("from_hub")})
            result = check_tour(Path(tmp))
            self.assertFalse(result["shots"][0]["ok"])
            self.assertFalse(result["shots"][1]["ok"])
            self.assertLess(result["shots"][1]["door"]["portalStd"], 1.0)

    def test_biome_view_requires_lit_wall(self):
        with tempfile.TemporaryDirectory() as tmp:
            portal = noisy((80, 40, 3), 170, 60, seed=5)
            lit = door_image(portal, wall_value=210)
            shadowed = door_image(portal, wall_value=90)
            write_tour(Path(tmp), {"02_lit": lit, "03_shadow": shadowed, "04_hub": shadowed},
                       extra={"02_lit": door_fields("from_biome"), "03_shadow": door_fields("from_biome"),
                              "04_hub": door_fields("from_hub")})
            shots = check_tour(Path(tmp))["shots"]
            self.assertTrue(shots[0]["ok"], shots[0])
            self.assertFalse(shots[1]["ok"])
            self.assertLess(shots[1]["door"]["wallLuma"], MIN_LIT_WALL_LUMA)
            # Desde el hub no se exige la pared iluminada.
            self.assertTrue(shots[2]["ok"], shots[2])


class ForegroundTest(unittest.TestCase):
    def test_inside_view_with_black_foreground_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            lit = noisy((100, 120, 3), 120, 40, seed=6)
            black = lit.copy()
            black[70:] = noisy((30, 120, 3), 8, 6, seed=7)
            write_tour(Path(tmp), {"03_lit": lit, "03_black": black, "02_black": black},
                       extra={"03_lit": {"kind": "inside"}, "03_black": {"kind": "inside"},
                              "02_black": {"kind": "hub_overview"}})
            shots = check_tour(Path(tmp))["shots"]
            self.assertTrue(shots[0]["ok"], shots[0])
            self.assertLessEqual(shots[0]["darkForeground"], MAX_DARK_FOREGROUND)
            # La media de la imagen entera pasa, pero el primer plano es negro.
            self.assertGreater(shots[1]["meanLuma"], MIN_MEAN_LUMA)
            self.assertFalse(shots[1]["ok"])
            self.assertGreater(shots[1]["darkForeground"], 0.9)
            # Solo se exige en las vistas desde dentro.
            self.assertTrue(shots[2]["ok"], shots[2])


class LaminaTest(unittest.TestCase):
    def test_sheet_has_one_row_per_biome(self):
        with tempfile.TemporaryDirectory() as tmp:
            images, extra = {}, {}
            for row, biome in enumerate(("door0_Jungle", "door1_Desert")):
                for kind in ("from_hub", "from_biome", "inside"):
                    name = f"{row}_{kind}"
                    images[name] = noisy((72, 128, 3), 120, 40, seed=row)
                    extra[name] = {"kind": kind, "biome": biome}
            write_tour(Path(tmp), images, extra=extra)
            out = Path(tmp) / "lamina.png"
            ok, message = make_lamina(Path(tmp), out)
            self.assertTrue(ok, message)
            with Image.open(out) as sheet:
                self.assertGreater(sheet.size[0], 3 * THUMB_SIZE[0])
                self.assertGreater(sheet.size[1], 2 * THUMB_SIZE[1])
                self.assertLess(sheet.size[1], 3 * THUMB_SIZE[1])

    def test_missing_view_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            write_tour(Path(tmp), {"0_hub": noisy((72, 128, 3), 120, 40)},
                       extra={"0_hub": {"kind": "from_hub", "biome": "door0_Jungle"}})
            ok, message = make_lamina(Path(tmp), Path(tmp) / "lamina.png")
            self.assertFalse(ok)
            self.assertIn("faltan", message)
            self.assertFalse((Path(tmp) / "lamina.png").exists())


if __name__ == "__main__":
    unittest.main()
