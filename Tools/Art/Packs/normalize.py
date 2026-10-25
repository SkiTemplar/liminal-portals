"""normalize.py — normaliza las mallas de ``catalog.json`` y exporta los FBX.

    blender -b --factory-startup --python Tools/Art/Packs/normalize.py -- [--biome Jungle] [--meshes SM_X ...]

Por cada entrada:

1. Importa el fichero del pack desde la caché (``Art/Packs/<pack>/<file>``, la deja
   ``fetch_packs.py``). Solo formatos de datos; nunca se abre un ``.blend`` del pack.
2. Une las mallas, aplica ``rotateDeg`` y ``stretch`` y escala uniforme a ``size``.
3. Decima (colapso) si pasa del presupuesto de su categoría, con un 3 % de margen.
4. Pivote en el centro de la huella con z mínima = 0; +X delante, Z arriba.
5. Recolorea: cada cara toma la muestra de la regla más cercana en Oklab (``packs_core``) y
   cada esquina el color de esa muestra con el degradado vertical suave del objeto. Se guarda
   en el atributo ``Col`` y sale en el FBX como sRGB.
6. Flat shading (normales por cara), un solo material ``M_LowPoly`` y FBX en
   ``Art/Export/<Bioma>/<mesh>.fbx`` (metros; en Unreal ``import_uniform_scale = 100``).

Resumen por malla en ``Art/Export/packs_normalize.json`` (lo lee ``check_export.py``).
"""

from __future__ import annotations

import json
import math
import sys
from pathlib import Path
from typing import Any

import bpy
from mathutils import Euler, Matrix

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

from bl_mesh import (  # noqa: E402
    export_fbx,
    face_colors,
    import_file,
    mesh_of,
    require_version,
    reset_scene,
    triangle_count,
    vertex_color_material,
    write_color_attr,
)
from fetch_packs import cache_dir  # noqa: E402
from packs_core import (  # noqa: E402
    DEFAULT_SPREAD,
    base_pivot,
    compile_rules,
    decimate_ratio,
    flatten_palettes,
    gradient_srgb,
    load_json,
    parse_args,
    pick_swatch,
    share_percent,
    size_factor,
    srgb_to_linear,
    validate_catalog,
)

REPO = HERE.parents[2]
EXPORT = REPO / "Art" / "Export"
SUMMARY = EXPORT / "packs_normalize.json"


def transform(obj: bpy.types.Object, m: Matrix) -> None:
    me = mesh_of(obj)
    me.transform(m)
    me.update()


def orient_and_scale(obj: bpy.types.Object, entry: dict[str, Any]) -> float:
    rot = entry.get("rotateDeg") or [0, 0, 0]
    transform(obj, Euler([math.radians(a) for a in rot], "XYZ").to_matrix().to_4x4())
    if entry.get("stretch"):
        transform(obj, Matrix.Diagonal((*entry["stretch"], 1.0)))
    s = size_factor([v.co for v in mesh_of(obj).vertices], entry["size"])
    transform(obj, Matrix.Scale(s, 4))
    return s


def to_pivot(obj: bpy.types.Object) -> None:
    origin = base_pivot([v.co for v in mesh_of(obj).vertices])
    transform(obj, Matrix.Translation([-c for c in origin]))


def decimate(obj: bpy.types.Object, budget: int) -> float | None:
    """Colapso hasta caber en ``budget``; devuelve la proporción aplicada (None si no hizo falta)."""
    ratio = decimate_ratio(triangle_count(mesh_of(obj)), budget)
    if ratio is None:
        return None
    mod = obj.modifiers.new("Decimate", "DECIMATE")
    kinds = mod.bl_rna.properties["decimate_type"].enum_items.keys()
    if "COLLAPSE" not in kinds:
        raise RuntimeError(f"Decimate sin modo COLLAPSE en esta versión: {list(kinds)}")
    mod.decimate_type = "COLLAPSE"  # pyright: ignore[reportAttributeAccessIssue]
    mod.ratio = ratio  # pyright: ignore[reportAttributeAccessIssue]
    mod.use_collapse_triangulate = True  # pyright: ignore[reportAttributeAccessIssue]
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.modifier_apply(modifier=mod.name)
    if triangle_count(mesh_of(obj)) > budget:
        raise RuntimeError(f"{obj.name}: {triangle_count(mesh_of(obj))} triángulos tras decimar a {ratio:.3f}")
    return ratio


def recolor(obj: bpy.types.Object, entry: dict[str, Any], palette: dict[str, str]) -> dict[str, float]:
    compiled, tol, default = compile_rules(entry["recolor"])
    spread = float(entry.get("gradient", DEFAULT_SPREAD))
    me = mesh_of(obj)
    cols = face_colors(obj)
    zs = [v.co.z for v in me.vertices]
    zmin, span = min(zs), max(max(zs) - min(zs), 1e-6)
    share: dict[str, float] = {}
    loop_cols: list[list[float]] = [[0.0, 0.0, 0.0]] * len(me.loops)
    for p, c in zip(me.polygons, cols, strict=True):
        swatch = pick_swatch(c, compiled, tol, default, entry["mesh"])
        share[swatch] = share.get(swatch, 0.0) + p.area
        for li in p.loop_indices:
            h = (me.vertices[me.loops[li].vertex_index].co.z - zmin) / span
            loop_cols[li] = [srgb_to_linear(x) for x in gradient_srgb(palette[swatch], h, spread)]
    for name in [a.name for a in me.color_attributes]:
        me.color_attributes.remove(me.color_attributes[name])
    write_color_attr(obj, "Col", loop_cols)
    me.materials.clear()
    me.materials.append(vertex_color_material())
    for p in me.polygons:
        p.material_index = 0
    return share_percent(share)


def flat_shade(obj: bpy.types.Object) -> None:
    me = mesh_of(obj)
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    if me.has_custom_normals:
        bpy.ops.mesh.customdata_custom_splitnormals_clear()
    me.shade_flat()


def normalize(entry: dict[str, Any], catalog: dict[str, Any], palettes: dict[str, dict[str, str]]) -> dict[str, Any]:
    src = cache_dir() / entry["pack"] / entry["file"]
    if not src.exists():
        raise SystemExit(f"falta {src}: ejecuta `uv run python fetch_packs.py {entry['pack']}` en Tools/Art/Packs")
    reset_scene()
    obj = import_file(src, name=entry["mesh"])
    src_tris = triangle_count(mesh_of(obj))
    scale = orient_and_scale(obj, entry)
    ratio = decimate(obj, catalog["categories"][entry["category"]]["budget"])
    to_pivot(obj)
    share = recolor(obj, entry, palettes[entry["biome"]])
    flat_shade(obj)
    out = EXPORT / entry["biome"] / f"{entry['mesh']}.fbx"
    export_fbx(obj, out)
    dims = [round(d, 3) for d in obj.dimensions]
    tris = triangle_count(mesh_of(obj))
    print(f"NORM {entry['mesh']:24} dims={dims} tris={src_tris}->{tris} share={share}")
    return {"fbx": str(out.relative_to(REPO)).replace("\\", "/"), "sourceTris": src_tris, "tris": tris,
            "decimateRatio": None if ratio is None else round(ratio, 4), "packScale": round(scale, 5),
            "dims": dims, "palette": share}


def main() -> None:
    require_version()
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    args = parse_args(argv)
    catalog = load_json(HERE / "catalog.json")
    palettes = flatten_palettes(load_json(HERE / "palettes.json"))
    packs = {p["id"] for p in load_json(HERE / "packs.json")["packs"]}
    errors = validate_catalog(catalog, palettes, packs)
    if errors:
        raise SystemExit("catálogo no válido:\n  " + "\n  ".join(errors))
    entries = [e for e in catalog["entries"] if args["biome"] in (None, e["biome"])]
    if args["meshes"]:
        entries = [e for e in entries if e["mesh"] in args["meshes"]]
    if not entries:
        raise SystemExit("sin entradas para ese bioma o esas mallas")
    summary = load_json(SUMMARY) if SUMMARY.exists() else {}
    for e in entries:
        summary[e["mesh"]] = normalize(e, catalog, palettes)
    SUMMARY.parent.mkdir(parents=True, exist_ok=True)
    SUMMARY.write_text(json.dumps(dict(sorted(summary.items())), ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"NORM {len(entries)} mallas -> {EXPORT}")


if __name__ == "__main__":
    main()
