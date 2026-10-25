"""Tests del comprobador: intersección triángulo-caja, hueco de puerta, presupuestos y lector FBX."""
import struct
import zlib
from pathlib import Path

import pytest

import fbx_reader
from kit_hub import door_frame
from lp_checks import AssetSpec, door_clearance_hits, evaluate, tri_box_overlap
from lp_geom import triangles


def test_tri_box_overlap_inside_and_outside():
    assert tri_box_overlap(((0, 0, 0), (1, 0, 0), (0, 1, 0)), (0, 0, 0), (0.5, 0.5, 0.5))
    assert not tri_box_overlap(((2, 2, 2), (3, 2, 2), (2, 3, 2)), (0, 0, 0), (0.5, 0.5, 0.5))
    # Triángulo grande que atraviesa la caja sin vértices dentro.
    assert tri_box_overlap(((-10, -10, 0), (10, -10, 0), (0, 10, 0)), (0, 0, 0), (0.5, 0.5, 0.5))
    # Solo toca la cara de la caja: no invade.
    assert not tri_box_overlap(((0.5, -1, -1), (0.5, 1, -1), (0.5, 0, 1)), (0, 0, 0), (0.5, 0.5, 0.5))


def test_door_frame_leaves_opening_free():
    assert door_clearance_hits(triangles(door_frame(True))) == 0


def test_door_clearance_detects_invasion():
    # Caso negativo: un montante 2 cm dentro del hueco.
    jamb = ((0.0, 68.0, 0.0), (0.0, 68.0, 230.0), (-20.0, 68.0, 0.0))
    assert door_clearance_hits([jamb]) == 1


def test_spec_rejects_budget_over_category_cap():
    with pytest.raises(ValueError):
        AssetSpec("SM_X_Y_01", "rock", 601)
    with pytest.raises(ValueError):
        AssetSpec("SM_X_Y_01", "nope", 10)


def measured(**overrides):
    base = {"tris": 10, "bbox_min": [-1, -1, 0], "bbox_max": [1, 1, 2], "materials": 1, "color_layers": ["Col"],
            "emissive_corners": 0, "unit_scale": 100.0, "lcl_scaling": [1, 1, 1], "door_hits": 0}
    base.update(overrides)
    return base


def test_evaluate_passes_and_flags_failures():
    spec = AssetSpec("SM_X_Y_01", "rock", 20)
    assert all(evaluate(spec, measured()).values())
    assert not evaluate(spec, measured(bbox_min=[-1, -1, -3]))["z_min_is_zero"]
    assert not evaluate(spec, measured(tris=21))["tris_within_budget"]
    assert not evaluate(spec, measured(materials=2))["single_material"]
    assert not evaluate(spec, measured(color_layers=[]))["has_col_attribute"]
    assert not evaluate(spec, measured(emissive_corners=4))["emissive_as_expected"]
    assert not evaluate(spec, measured(unit_scale=1.0))["unit_is_m_scale_100"]


# --- FBX binario sintético ------------------------------------------------------------------

def _prop(value) -> bytes:
    if isinstance(value, str):
        raw = value.encode()
        return b"S" + struct.pack("<I", len(raw)) + raw
    if isinstance(value, float):
        return b"D" + struct.pack("<d", value)
    if isinstance(value, int):
        return b"I" + struct.pack("<i", value)
    kind, items, compress = value
    raw = struct.pack(f"<{len(items)}{kind}", *items)
    data = zlib.compress(raw) if compress else raw
    return kind.encode() + struct.pack("<III", len(items), 1 if compress else 0, len(data)) + data


def node(name, props=(), children=()):
    def encode(offset: int) -> bytes:
        body = b"".join(_prop(p) for p in props)
        cursor = offset + 13 + len(name) + len(body)
        nested = b""
        for child in children:
            chunk = child(cursor)
            nested += chunk
            cursor += len(chunk)
        if children:
            nested += b"\x00" * 13
        end = offset + 13 + len(name) + len(body) + len(nested)
        return struct.pack("<IIIB", end, len(props), len(body), len(name)) + name.encode() + body + nested
    return encode


def p70(*entries):
    return node("Properties70", (), [node("P", e) for e in entries])


def synthetic_fbx() -> bytes:
    roots = [
        node("GlobalSettings", (), [p70(("UnitScaleFactor", "double", "Number", "", 100.0),
                                        ("UpAxis", "int", "Integer", "", 1))]),
        node("Objects", (), [
            node("Geometry", (1, "G\x00\x01Geometry", "Mesh"), [
                node("Vertices", [("d", [0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0], True)]),
                node("PolygonVertexIndex", [("i", [0, 1, ~2], False)]),
                node("LayerElementColor", (0,), [
                    node("Name", ("Col",)), node("MappingInformationType", ("ByPolygonVertex",)),
                    node("ReferenceInformationType", ("IndexToDirect",)),
                    node("Colors", [("d", [1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 0.0], False)]),
                    node("ColorIndex", [("i", [0, 1, 1], False)]),
                ]),
            ]),
            node("Model", (2, "SM_Test_Tri_01\x00\x01Model", "Mesh"),
                 [p70(("Lcl Scaling", "Lcl Scaling", "", "A", 1.0, 1.0, 1.0))]),
            node("Material", (3, "M_LowPoly\x00\x01Material", ""), ()),
        ]),
    ]
    data = bytearray(fbx_reader.MAGIC + b"\x1a\x00" + struct.pack("<I", 7400))
    for root in roots:
        data += root(len(data))
    data += b"\x00" * 13
    return bytes(data)


def test_fbx_reader_parses_synthetic_file():
    summary = fbx_reader.summarize(synthetic_fbx())
    assert summary.unit_scale == 100.0
    assert summary.tris == 1
    assert summary.color_layers == ["Col"]
    assert summary.corner_alphas == [1.0, 0.0, 0.0]
    assert summary.materials == ["M_LowPoly"]
    assert summary.models[0]["name"] == "SM_Test_Tri_01"


def test_fbx_reader_rejects_non_fbx():
    with pytest.raises(ValueError):
        fbx_reader.summarize(b"not an fbx at all, just bytes")


EXPORTED = Path(__file__).resolve().parents[4] / "Art" / "Export" / "Hub" / "SM_Hub_DoorFrame_01.fbx"


@pytest.mark.skipif(not EXPORTED.exists(), reason="sin FBX exportado")
def test_fbx_reader_on_exported_door_frame():
    summary = fbx_reader.summarize(EXPORTED.read_bytes())
    assert summary.unit_scale == 100.0
    assert summary.materials == ["M_LowPoly"]
    assert "Col" in summary.color_layers
