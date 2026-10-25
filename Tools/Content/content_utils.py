"""Utilidades del editor de Unreal para crear contenido por script (build_content.py): materiales
idempotentes, instancias de material, actores y comprobaciones. Solo se importa dentro del editor.
"""

import unreal

MEL = unreal.MaterialEditingLibrary
ASSET_TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary

BASIC_SHAPES = {
    "cube": "/Engine/BasicShapes/Cube",
    "sphere": "/Engine/BasicShapes/Sphere",
    "cylinder": "/Engine/BasicShapes/Cylinder",
    "cone": "/Engine/BasicShapes/Cone",
}


def require(value, message):
    if value is None or value is False:
        raise RuntimeError(message)
    return value


def recreate_material(path: str) -> unreal.Material:
    """Reutiliza el material si existe (vaciando su grafo) para no romper referencias; si no, lo crea."""
    if EAL.does_asset_exist(path):
        material = require(EAL.load_asset(path), f"No se pudo cargar {path}")
        MEL.delete_all_material_expressions(material)
        return material
    folder, name = path.rsplit("/", 1)
    material = ASSET_TOOLS.create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
    return require(material, f"No se pudo crear {path}")


def expr(material, cls, x: int, y: int):
    return require(MEL.create_material_expression(material, cls, x, y), f"No se pudo crear {cls.__name__}")


def connect(src, src_out: str, dst, dst_in: str):
    require(MEL.connect_material_expressions(src, src_out, dst, dst_in),
            f"No se pudo conectar {src.get_name()}.{src_out} -> {dst.get_name()}.{dst_in}")


def to_property(src, src_out: str, prop):
    require(MEL.connect_material_property(src, src_out, prop), f"No se pudo conectar {src.get_name()}.{src_out} -> {prop}")


def vector_param(material, name: str, color, x: int, y: int):
    node = expr(material, unreal.MaterialExpressionVectorParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", unreal.LinearColor(color[0], color[1], color[2], 1.0))
    return node


def scalar_param(material, name: str, value: float, x: int, y: int):
    node = expr(material, unreal.MaterialExpressionScalarParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", value)
    return node


def sampler_type_for(texture) -> unreal.MaterialSamplerType:
    """Tipo de muestreador que exige la validación del material para la textura por defecto."""
    srgb = texture.get_editor_property("srgb")
    return unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if srgb else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR


def finish_material(material):
    MEL.recompile_material(material)
    require(EAL.save_loaded_asset(material, False), f"No se pudo guardar {material.get_path_name()}")
    unreal.log(f"[LiminalContent] Material listo: {material.get_path_name()}")


def ensure_material_instance(path: str, parent, vectors: dict, scalars: dict):
    """Instancia de material con esos parámetros; si existe, se reutiliza y se sobrescriben."""
    if EAL.does_asset_exist(path):
        instance = require(EAL.load_asset(path), f"No se pudo cargar {path}")
    else:
        folder, name = path.rsplit("/", 1)
        instance = ASSET_TOOLS.create_asset(name, folder, unreal.MaterialInstanceConstant,
                                            unreal.MaterialInstanceConstantFactoryNew())
        require(instance, f"No se pudo crear {path}")
    MEL.set_material_instance_parent(instance, parent)
    MEL.clear_all_material_instance_parameters(instance)
    for name, color in vectors.items():
        MEL.set_material_instance_vector_parameter_value(instance, name, unreal.LinearColor(color[0], color[1], color[2], 1.0))
    for name, value in scalars.items():
        MEL.set_material_instance_scalar_parameter_value(instance, name, value)
    MEL.update_material_instance(instance)
    require(EAL.save_loaded_asset(instance, False), f"No se pudo guardar {path}")
    return instance


def load_class(path: str):
    return require(unreal.load_class(None, path), f"No se encuentra la clase {path}")


def spawn(actor_subsystem, cls, location, label: str, yaw: float = 0.0):
    actor = actor_subsystem.spawn_actor_from_class(cls, location, unreal.Rotator(0.0, 0.0, yaw))
    require(actor, f"No se pudo colocar {label}")
    actor.set_actor_label(label)
    return actor


def spawn_shape(actor_subsystem, shape: str, location, scale, material, label: str, yaw: float = 0.0):
    """StaticMeshActor con una forma básica del motor (100 cm de lado, centrada) y su material."""
    mesh = require(unreal.load_asset(BASIC_SHAPES[shape]), f"Falta la forma {shape}")
    actor = spawn(actor_subsystem, unreal.StaticMeshActor, location, label, yaw)
    component = actor.get_editor_property("static_mesh_component")
    component.set_static_mesh(mesh)
    component.set_material(0, material)
    actor.set_actor_scale3d(unreal.Vector(*scale))
    return actor
