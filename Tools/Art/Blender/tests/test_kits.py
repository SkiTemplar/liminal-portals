"""Tests de los kits completos (geometría pura): determinismo, conteos, presupuestos y medidas."""
import re

import pytest

from kits import KITS
from lp_checks import CATEGORY_BUDGETS, door_clearance_hits
from lp_geom import is_emissive, triangles

NAME = re.compile(r"^SM_(?P<kit>[A-Za-z]+)_(?P<thing>[A-Za-z]+)_(?P<nn>\d{2})$")


@pytest.fixture(scope="module")
def built():
    return {kit_id: module.build() for kit_id, module in KITS.items()}


def test_builds_are_deterministic(built):
    for kit_id, module in KITS.items():
        again = module.build()
        assert [a.mesh for a in again] == [a.mesh for a in built[kit_id]], kit_id


def test_names_budgets_and_ground(built):
    for kit_id, assets in built.items():
        names = [a.spec.name for a in assets]
        assert len(names) == len(set(names))
        for asset in assets:
            match = NAME.match(asset.spec.name)
            assert match and match["kit"] == KITS[kit_id].KIT_NAME, asset.spec.name
            assert asset.mesh.tri_count <= asset.spec.budget <= CATEGORY_BUDGETS[asset.spec.category]
            assert asset.mesh.bbox()[0][2] == pytest.approx(0.0, abs=1e-6), asset.spec.name


def test_emissive_only_where_declared(built):
    for assets in built.values():
        for asset in assets:
            assert any(is_emissive(c) for c in asset.mesh.colors) == asset.spec.expect_emissive, asset.spec.name


def test_door_assets_leave_opening_free(built):
    doors = [a for assets in built.values() for a in assets if a.spec.door_clear]
    assert len(doors) == 6
    for asset in doors:
        assert door_clearance_hits(triangles(asset.mesh)) == 0, asset.spec.name


def count(assets, thing):
    return sum(1 for a in assets if NAME.match(a.spec.name)["thing"] == thing)


def test_ice_kit_contents(built):
    ice = built["ice"]
    assert count(ice, "Block") >= 4 and count(ice, "Shard") >= 3 and count(ice, "SnowMound") >= 3
    assert count(ice, "PressureRidge") >= 1
    horizon = [a for a in ice if a.spec.category == "horizon"]
    assert len(horizon) >= 2
    for asset in horizon:
        lo, hi = asset.mesh.bbox()
        assert 2000.0 <= max(hi[i] - lo[i] for i in range(3)) <= 6000.0


def test_return_wall_variants_and_size(built):
    walls = built["returnwall"]
    assert len(walls) == 4
    for asset in walls:
        lo, hi = asset.mesh.bbox()
        assert hi[1] - lo[1] == pytest.approx(600.0, abs=1.0)
        assert hi[2] <= 350.0 and asset.mesh.tri_count <= 2000


def test_crops_wheat_budget(built):
    wheat = [a for a in built["crops"] if "Wheat" in a.spec.name]
    assert wheat and all(a.mesh.tri_count <= 300 for a in wheat)
