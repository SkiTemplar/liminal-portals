"""Hoja de contacto por kit a partir de los FBX exportados (no de la geometría en memoria).

Cada celda: el FBX reimportado, vista 3/4 desde +X/−Y con cámara ortográfica, EEVEE con color
de vértice, fondo neutro y una etiqueta con nombre, triángulos y medidas. Las caras emisivas (alfa 0
en Col) se marcan en la etiqueta. Salida: Art/Export/sheets/kit_<kit>.png.

    blender -b --factory-startup --python Tools/Art/Blender/contact_sheet.py -- [--kits all]
"""
from __future__ import annotations

import argparse
import math
import sys
import tempfile
from pathlib import Path

import bpy
import numpy as np
from mathutils import Matrix, Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))

import lp_blender  # noqa: E402
from kits import EXPORT_ROOT, fbx_path, select  # noqa: E402

TILE = 420
COLUMNS = 5
BACKGROUND = (0.45, 0.46, 0.47)
LABEL_COLOR = (0.02, 0.02, 0.02, 1.0)
VIEW_AZIMUTH, VIEW_ELEVATION = -38.0, 24.0
# El FBX trae su propio M_LowPoly sin nodos útiles: la vista previa usa uno que lee Col.
PREVIEW_MATERIAL = "M_SheetPreview"


def _enum_has(owner, prop: str, value: str) -> bool:
    return value in {i.identifier for i in owner.bl_rna.properties[prop].enum_items}


def setup_render(scene: bpy.types.Scene) -> None:
    """EEVEE con un sol y luz de cielo gris: colores de vértice fieles y facetas visibles."""
    engines = {i.identifier for i in scene.render.bl_rna.properties["engine"].enum_items}
    scene.render.engine = "BLENDER_EEVEE" if "BLENDER_EEVEE" in engines else sorted(engines)[0]
    sun_data = bpy.data.lights.new("Sun", "SUN")
    sun_data.energy = 3.2
    sun = bpy.data.objects.new("Sun", sun_data)
    sun.rotation_euler = (math.radians(50.0), math.radians(10.0), math.radians(-70.0))
    scene.collection.objects.link(sun)
    scene.render.resolution_x = scene.render.resolution_y = TILE
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = False
    scene.render.image_settings.file_format = "PNG"
    scene.view_settings.view_transform = "Standard"
    world = bpy.data.worlds.new("Neutral")
    if hasattr(world, "use_nodes") and not world.use_nodes:
        world.use_nodes = True
    background = next(n for n in world.node_tree.nodes if n.type == "BACKGROUND")
    background.inputs[0].default_value = (*BACKGROUND, 1.0)
    background.inputs[1].default_value = 1.0
    scene.world = world


def import_fbx(path: Path) -> bpy.types.Object:
    before = set(bpy.data.objects)
    if hasattr(bpy.ops.wm, "fbx_import"):
        bpy.ops.wm.fbx_import(filepath=str(path), import_colors="SRGB")
    else:
        bpy.ops.import_scene.fbx(filepath=str(path), colors_type="SRGB")
    new = [o for o in bpy.data.objects if o not in before and o.type == "MESH"]
    if len(new) != 1:
        raise RuntimeError(f"{path.name}: se esperaba una malla y llegaron {len(new)}")
    obj = new[0]
    attrs = obj.data.color_attributes
    if attrs.find(lp_blender.COLOR_ATTRIBUTE) < 0:
        raise RuntimeError(f"{path.name}: sin atributo Col tras importar")
    attrs.active_color = attrs[lp_blender.COLOR_ATTRIBUTE]
    obj.data.materials.clear()
    obj.data.materials.append(lp_blender.ensure_material(PREVIEW_MATERIAL))
    return obj


def emissive_faces(obj: bpy.types.Object) -> int:
    attr = obj.data.color_attributes[lp_blender.COLOR_ATTRIBUTE]
    values = np.empty(len(attr.data) * 4, dtype=np.float32)
    attr.data.foreach_get("color", values)
    alphas = values[3::4]
    if attr.domain != "CORNER":
        return int((alphas < 0.01).sum())
    count = 0
    for poly in obj.data.polygons:
        if all(alphas[i] < 0.01 for i in poly.loop_indices):
            count += 1
    return count


def world_bbox(obj: bpy.types.Object) -> tuple[Vector, Vector]:
    pts = [obj.matrix_world @ Vector(c) for c in obj.bound_box]
    lo = Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
    hi = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
    return lo, hi


def make_label(text_lines: list[str], camera: bpy.types.Object, ortho: float) -> bpy.types.Object:
    """Texto convertido a malla con Col propio, pegado a la parte baja del encuadre."""
    curve = bpy.data.curves.new("Label", "FONT")
    curve.body = "\n".join(text_lines)
    curve.size = ortho * 0.042
    curve.space_line = 1.1
    tmp = bpy.data.objects.new("LabelTmp", curve)
    bpy.context.scene.collection.objects.link(tmp)
    depsgraph = bpy.context.evaluated_depsgraph_get()
    mesh = bpy.data.meshes.new_from_object(tmp.evaluated_get(depsgraph))
    bpy.data.objects.remove(tmp)
    attr = mesh.color_attributes.new(lp_blender.COLOR_ATTRIBUTE, "BYTE_COLOR", "CORNER")
    attr.data.foreach_set("color_srgb", list(LABEL_COLOR) * len(attr.data))
    mesh.color_attributes.active_color = attr
    mesh.materials.append(lp_blender.ensure_material(PREVIEW_MATERIAL))
    label = bpy.data.objects.new("Label", mesh)
    bpy.context.scene.collection.objects.link(label)
    label.parent = camera
    label.matrix_parent_inverse = Matrix.Identity(4)
    label.location = (-ortho * 0.47, -ortho * 0.38, -1.0)
    return label


def render_tile(obj: bpy.types.Object, info: list[str], out: Path, elevation: float = VIEW_ELEVATION) -> None:
    scene = bpy.context.scene
    lo, hi = world_bbox(obj)
    center = (lo + hi) * 0.5
    az, el = math.radians(VIEW_AZIMUTH), math.radians(elevation)
    direction = Vector((math.cos(el) * math.cos(az), math.cos(el) * math.sin(az), math.sin(el)))
    size = max((hi - lo).length, 1e-3)
    cam_data = bpy.data.cameras.new("Cam")
    cam_data.type = "ORTHO"
    camera = bpy.data.objects.new("Cam", cam_data)
    scene.collection.objects.link(camera)
    camera.location = center + direction * size * 3.0
    camera.rotation_euler = (-direction).to_track_quat("-Z", "Y").to_euler()
    bpy.context.view_layer.update()
    inv = camera.matrix_world.inverted()
    corners = [inv @ (obj.matrix_world @ Vector(c)) for c in obj.bound_box]
    span_x = max(c.x for c in corners) - min(c.x for c in corners)
    span_y = max(c.y for c in corners) - min(c.y for c in corners)
    ortho = max(span_x, span_y * 1.25) * 1.12
    cam_data.ortho_scale = ortho
    shift_y = (max(c.y for c in corners) + min(c.y for c in corners)) * 0.5
    shift_x = (max(c.x for c in corners) + min(c.x for c in corners)) * 0.5
    camera.location = camera.matrix_world @ Vector((shift_x, shift_y - ortho * 0.08, 0.0))
    cam_data.clip_start = size * 0.01
    cam_data.clip_end = size * 10.0
    scene.camera = camera
    bpy.context.view_layer.update()
    label = make_label(info, camera, ortho)
    scene.render.filepath = str(out)
    bpy.ops.render.render(write_still=True)
    bpy.data.objects.remove(label)
    bpy.data.objects.remove(camera)


def compose(tiles: list[Path], out: Path) -> None:
    rows = math.ceil(len(tiles) / COLUMNS)
    sheet = np.zeros((rows * TILE, COLUMNS * TILE, 4), dtype=np.float32)
    sheet[..., :3] = 0.12
    sheet[..., 3] = 1.0
    for index, tile in enumerate(tiles):
        img = bpy.data.images.load(str(tile))
        px = np.array(img.pixels[:], dtype=np.float32).reshape(TILE, TILE, 4)
        bpy.data.images.remove(img)
        r, c = divmod(index, COLUMNS)
        y0 = (rows - 1 - r) * TILE
        sheet[y0 + 2:y0 + TILE - 2, c * TILE + 2:(c + 1) * TILE - 2] = px[2:-2, 2:-2]
    image = bpy.data.images.new("Sheet", COLUMNS * TILE, rows * TILE, alpha=True)
    image.pixels.foreach_set(sheet.ravel())
    image.filepath_raw = str(out)
    image.file_format = "PNG"
    image.save()


def sheet_for_kit(kit_id: str, module, root: Path, sheets_dir: Path) -> Path:
    scene = lp_blender.reset_scene()
    setup_render(scene)
    tiles: list[Path] = []
    tmp_dir = Path(tempfile.mkdtemp(prefix=f"kit_{kit_id}_"))
    for asset in module.build():
        path = fbx_path(module.KIT_NAME, asset.spec.name, root)
        obj = import_fbx(path)
        lo, hi = world_bbox(obj)
        size_cm = [(hi[i] - lo[i]) * 100.0 for i in range(3)]
        tris = sum(len(p.vertices) - 2 for p in obj.data.polygons)
        line2 = f"{tris} tris · {size_cm[0]:.0f}×{size_cm[1]:.0f}×{size_cm[2]:.0f} cm"
        glow = emissive_faces(obj)
        if glow:
            line2 += f" · emisivo {glow} caras"
        tile = tmp_dir / f"{asset.spec.name}.png"
        # Las piezas de techo se ven desde abajo, que es su cara vista.
        elevation = -VIEW_ELEVATION if asset.spec.pivot == "ceiling" else VIEW_ELEVATION
        render_tile(obj, [asset.spec.name, line2], tile, elevation)
        bpy.data.objects.remove(obj)
        tiles.append(tile)
    out = sheets_dir / f"kit_{kit_id}.png"
    compose(tiles, out)
    for tile in tiles:
        tile.unlink()
    tmp_dir.rmdir()
    print(f"[sheet] {out} ({len(tiles)} celdas)")
    return out


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--kits", default="all")
    parser.add_argument("--root", default=str(EXPORT_ROOT))
    args = parser.parse_args(argv)
    root = Path(args.root)
    sheets_dir = root / "sheets"
    sheets_dir.mkdir(parents=True, exist_ok=True)
    for kit_id, module in select(args.kits):
        sheet_for_kit(kit_id, module, root, sheets_dir)
    return 0


if __name__ == "__main__":
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    sys.exit(main(argv))
