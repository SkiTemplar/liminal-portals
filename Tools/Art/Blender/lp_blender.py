"""Utilidades de Blender comunes a los kits: escena en metros, malla con atributo Col, material único,
flat shading y exportación FBX para Unreal.

Convención de exportación (la misma que Tools/Art/Packs y Explored): la geometría pura está en cm,
en Blender se pasa a metros y el FBX sale en metros (UnitScaleFactor = 100). El importador de UE 5.6
ignora la unidad del FBX: importar con import_uniform_scale = 100 y comprobar el bounding box.
"""
from __future__ import annotations

import bpy

from lp_geom import Mesh, corner_colors

MIN_BLENDER = (5, 0, 0)
CM_TO_M = 0.01
MATERIAL_NAME = "M_LowPoly"
COLOR_ATTRIBUTE = "Col"


def require_version() -> None:
    if bpy.app.version < MIN_BLENDER:
        raise RuntimeError(f"hace falta Blender ≥ {MIN_BLENDER}, está {bpy.app.version_string}")


def _enum_ids(rna_owner, prop: str) -> list[str]:
    return [item.identifier for item in rna_owner.bl_rna.properties[prop].enum_items]


def reset_scene() -> bpy.types.Scene:
    """Escena vacía en metros (1 unidad de Blender = 1 m)."""
    require_version()
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    units = scene.unit_settings
    units.system = "METRIC" if "METRIC" in _enum_ids(units, "system") else units.system
    units.scale_length = 1.0
    if "METERS" in _enum_ids(units, "length_unit"):
        units.length_unit = "METERS"
    return scene


def _node_by_type(nodes, node_type: str):
    return next((n for n in nodes if n.type == node_type), None)


def ensure_material(name: str = MATERIAL_NAME) -> bpy.types.Material:
    """Material único compartido: color de vértice Col → Base Color (sustituto de M_LowPoly en UE)."""
    mat = bpy.data.materials.get(name)
    if mat is not None:
        return mat
    mat = bpy.data.materials.new(name)
    if hasattr(mat, "use_nodes") and not mat.use_nodes:
        mat.use_nodes = True
    nodes = mat.node_tree.nodes
    bsdf = _node_by_type(nodes, "BSDF_PRINCIPLED") or nodes.new("ShaderNodeBsdfPrincipled")
    attr = nodes.new("ShaderNodeVertexColor")
    attr.layer_name = COLOR_ATTRIBUTE
    mat.node_tree.links.new(attr.outputs[0], bsdf.inputs[0])
    roughness = bsdf.inputs.get("Roughness")
    if roughness is not None:
        roughness.default_value = 0.85
    return mat


def mesh_to_object(name: str, mesh: Mesh, gradient: float, collection=None) -> bpy.types.Object:
    """Crea el objeto con caras planas, atributo Col (BYTE_COLOR por esquina, sRGB) y M_LowPoly."""
    data = bpy.data.meshes.new(name)
    data.from_pydata([[c * CM_TO_M for c in v] for v in mesh.verts], [], [list(f) for f in mesh.faces])
    if data.validate(clean_customdata=False):
        raise RuntimeError(f"{name}: la malla tenía datos no válidos (caras duplicadas o degeneradas)")
    if len(data.polygons) != len(mesh.faces):
        raise RuntimeError(f"{name}: Blender descartó caras ({len(data.polygons)} de {len(mesh.faces)})")
    data.shade_flat()
    attr = data.color_attributes.new(COLOR_ATTRIBUTE, "BYTE_COLOR", "CORNER")
    flat = [c for color in corner_colors(mesh, gradient) for c in color]
    attr.data.foreach_set("color_srgb", flat)
    data.color_attributes.active_color = attr
    data.color_attributes.render_color_index = data.color_attributes.find(COLOR_ATTRIBUTE)
    data.materials.append(ensure_material())
    obj = bpy.data.objects.new(name, data)
    (collection or bpy.context.scene.collection).objects.link(obj)
    return obj


def export_fbx(obj: bpy.types.Object, filepath: str) -> None:
    """Exporta solo `obj`: triángulos, normales por cara, color sRGB, en metros (UnitScaleFactor 100)."""
    for other in bpy.context.scene.objects:
        other.select_set(False)
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.fbx(
        filepath=filepath, check_existing=False, use_selection=True, global_scale=1.0,
        apply_unit_scale=True, apply_scale_options="FBX_SCALE_ALL", axis_forward="-Z", axis_up="Y",
        object_types={"MESH"}, use_mesh_modifiers=True, mesh_smooth_type="FACE", colors_type="SRGB",
        prioritize_active_color=True, use_triangles=True, use_tspace=False, bake_anim=False,
        path_mode="STRIP", embed_textures=False, use_metadata=False,
    )
