"""Comprueba los FBX exportados de los kits y escribe Art/Export/kits_report.json.

Python puro (sin Blender): lee los valores crudos del FBX con fbx_reader y los compara con la
especificación de cada asset (presupuesto, bbox, pivote, un material, atributo Col, emisivo y hueco
de puerta libre). Sale con 1 si falta algún FBX o falla alguna comprobación.

    uv run --no-project python Tools/Art/Blender/check_kits.py [--kits all]
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import fbx_reader  # noqa: E402
from kits import EXPORT_ROOT, fbx_path, select  # noqa: E402
from lp_checks import FBX_UNIT_SCALE, bbox_of, door_clearance_hits, evaluate  # noqa: E402

EMISSIVE_ALPHA_MAX = 0.01


def measure(path: Path, spec) -> dict:
    summary = fbx_reader.summarize(path.read_bytes())
    k = summary.unit_scale  # crudo → cm
    verts = [(x * k, y * k, z * k) for x, y, z in summary.verts]
    lo, hi = bbox_of(verts)
    tris = []
    for poly in summary.polygons:
        for j in range(1, len(poly) - 1):
            tris.append((verts[poly[0]], verts[poly[j]], verts[poly[j + 1]]))
    model = summary.models[0] if summary.models else {"rotation": [0, 0, 0], "scaling": [1, 1, 1]}
    return {
        "tris": summary.tris,
        "verts": len(verts),
        "bbox_min": [round(v, 2) for v in lo],
        "bbox_max": [round(v, 2) for v in hi],
        "size_cm": [round(hi[i] - lo[i], 1) for i in range(3)],
        "materials": len(summary.materials),
        "material_names": summary.materials,
        "color_layers": summary.color_layers,
        "emissive_corners": sum(1 for a in summary.corner_alphas if a <= EMISSIVE_ALPHA_MAX),
        "unit_scale": summary.unit_scale,
        "up_axis": summary.up_axis,
        "lcl_rotation": [round(v, 3) for v in model["rotation"]],
        "lcl_scaling": model["scaling"],
        "door_hits": door_clearance_hits(tris) if spec.door_clear else None,
        "models": len(summary.models),
    }


def check_kit(kit_id: str, module, root: Path) -> dict:
    assets = module.build()
    entries, failures = {}, []
    for asset in assets:
        spec = asset.spec
        path = fbx_path(module.KIT_NAME, spec.name, root)
        if not path.exists():
            entries[spec.name] = {"ok": False, "error": f"falta {path}"}
            failures.append(spec.name)
            continue
        try:
            measured = measure(path, spec)
        except (ValueError, KeyError, IndexError) as exc:
            entries[spec.name] = {"ok": False, "error": f"FBX ilegible: {exc}"}
            failures.append(spec.name)
            continue
        checks = evaluate(spec, measured)
        checks["single_model"] = measured["models"] == 1
        checks["matches_source_tris"] = measured["tris"] == asset.mesh.tri_count
        ok = all(checks.values())
        if not ok:
            failures.append(spec.name)
        entries[spec.name] = {
            "ok": ok, "category": spec.category, "budget": spec.budget, "pivot": spec.pivot, "notes": spec.notes,
            "checks": checks, "measured": measured,
        }
    tris = [e["measured"]["tris"] for e in entries.values() if "measured" in e]
    return {
        "kit": module.KIT_NAME, "seed": module.SEED, "meshes": len(assets), "failures": failures,
        "max_tris": max(tris) if tris else 0, "total_tris": sum(tris), "assets": entries,
    }


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--kits", default="all")
    parser.add_argument("--root", default=str(EXPORT_ROOT))
    args = parser.parse_args(argv)
    root = Path(args.root)
    report = {
        "fbx_convention": {"unit_scale": FBX_UNIT_SCALE, "ue_import_uniform_scale": 100,
                           "axes": "Blender -Z forward / Y up (nodo con Lcl Rotation -90 X); +X delante"},
        "emissive_marking": "alfa de Col = 0 en las caras emisivas (resto alfa = 1)",
        "kits": {},
    }
    failed = 0
    for kit_id, module in select(args.kits):
        result = check_kit(kit_id, module, root)
        report["kits"][kit_id] = result
        failed += len(result["failures"])
        status = "OK" if not result["failures"] else f"FALLOS: {result['failures']}"
        print(f"[check] {module.KIT_NAME}: {result['meshes']} mallas, máx. {result['max_tris']} tris — {status}")
    out = root / "kits_report.json"
    out.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"[check] informe: {out}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
