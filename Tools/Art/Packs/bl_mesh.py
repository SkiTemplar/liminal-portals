"""Utilidades de Blender compartidas por ``inventory.py``, ``normalize.py``, ``check_export.py`` y
``contact_sheet.py``: importar ficheros de pack, leer el color original de cada cara, escribir
el atributo ``Col``, contar triángulos y exportar o leer FBX.

Solo se importan formatos de datos (OBJ, glTF/GLB, FBX). Nunca se abre un ``.blend`` de un
pack: podría llevar scripts embebidos.
"""

from __future__ import annotations

from collections.abc import Sequence
from pathlib import Path
from typing import Any, cast

import bpy

from packs_core import DATA_FORMATS, RGB, linear_to_srgb, rgb3

GREY: RGB = (0.8, 0.8, 0.8)


def require_version(major: int = 5, minor: int = 0) -> None:
    if tuple(bpy.app.version[:2]) < (major, minor):
        raise SystemExit(f"se necesita Blender >= {major}.{minor}, hay {bpy.app.version_string}")


def reset_scene() -> None:
    bpy.ops.wm.read_factory_settings(use_empty=True)


def mesh_of(obj: bpy.types.Object) -> bpy.types.Mesh:
    me = obj.data
    if not isinstance(me, bpy.types.Mesh):
        raise TypeError(f"{obj.name}: se esperaba una malla y es {type(me).__name__}")
    return me


def triangle_count(me: bpy.types.Mesh) -> int:
    return sum(p.loop_total - 2 for p in me.polygons)


def floats(prop: object) -> list[float]:
    return [float(x) for x in cast(Sequence[float], prop)]


def _run_importer(path: Path) -> None:
    ext = path.suffix.lower()
    if ext not in DATA_FORMATS:
        raise ValueError(f"formato no admitido (solo datos, nunca .blend): {path}")
    if ext in (".gltf", ".glb"):
        bpy.ops.import_scene.gltf(filepath=str(path))
    elif ext == ".fbx":
        bpy.ops.import_scene.fbx(filepath=str(path))
    else:
        bpy.ops.wm.obj_import(filepath=str(path))


def import_file(path: Path, name: str | None = None) -> bpy.types.Object:
    """Importa y une todas las mallas del fichero en un objeto con las transformaciones aplicadas."""
    before = set(bpy.data.objects)
    _run_importer(path)
    new = [o for o in bpy.data.objects if o not in before]
    new_names = [o.name for o in new]
    meshes = [o for o in new if o.type == "MESH"]
    if not meshes:
        raise RuntimeError(f"{path.name}: sin mallas")
    for o in meshes:
        for m in list(o.modifiers):
            o.modifiers.remove(m)
        if mesh_of(o).shape_keys:
            o.shape_key_clear()
    bpy.ops.object.select_all(action="DESELECT")
    for o in meshes:
        mw = o.matrix_world.copy()
        o.parent = None
        o.matrix_world = mw
        o.select_set(True)
    view_layer = bpy.context.view_layer
    view_layer.objects.active = meshes[0]
    if len(meshes) > 1:
        bpy.ops.object.join()
    obj = view_layer.objects.active
    if obj is None:
        raise RuntimeError(f"{path.name}: la unión no dejó objeto activo")
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    for n in new_names:
        o = bpy.data.objects.get(n)
        if o is not None and o != obj:
            bpy.data.objects.remove(o, do_unlink=True)
    obj.name = name or path.stem
    return obj


def material_color(mat: bpy.types.Material | None) -> RGB:
    """Color base del material en sRGB (los packs usados colorean por material, sin textura)."""
    if mat is None:
        return GREY
    if mat.node_tree is not None:
        for node in mat.node_tree.nodes:
            if node.type == "BSDF_PRINCIPLED":
                inp = node.inputs["Base Color"]
                col = cast(Any, inp).default_value
                return rgb3(linear_to_srgb(c) for c in col[:3])
    return rgb3(linear_to_srgb(c) for c in floats(mat.diffuse_color)[:3])


def has_texture(mat: bpy.types.Material | None) -> bool:
    if mat is None or mat.node_tree is None:
        return False
    return any(n.type == "TEX_IMAGE" for n in mat.node_tree.nodes)


def face_colors(obj: bpy.types.Object) -> list[RGB]:
    """Color sRGB original de cada polígono (color de su material)."""
    me = mesh_of(obj)
    cols = [material_color(m) for m in me.materials] or [GREY]
    return [cols[min(p.material_index, len(cols) - 1)] for p in me.polygons]


def write_color_attr(obj: bpy.types.Object, name: str, loop_colors: list[Sequence[float]]) -> None:
    """Atributo de color por esquina; ``loop_colors`` en lineal (Blender guarda FLOAT_COLOR lineal)."""
    me = mesh_of(obj)
    if len(loop_colors) != len(me.loops):
        raise ValueError(f"{obj.name}: {len(loop_colors)} colores para {len(me.loops)} esquinas")
    if name in me.color_attributes:
        me.color_attributes.remove(me.color_attributes[name])
    attr = me.color_attributes.new(name=name, type="FLOAT_COLOR", domain="CORNER")
    if not isinstance(attr, bpy.types.FloatColorAttribute):
        raise RuntimeError(f"{obj.name}: no se pudo crear el atributo de color {name}")
    flat: list[float] = []
    for c in loop_colors:
        flat.extend((c[0], c[1], c[2], 1.0))
    attr.data.foreach_set("color", flat)
    me.color_attributes.active_color = attr
    me.color_attributes.render_color_index = me.color_attributes.find(name)


def vertex_color_material(name: str = "M_LowPoly", layer: str = "Col") -> bpy.types.Material:
    """Material de vista previa que pinta el atributo ``layer`` (Unreal usa su propio M_LowPoly)."""
    mat = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    nt = mat.node_tree
    if nt is None:
        raise RuntimeError(f"{name} sin árbol de nodos")
    bsdf = next((n for n in nt.nodes if n.type == "BSDF_PRINCIPLED"), None)
    if bsdf is None:
        bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    if not any(n.type == "VERTEX_COLOR" for n in nt.nodes):
        vc = nt.nodes.new("ShaderNodeVertexColor")
        cast(Any, vc).layer_name = layer
        nt.links.new(vc.outputs["Color"], bsdf.inputs["Base Color"])
    cast(Any, bsdf.inputs["Roughness"]).default_value = 0.85
    return mat


def export_fbx(obj: bpy.types.Object, path: Path) -> None:
    """FBX en metros con los ajustes de Explored (Unreal: ``import_uniform_scale = 100``).

    ``colors_type="SRGB"``: el atributo ``Col`` (lineal en Blender) sale como sRGB, que es lo que
    Unreal guarda en ``FColor`` y lee ``M_LowPoly``.
    """
    path.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.fbx(
        filepath=str(path), check_existing=False, use_selection=True, global_scale=1.0,
        apply_unit_scale=True, apply_scale_options="FBX_SCALE_ALL", axis_forward="-Z", axis_up="Y",
        object_types={"MESH"}, use_mesh_modifiers=True, mesh_smooth_type="FACE", colors_type="SRGB",
        prioritize_active_color=True, use_triangles=True, use_tspace=False, bake_anim=False,
        path_mode="STRIP", embed_textures=False,
    )
