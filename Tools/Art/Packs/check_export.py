"""check_export.py — valida los FBX exportados por ``normalize.py`` y escribe el informe.

    blender -b --factory-startup --python Tools/Art/Packs/check_export.py

Por cada entrada de ``catalog.json`` reimporta ``Art/Export/<Bioma>/<mesh>.fbx`` y comprueba
(``packs_core.check_mesh``): triángulos ≤ presupuesto de la categoría, bounding box dentro de
su rango, z mínima = 0 ± 1 cm, pivote en el centro de la huella, un material, atributo ``Col``
presente y colores dentro de la paleta del bioma (descontado el degradado). También lee del
FBX en bruto la unidad (``UnitScaleFactor``) para documentar la escala de importación.

Escribe ``Art/Export/packs_report.json`` con estadísticas, fuente, licencia y errores por malla.
Sale con código 1 si alguna malla falla o falta.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

import bpy

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

from bl_mesh import floats, mesh_of, require_version, reset_scene, triangle_count  # noqa: E402
from packs_core import (  # noqa: E402
    DEFAULT_SPREAD,
    bbox_dims,
    check_mesh,
    flatten_palettes,
    linear_to_srgb,
    load_json,
    palette_distance,
    validate_catalog,
)

REPO = HERE.parents[2]
EXPORT = REPO / "Art" / "Export"
REPORT = EXPORT / "packs_report.json"


def fbx_unit_scale(path: Path) -> float | None:
    """``UnitScaleFactor`` de ``GlobalSettings`` (1 = cm, 100 = m) leído del FBX en bruto."""
    try:
        from io_scene_fbx import parse_fbx  # pyright: ignore[reportMissingImports]
    except ImportError:
        return None
    root, _version = parse_fbx.parse(str(path))
    for elem in root.elems:
        if elem.id != b"GlobalSettings":
            continue
        for sub in elem.elems:
            if sub.id != b"Properties70":
                continue
            for prop in sub.elems:
                if prop.props and prop.props[0] == b"UnitScaleFactor":
                    return float(prop.props[-1])
    return None


def mesh_stats(path: Path, swatches: list[str], spread: float) -> dict[str, Any]:
    reset_scene()
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=str(path))
    meshes = [o for o in bpy.data.objects if o not in before and o.type == "MESH"]
    if len(meshes) != 1:
        return {"error": f"{len(meshes)} mallas en el FBX (se espera 1)"}
    obj = meshes[0]
    bpy.context.view_layer.update()
    me = mesh_of(obj)
    vs = [obj.matrix_world @ v.co for v in me.vertices]
    dims = bbox_dims(vs)
    attr = me.color_attributes.get("Col")
    off = None
    if attr is not None:
        raw = [0.0] * (len(attr.data) * 4)
        attr.data.foreach_get("color", raw)
        uniq = {tuple(round(c, 4) for c in raw[i:i + 3]) for i in range(0, len(raw), 4)}
        srgb = [tuple(linear_to_srgb(c) for c in col) for col in uniq]
        off = max(palette_distance(c, swatches, spread) for c in srgb)
    return {
        "tris": triangle_count(me),
        "dims": [round(d, 3) for d in dims],
        "zmin": round(min(v.z for v in vs), 4),
        "center": [round((min(v.x for v in vs) + max(v.x for v in vs)) / 2, 4),
                   round((min(v.y for v in vs) + max(v.y for v in vs)) / 2, 4)],
        "materials": len([m for m in me.materials if m is not None]),
        "hasCol": attr is not None,
        "paletteMaxDE": None if off is None else round(off, 4),
        "flat": not any(p.use_smooth for p in me.polygons),
        "objectScale": [round(s, 4) for s in floats(obj.scale)],
        "fbxUnitScale": fbx_unit_scale(path),
    }


def main() -> int:
    require_version()
    catalog = load_json(HERE / "catalog.json")
    palettes = flatten_palettes(load_json(HERE / "palettes.json"))
    packs = {p["id"]: p for p in load_json(HERE / "packs.json")["packs"]}
    normalize_log = load_json(EXPORT / "packs_normalize.json") if (EXPORT / "packs_normalize.json").exists() else {}
    cat_errors = validate_catalog(catalog, palettes, set(packs))
    meshes: dict[str, Any] = {}
    failed = 0
    for e in catalog["entries"]:
        path = EXPORT / e["biome"] / f"{e['mesh']}.fbx"
        pack = packs[e["pack"]]
        row: dict[str, Any] = {
            "biome": e["biome"], "category": e["category"], "fbx": str(path.relative_to(REPO)).replace("\\", "/"),
            "source": {"pack": pack["id"], "file": e["file"], "page": pack["page"], "license": pack["license"]["spdx"]},
        }
        if not path.exists():
            row["errors"] = ["FBX no exportado"]
        else:
            stats = mesh_stats(path, list(palettes[e["biome"]].values()), float(e.get("gradient", DEFAULT_SPREAD)))
            row["stats"] = stats
            row["errors"] = [stats["error"]] if "error" in stats else check_mesh(stats, catalog["categories"][e["category"]])
            if "error" not in stats and not stats["flat"]:
                row["errors"].append("caras con suavizado (se pide flat shading)")
        if e["mesh"] in normalize_log:
            row["normalize"] = {k: normalize_log[e["mesh"]][k] for k in ("sourceTris", "decimateRatio", "palette")}
        failed += bool(row["errors"])
        status = "OK " if not row["errors"] else "ERR"
        print(f"CHECK {status} {e['mesh']:24} {row.get('stats', {}).get('tris', '-'):>5} t  "
              f"{row.get('stats', {}).get('dims', '-')}  {'; '.join(row['errors'])}")
        meshes[e["mesh"]] = row
    by_biome: dict[str, int] = {}
    for row in meshes.values():
        by_biome[row["biome"]] = by_biome.get(row["biome"], 0) + 1
    report = {
        "blender": bpy.app.version_string,
        "catalogErrors": cat_errors,
        "summary": {"meshes": len(meshes), "failed": failed, "byBiome": by_biome,
                    "maxTris": max((r.get("stats", {}).get("tris", 0) for r in meshes.values()), default=0)},
        "meshes": meshes,
    }
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"CHECK {len(meshes) - failed}/{len(meshes)} correctas, catálogo con {len(cat_errors)} errores -> {REPORT}")
    return 1 if failed or cat_errors else 0


if __name__ == "__main__":
    code = main()
    sys.stdout.flush()
    if code:
        sys.exit(code)
