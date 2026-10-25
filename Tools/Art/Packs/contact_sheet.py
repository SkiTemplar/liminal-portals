"""contact_sheet.py — hoja de contacto por bioma de los FBX exportados.

    blender -b --factory-startup --python Tools/Art/Packs/contact_sheet.py -- [--biome Desert]

Importa ``Art/Export/<Bioma>/*.fbx`` (los del catálogo, en su orden) y renderiza con Workbench,
fondo neutro y luz de estudio:

- ``Art/Export/sheets/packs_<bioma>.png``: todas en fila, cada una encajada a la misma altura,
  con nombre, medidas reales y triángulos debajo (para juzgar forma y color).
- ``Art/Export/sheets/packs_<bioma>_escala.png``: las mismas a escala real junto a una figura
  de 1,8 m (para juzgar proporciones entre mallas).
"""

from __future__ import annotations

import sys
from pathlib import Path

import bpy

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

from bl_mesh import mesh_of, require_version, reset_scene, triangle_count  # noqa: E402
from bl_sheet import render_row  # noqa: E402
from packs_core import BIOMES, load_json, parse_args  # noqa: E402

REPO = HERE.parents[2]
EXPORT = REPO / "Art" / "Export"
SHEETS = EXPORT / "sheets"


def import_fbx(path: Path) -> bpy.types.Object:
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=str(path))
    meshes = [o for o in bpy.data.objects if o not in before and o.type == "MESH"]
    if len(meshes) != 1:
        raise RuntimeError(f"{path.name}: {len(meshes)} mallas (se espera 1)")
    obj = meshes[0]
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    return obj


def sheet(biome: str, meshes: list[str], fit: bool) -> Path:
    reset_scene()
    items = []
    for name in meshes:
        obj = import_fbx(EXPORT / biome / f"{name}.fbx")
        d = obj.dimensions
        label = f"{name.removeprefix('SM_')}\n{d.x:.1f}x{d.y:.1f}x{d.z:.1f} m  {triangle_count(mesh_of(obj))} t"
        items.append((obj, label if fit else name.removeprefix(f"SM_{biome}_")))
    suffix = "" if fit else "_escala"
    out = SHEETS / f"packs_{biome.lower()}{suffix}.png"
    return render_row(items, out, fit=fit, px_per_item=320 if fit else 240)


def main() -> None:
    require_version()
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    args = parse_args(argv)
    catalog = load_json(HERE / "catalog.json")
    for biome in [args["biome"]] if args["biome"] else list(BIOMES):
        meshes = [e["mesh"] for e in catalog["entries"] if e["biome"] == biome]
        missing = [m for m in meshes if not (EXPORT / biome / f"{m}.fbx").exists()]
        if missing:
            raise SystemExit(f"{biome}: faltan FBX {missing}; ejecuta normalize.py")
        for fit in (True, False):
            print(f"SHEET {sheet(biome, meshes, fit)}")


if __name__ == "__main__":
    main()
