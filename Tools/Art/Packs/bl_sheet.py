"""Hoja de contacto en Blender (Workbench): mallas en fila sobre fondo neutro, luz de estudio
simple y el nombre de cada una debajo. Lo usan ``inventory.py`` (originales del pack) y
``contact_sheet.py`` (FBX exportados).

``fit=True`` encaja cada malla en una celda de la misma altura (para ver la forma; la etiqueta
lleva la medida real). ``fit=False`` las deja a escala real junto a una figura de 1,8 m.
"""

from __future__ import annotations

import math
from pathlib import Path

import bpy
from mathutils import Euler, Vector

from bl_mesh import mesh_of, write_color_attr

CELL_M = 2.0
LABEL_COLOR = (0.02, 0.02, 0.02)
BACKGROUND = (0.42, 0.44, 0.46)
VIEW_DIR = Vector((0.12, -1.0, 0.28)).normalized()
PERSON_M = 1.8
TURN_DEG = -35.0


def _setup_render(width: int, height: int, path: Path) -> None:
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    sh = scene.display.shading
    sh.light = "STUDIO"
    sh.color_type = "VERTEX"
    sh.show_cavity = False
    sh.show_shadows = False
    sh.show_object_outline = False
    sh.background_type = "VIEWPORT"
    sh.background_color = BACKGROUND
    scene.render.resolution_x = width
    scene.render.resolution_y = height
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = False
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGB"
    scene.view_settings.view_transform = "Standard"
    scene.render.filepath = str(path)


def _label(text: str, location: Vector, size: float, rotation: Euler) -> bpy.types.Object:
    curve = bpy.data.curves.new(f"lbl_{text[:20]}", type="FONT")
    curve.body = text  # pyright: ignore[reportAttributeAccessIssue] -- TextCurve
    curve.size = size  # pyright: ignore[reportAttributeAccessIssue]
    curve.align_x = "CENTER"  # pyright: ignore[reportAttributeAccessIssue]
    obj = bpy.data.objects.new(curve.name, curve)
    bpy.context.scene.collection.objects.link(obj)
    obj.location = location
    obj.rotation_euler = rotation
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.convert(target="MESH")
    conv = bpy.context.view_layer.objects.active
    if conv is None:
        raise RuntimeError("no se pudo convertir la etiqueta a malla")
    write_color_attr(conv, "Col", [LABEL_COLOR] * len(mesh_of(conv).loops))
    return conv


def _person(x: float) -> bpy.types.Object:
    bpy.ops.mesh.primitive_cylinder_add(vertices=8, radius=0.25, depth=PERSON_M, location=(x, 0, PERSON_M / 2))
    obj = bpy.context.view_layer.objects.active
    if obj is None:
        raise RuntimeError("no se pudo crear la figura de escala")
    write_color_attr(obj, "Col", [(0.6, 0.1, 0.5)] * len(mesh_of(obj).loops))
    return obj


def _bounds(objs: list[bpy.types.Object]) -> tuple[Vector, Vector]:
    bpy.context.view_layer.update()
    pts = [o.matrix_world @ Vector(c) for o in objs for c in o.bound_box]
    lo = Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
    hi = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
    return lo, hi


def _layout(items: list[tuple[bpy.types.Object, str]], fit: bool) -> tuple[list[bpy.types.Object], float]:
    """Coloca las mallas en fila a lo largo de X; devuelve todos los objetos y la altura de letra."""
    placed: list[bpy.types.Object] = []
    x = 0.0
    heights: list[float] = []
    yaw = math.radians(TURN_DEG)
    for obj, _ in items:
        dims = obj.dimensions.copy()
        s = CELL_M / max(dims.z, max(dims.x, dims.y) * 0.75, 1e-6) if fit else 1.0
        obj.scale = (s, s, s)
        # Giro de presentación: +X (delante) a tres cuartos, para ver frente y costado.
        obj.rotation_euler = (0.0, 0.0, yaw)
        width = (abs(dims.x * math.cos(yaw)) + abs(dims.y * math.sin(yaw))) * s
        if fit:
            width = max(width, 1.15 * CELL_M)
        x += width / 2
        obj.location = (x, 0.0, 0.0)
        x += width / 2 + (0.35 * CELL_M if fit else max(0.6, 0.15 * width))
        placed.append(obj)
        heights.append(dims.z * s)
    letter = 0.11 * CELL_M if fit else max(0.25, 0.04 * max(heights))
    return placed, letter


def render_row(items: list[tuple[bpy.types.Object, str]], path: Path, *, fit: bool = True,
               px_per_item: int = 300, height_px: int = 520) -> Path:
    """Renderiza ``items`` (objeto, etiqueta) en fila y guarda el PNG en ``path``."""
    if not items:
        raise ValueError("hoja de contacto sin mallas")
    placed, letter = _layout(items, fit)
    extra: list[bpy.types.Object] = []
    if not fit:
        lo, hi = _bounds(placed)
        extra.append(_person(hi.x + 1.0))
    rot = (-VIEW_DIR).to_track_quat("-Z", "Y").to_euler()
    for obj, (_, text) in zip(placed, items, strict=True):
        lines = text.split("\n")
        for i, line in enumerate(lines):
            loc = Vector((obj.location.x, -0.9 * CELL_M if fit else -1.0, -(i + 1) * letter * 1.5))
            extra.append(_label(line, loc, letter, rot))
    lo, hi = _bounds(placed + extra)
    center = (lo + hi) / 2
    cam_data = bpy.data.cameras.new("SheetCam")
    cam_data.type = "ORTHO"
    cam_data.clip_end = 10000.0
    cam = bpy.data.objects.new("SheetCam", cam_data)
    bpy.context.scene.collection.objects.link(cam)
    bpy.context.scene.camera = cam
    span = (hi - lo).length
    cam.location = center + VIEW_DIR * span * 2
    cam.rotation_euler = rot
    bpy.context.view_layer.update()
    inv = cam.matrix_world.inverted()
    pts = [inv @ (o.matrix_world @ Vector(c)) for o in placed + extra for c in o.bound_box]
    x0, x1 = min(p.x for p in pts), max(p.x for p in pts)
    y0, y1 = min(p.y for p in pts), max(p.y for p in pts)
    w, h = x1 - x0, y1 - y0
    cam.location = cam.location + cam.matrix_world.to_3x3() @ Vector(((x0 + x1) / 2, (y0 + y1) / 2, 0.0))
    width_px = max(px_per_item * len(items), 800)
    aspect = width_px / height_px
    cam_data.ortho_scale = max(w, h * aspect) * 1.06
    _setup_render(width_px, max(height_px, int(math.ceil(width_px / max(aspect, 1e-6)))), path)
    path.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.render.render(write_still=True)
    return path
