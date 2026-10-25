"""Lógica pura del kit de packs: color, recoloreado, medida, validación de mallas y catálogo."""

from __future__ import annotations

import copy
from pathlib import Path
from types import SimpleNamespace

import pytest

import packs_core as pc

HERE = Path(__file__).resolve().parent.parent


def P(x: float, y: float, z: float) -> SimpleNamespace:
    return SimpleNamespace(x=x, y=y, z=z)


@pytest.fixture(scope="module")
def data() -> dict:
    return {
        "catalog": pc.load_json(HERE / "catalog.json"),
        "palettes": pc.flatten_palettes(pc.load_json(HERE / "palettes.json")),
        "packs": {p["id"] for p in pc.load_json(HERE / "packs.json")["packs"]},
    }


# --- color -----------------------------------------------------------------

def test_hex_roundtrip():
    assert pc.to_hex(pc.hex_to_srgb("#1F4D2B")) == "#1f4d2b"


@pytest.mark.parametrize("bad", ["#12345", "#GGGGGG", "", "#1234567"])
def test_hex_rejects_invalid(bad):
    with pytest.raises(ValueError):
        pc.hex_to_srgb(bad)


def test_linear_srgb_roundtrip():
    for c in (0.0, 0.002, 0.04, 0.5, 1.0):
        assert pc.linear_to_srgb(pc.srgb_to_linear(c)) == pytest.approx(c, abs=1e-9)


def test_oklab_roundtrip():
    for hx in ("#E05A47", "#3A4F47", "#F4F7FA", "#000000"):
        srgb = pc.hex_to_srgb(hx)
        assert pc.oklab_to_srgb(pc.oklab(srgb)) == pytest.approx(srgb, abs=1e-6)


def test_oklab_white_is_l1():
    assert pc.oklab((1.0, 1.0, 1.0)) == pytest.approx((1.0, 0.0, 0.0), abs=1e-4)


# --- recoloreado -----------------------------------------------------------

def test_pick_swatch_nearest_rule():
    compiled, tol, default = pc.compile_rules(
        {"rules": [{"from": "#4b623e", "to": "verde"}, {"from": "#62433e", "to": "tronco"}]})
    assert pc.pick_swatch(pc.hex_to_srgb("#4d643f"), compiled, tol, default) == "verde"
    assert pc.pick_swatch(pc.hex_to_srgb("#60413c"), compiled, tol, default) == "tronco"


def test_pick_swatch_default_when_far():
    compiled, tol, default = pc.compile_rules({"rules": [{"from": "#4b623e", "to": "verde"}], "default": "roca"})
    assert pc.pick_swatch(pc.hex_to_srgb("#ff00ff"), compiled, tol, default) == "roca"


def test_pick_swatch_fails_without_rule_or_default():
    compiled, tol, default = pc.compile_rules({"rules": [{"from": "#4b623e", "to": "verde"}]})
    with pytest.raises(RuntimeError, match="sin regla"):
        pc.pick_swatch(pc.hex_to_srgb("#ff00ff"), compiled, tol, default, "SM_X")


def test_pick_swatch_tie_goes_to_first_rule():
    compiled, tol, default = pc.compile_rules(
        {"rules": [{"from": "#808080", "to": "a"}, {"from": "#808080", "to": "b"}]})
    assert pc.pick_swatch(pc.hex_to_srgb("#808080"), compiled, tol, default) == "a"


def test_gradient_keeps_hue_and_brightens_upwards():
    base = "#2E6B36"
    lo, mid, hi = (pc.oklab(pc.gradient_srgb(base, h)) for h in (0.0, 0.5, 1.0))
    assert lo[0] < mid[0] < hi[0]
    assert hi[0] - lo[0] == pytest.approx(pc.DEFAULT_SPREAD, abs=1e-3)
    ref = pc.oklab(pc.hex_to_srgb(base))
    assert mid == pytest.approx(ref, abs=1e-6)
    for lab in (lo, hi):
        assert lab[1:] == pytest.approx(ref[1:], abs=2e-3)


def test_gradient_clamps_height_and_rejects_negative_spread():
    assert pc.gradient_srgb("#A0583A", -3) == pc.gradient_srgb("#A0583A", 0)
    assert pc.gradient_srgb("#A0583A", 7) == pc.gradient_srgb("#A0583A", 1)
    with pytest.raises(ValueError):
        pc.gradient_srgb("#A0583A", 0.5, spread=-0.1)


def test_palette_distance_ignores_gradient_but_not_hue():
    swatches = ["#2E6B36", "#5A3E2B"]
    for h in (0.0, 0.3, 1.0):
        assert pc.palette_distance(pc.gradient_srgb("#2E6B36", h), swatches) < 1e-3
    assert pc.palette_distance(pc.hex_to_srgb("#E05A47"), swatches) > pc.PALETTE_TOL


def test_share_percent_sorted():
    assert list(pc.share_percent({"a": 1.0, "b": 3.0})) == ["b", "a"]
    assert pc.share_percent({"a": 1.0, "b": 3.0})["b"] == 75.0


# --- medida, pivote y presupuesto -----------------------------------------

def test_size_factor_axes():
    vs = [P(0, 0, 0), P(2, 1, 4)]
    assert pc.size_factor(vs, {"axis": "z", "m": 8}) == 2
    assert pc.size_factor(vs, {"axis": "max", "m": 2}) == 0.5
    assert pc.size_factor(vs, {"axis": "x", "m": 1}) == 0.5
    with pytest.raises(ValueError):
        pc.size_factor(vs, {"axis": "w", "m": 1})
    with pytest.raises(ValueError):
        pc.size_factor([P(0, 0, 1), P(1, 1, 1)], {"axis": "z", "m": 1})


def test_base_pivot_is_footprint_centre_at_zmin():
    assert pc.base_pivot([P(-1, 2, 0.5), P(3, 4, 2), P(0, 3, 1)]) == (1, 3, 0.5)
    with pytest.raises(ValueError):
        pc.base_pivot([])


def test_decimate_ratio():
    assert pc.decimate_ratio(700, 800) is None
    assert pc.decimate_ratio(800, 800) is None
    r = pc.decimate_ratio(892, 800)
    assert r is not None and 892 * r < 800
    with pytest.raises(ValueError):
        pc.decimate_ratio(10, 0)


# --- validación de mallas exportadas --------------------------------------

GOOD = {"tris": 500, "dims": (1.0, 1.2, 1.5), "zmin": 0.0, "center": (0.0, 0.0), "materials": 1,
        "hasCol": True, "paletteMaxDE": 0.001}
BUSH = {"budget": 800, "range": {"z": [0.5, 2.5]}}


def test_check_mesh_passes():
    assert pc.check_mesh(GOOD, BUSH) == []


@pytest.mark.parametrize("change, fragment", [
    ({"tris": 801}, "presupuesto"),
    ({"dims": (1, 1, 3.0)}, "fuera de"),
    ({"dims": (1, 1, 0.2)}, "fuera de"),
    ({"zmin": 0.02}, "z mínima"),
    ({"zmin": -0.011}, "z mínima"),
    ({"center": (0.05, 0.0)}, "pivote"),
    ({"materials": 2}, "materiales"),
    ({"hasCol": False}, "Col"),
    ({"paletteMaxDE": 0.1}, "paleta"),
])
def test_check_mesh_negative(change, fragment):
    errors = pc.check_mesh({**GOOD, **change}, BUSH)
    assert len(errors) == 1 and fragment in errors[0]


def test_check_mesh_zmin_inside_tolerance():
    assert pc.check_mesh({**GOOD, "zmin": 0.009}, BUSH) == []


# --- catálogo -------------------------------------------------------------

def test_real_catalog_is_valid(data):
    assert pc.validate_catalog(data["catalog"], data["palettes"], data["packs"]) == []


def test_real_catalog_uses_every_manifest_pack(data):
    used = {e["pack"] for e in data["catalog"]["entries"]}
    assert used == data["packs"], "packs.json debe listar solo los packs que se usan"


def test_palettes_match_arte_md(data):
    arte = (HERE.parents[2] / "docs" / "ARTE.md").read_text(encoding="utf-8").upper()
    for biome, swatches in data["palettes"].items():
        for name, hx in swatches.items():
            assert hx.upper() in arte, f"{biome}.{name} {hx} no aparece en docs/ARTE.md"


def _mutate(data, fn) -> list[str]:
    cat = copy.deepcopy(data["catalog"])
    fn(cat)
    return pc.validate_catalog(cat, data["palettes"], data["packs"])


@pytest.mark.parametrize("fn, fragment", [
    (lambda c: c["entries"][0].update(mesh="SM_Jungle_Tree_1"), "nombre"),
    (lambda c: c["entries"][0].update(biome="Desert"), "bioma"),
    (lambda c: c["entries"][0].update(category="tree"), "categoría"),
    (lambda c: c["entries"][0].update(pack="otro"), "pack desconocido"),
    (lambda c: c["entries"][0].update(file="Blends/CommonTree_1.blend"), "formato"),
    (lambda c: c["entries"][0]["recolor"]["rules"][0].update(to="azul"), "paleta"),
    (lambda c: c["entries"][0]["recolor"]["rules"][0].update({"from": "#zzzzzz"}), "hexadecimal"),
    (lambda c: c["entries"][0].update(recolor={"rules": []}), "sin reglas"),
    (lambda c: c["entries"][0]["size"].update(m=30.0), "size"),
    (lambda c: c["entries"].append(copy.deepcopy(c["entries"][0])), "repetida"),
    (lambda c: c.update(entries=[e for e in c["entries"] if e["category"] != "cactus"]), "se piden"),
])
def test_catalog_negative(data, fn, fragment):
    errors = _mutate(data, fn)
    assert any(fragment in e for e in errors), errors


def test_parse_args():
    assert pc.parse_args([]) == {"biome": None, "meshes": []}
    assert pc.parse_args(["--biome", "Desert", "--meshes", "A", "B"]) == {"biome": "Desert", "meshes": ["A", "B"]}
    for bad in (["--biome"], ["--biome", "Mars"], ["--x"], ["--biome", "--meshes"]):
        with pytest.raises(SystemExit):
            pc.parse_args(bad)
