"""Importa el arte low-poly exportado a FBX (Art/Export/<Kit>/SM_*.fbx) en /Game/Art/<Kit>/. Idempotente.

- Crea /Game/Art/Materials/M_LowPoly (opaco): BaseColor = Tint × Col.rgb^2,2 (el FBX trae el color de
  vértice en sRGB y Unreal lo guarda tal cual en el búfer de color: hay que pasarlo a lineal),
  Roughness por parámetro y Emissive = Emissive × Col.rgb^2,2 × (1 − Col.a) (alfa 0 marca las caras
  emisivas: difusor del panel fluorescente y cartel de salida). Instancias MI_LowPoly_* por uso.
- Importa cada FBX con su clave (sha256 del FBX + versión de los ajustes); si la clave guardada en los
  metadatos del asset coincide, no lo vuelve a importar ni a guardar (no ensucia git).
- Comprueba el bounding box importado contra Art/Export/packs_report.json y kits_report.json (±2 %).
  El importador de UE 5.6 sí aplica la unidad del FBX (UnitScaleFactor 100, metros): con
  import_uniform_scale = 100 las mallas salían 100 veces mayores, así que la escala es 1 y la
  comprobación del bounding box es la que manda. El paso a mano izquierda invierte Y.
- Colisión simple solo donde hace falta (troncos, rocas, bloques de hielo, granero); marcos y paredes
  exentas usan las cajas del blockout (ver docs/DECISIONES.md, D-32).

Uso: powershell -File Tools/content.ps1 -Mode Art (o All). Escribe Saved/Content/import_art.json.
"""

import hashlib
import json
import os
import re
import sys
import traceback

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import content_utils as cu  # noqa: E402 - necesita el sys.path de arriba

PROJECT_DIR = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
EXPORT_DIR = os.path.join(PROJECT_DIR, "Art", "Export")
RESULT_PATH = os.path.join(PROJECT_DIR, "Saved", "Content", "import_art.json")
ART_ROOT = "/Game/Art"
MATERIAL_FOLDER = f"{ART_ROOT}/Materials"
LOWPOLY_PATH = f"{MATERIAL_FOLDER}/M_LowPoly"

# Subir al cambiar los ajustes de importación, material o colisión: fuerza la reimportación.
IMPORT_VERSION = "4"
MATERIAL_VERSION = "1"
KEY_TAG = "LiminalArtKey"
IMPORT_SCALE = 1.0
BBOX_TOLERANCE = 0.02
# Margen absoluto en cm para las cotas cercanas a cero (rodapiés de 1,6 cm, enchufes).
BBOX_ABS_TOLERANCE = 0.5

# Instancias del material: rugosidad, emisivo y si son de dos caras (hojas finas, trigo).
MATERIAL_INSTANCES = {
    "MI_LowPoly": {"Roughness": 0.85, "Emissive": 0.0, "two_sided": False},
    "MI_LowPoly_Foliage": {"Roughness": 0.8, "Emissive": 0.0, "two_sided": True},
    "MI_LowPoly_Ice": {"Roughness": 0.3, "Emissive": 0.0, "two_sided": False},
    "MI_LowPoly_LightPanel": {"Roughness": 0.5, "Emissive": 6.0, "two_sided": False},
    "MI_LowPoly_ExitSign": {"Roughness": 0.5, "Emissive": 3.0, "two_sided": False},
}

# (patrón del nombre, instancia): el primero que casa manda.
MATERIAL_RULES = [
    (r"^SM_Hub_LightPanel_", "MI_LowPoly_LightPanel"),
    (r"^SM_Hub_ExitSign_", "MI_LowPoly_ExitSign"),
    (r"^SM_Jungle_(Fern|Flower)_|^SM_Jungle_Tree_05$|^SM_Crops_(Wheat|Corn|CropRow)_|^SM_Desert_Shrub_", "MI_LowPoly_Foliage"),
    (r"^SM_Ice_(Block|Shard|Iceberg|Serac|PressureRidge)_", "MI_LowPoly_Ice"),
    (r".", "MI_LowPoly"),
]

# Colisión simple: box = caja del bounding box; trunk = cápsula vertical en el tronco (radio y alto en
# cm); boxes = cajas a mano en fracciones del bounding box (el arco deja libre el hueco). En el commandlet
# la envolvente k-DOP y la descomposición convexa salen vacías (necesitan datos de render), así que las
# rocas y los bloques llevan la caja del bounding box.
ARCH_BOXES = (
    # (mín, máx) en fracciones del bounding box por eje: dos pilares y el dintel.
    ((0.0, 0.0, 0.0), (1.0, 0.33, 1.0)),
    ((0.0, 0.67, 0.0), (1.0, 1.0, 1.0)),
    ((0.0, 0.0, 0.65), (1.0, 1.0, 1.0)),
)
COLLISION_RULES = [
    (r"^SM_Jungle_Tree_", ("trunk", 35.0, 450.0)),
    (r"^SM_Farmland_Tree_", ("trunk", 25.0, 300.0)),
    (r"^SM_Desert_Arch_", ("boxes", ARCH_BOXES)),
    (r"^SM_Jungle_(Log|Roots)_|^SM_(Desert|Antarctic|Farmland)_Rock_|^SM_Ice_Block_", ("box",)),
    (r"^SM_Crops_Barn_|^SM_Ice_(Iceberg|Serac)_", ("box",)),
]

# Mallas abiertas por debajo o de una cara: distance field como si fueran de dos caras.
TWO_SIDED_DF = re.compile(r"^SM_Ice_SnowMound_|^SM_Hub_(Carpet|CeilingTile|LightPanel)_|^SM_Crops_(Wheat|Corn|CropRow)_|^SM_Jungle_(Fern|Flower)_")


def first_match(rules, name):
    for pattern, value in rules:
        if re.search(pattern, name):
            return value
    return None


def file_sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_expected():
    """Bounding box esperado (cm, convención de Blender: +Y sin invertir) y kit de cada malla."""
    expected = {}
    with open(os.path.join(EXPORT_DIR, "packs_report.json"), encoding="utf-8") as handle:
        packs = json.load(handle)
    for name, mesh in packs["meshes"].items():
        stats = mesh["stats"]
        dx, dy, dz = (d * 100.0 for d in stats["dims"])
        cx, cy = (c * 100.0 for c in stats["center"])
        zmin = stats["zmin"] * 100.0
        expected[name] = {"min": [cx - dx / 2, cy - dy / 2, zmin], "max": [cx + dx / 2, cy + dy / 2, zmin + dz],
                          "tris": stats["tris"], "fbx": mesh["fbx"]}
    with open(os.path.join(EXPORT_DIR, "kits_report.json"), encoding="utf-8") as handle:
        kits = json.load(handle)
    for kit in kits["kits"].values():
        for name, asset in kit["assets"].items():
            measured = asset["measured"]
            expected[name] = {"min": measured["bbox_min"], "max": measured["bbox_max"], "tris": measured["tris"], "fbx": None}
    return expected


def find_fbx_files():
    files = []
    for kit in sorted(os.listdir(EXPORT_DIR)):
        folder = os.path.join(EXPORT_DIR, kit)
        if not os.path.isdir(folder):
            continue
        for file_name in sorted(os.listdir(folder)):
            if file_name.startswith("SM_") and file_name.lower().endswith(".fbx"):
                files.append((kit, os.path.join(folder, file_name), file_name[:-4]))
    return files


def build_lowpoly_material():
    key = f"material-{MATERIAL_VERSION}"
    if cu.EAL.does_asset_exist(LOWPOLY_PATH):
        existing = cu.EAL.load_asset(LOWPOLY_PATH)
        if cu.EAL.get_metadata_tag(existing, KEY_TAG) == key:
            return existing, False
    material = cu.recreate_material(LOWPOLY_PATH)
    material.set_editor_property("used_with_instanced_static_meshes", True)
    vertex = cu.expr(material, unreal.MaterialExpressionVertexColor, -900, 0)
    linear = cu.expr(material, unreal.MaterialExpressionPower, -650, -100)
    linear.set_editor_property("const_exponent", 2.2)
    cu.connect(vertex, "", linear, "Base")
    tint = cu.vector_param(material, "Tint", (1.0, 1.0, 1.0), -650, -300)
    base = cu.expr(material, unreal.MaterialExpressionMultiply, -400, -200)
    cu.connect(linear, "", base, "A")
    cu.connect(tint, "", base, "B")
    cu.to_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)

    roughness = cu.scalar_param(material, "Roughness", 0.85, -400, 0)
    cu.to_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)

    # Emisivo solo en las caras con alfa 0: Emissive × color lineal × (1 − alfa).
    one_minus = cu.expr(material, unreal.MaterialExpressionOneMinus, -650, 200)
    cu.connect(vertex, "A", one_minus, "")
    emissive = cu.scalar_param(material, "Emissive", 0.0, -650, 350)
    mask = cu.expr(material, unreal.MaterialExpressionMultiply, -400, 250)
    cu.connect(one_minus, "", mask, "A")
    cu.connect(emissive, "", mask, "B")
    glow = cu.expr(material, unreal.MaterialExpressionMultiply, -200, 150)
    cu.connect(linear, "", glow, "A")
    cu.connect(mask, "", glow, "B")
    cu.to_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    cu.EAL.set_metadata_tag(material, KEY_TAG, key)
    cu.finish_material(material)
    return material, True


def build_material_instances(parent, parent_changed):
    instances = {}
    changed = []
    for name, spec in MATERIAL_INSTANCES.items():
        path = f"{MATERIAL_FOLDER}/{name}"
        key = f"mi-{MATERIAL_VERSION}-{json.dumps(spec, sort_keys=True)}"
        if not parent_changed and cu.EAL.does_asset_exist(path):
            existing = cu.EAL.load_asset(path)
            if cu.EAL.get_metadata_tag(existing, KEY_TAG) == key:
                instances[name] = existing
                continue
        instance = cu.ensure_material_instance(path, parent, {}, {"Roughness": spec["Roughness"], "Emissive": spec["Emissive"]})
        overrides = instance.get_editor_property("base_property_overrides")
        overrides.set_editor_property("override_two_sided", spec["two_sided"])
        overrides.set_editor_property("two_sided", spec["two_sided"])
        instance.set_editor_property("base_property_overrides", overrides)
        cu.EAL.set_metadata_tag(instance, KEY_TAG, key)
        cu.MEL.update_material_instance(instance)
        cu.require(cu.EAL.save_loaded_asset(instance, False), f"No se pudo guardar {path}")
        instances[name] = instance
        changed.append(path)
    return instances, changed


def import_fbx(fbx_path, destination):
    task = unreal.AssetImportTask()
    task.filename = fbx_path
    task.destination_path = destination
    task.automated = True
    task.replace_existing = True
    task.replace_existing_settings = True
    task.save = False
    options = unreal.FbxImportUI()
    options.import_mesh = True
    options.import_materials = False
    options.import_textures = False
    options.import_animations = False
    options.import_as_skeletal = False
    options.mesh_type_to_import = unreal.FBXImportType.FBXIT_STATIC_MESH
    data = options.static_mesh_import_data
    data.import_uniform_scale = IMPORT_SCALE
    data.combine_meshes = True
    data.auto_generate_collision = False
    data.generate_lightmap_u_vs = False
    data.build_nanite = False
    data.vertex_color_import_option = unreal.VertexColorImportOption.REPLACE
    data.normal_import_method = unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS
    task.options = options
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    paths = list(task.imported_object_paths)
    cu.require(paths, f"No se importó {fbx_path}")
    return cu.require(unreal.load_asset(paths[0]), f"No se pudo cargar {paths[0]}")


def mesh_subsystem():
    """El subsistema vive en el módulo StaticMeshEditor, que el commandlet no carga por sí solo; sin él,
    las funciones de EditorStaticMeshLibrary fallan en silencio (devuelven -1)."""
    unreal.load_module("StaticMeshEditor")
    return cu.require(unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem), "Falta StaticMeshEditorSubsystem")


def apply_collision(mesh, spec):
    """Sustituye la colisión simple por la de la regla; devuelve una descripción para el informe."""
    library = mesh_subsystem()
    library.remove_collisions(mesh)
    if spec is None:
        return "none"
    kind = spec[0]
    if kind == "box":
        library.add_simple_collisions(mesh, unreal.ScriptCollisionShapeType.BOX)
    elif kind == "boxes":
        low, high = measure(mesh)
        elements = []
        for frac_min, frac_max in spec[1]:
            lo = [low[a] + (high[a] - low[a]) * frac_min[a] for a in range(3)]
            hi = [low[a] + (high[a] - low[a]) * frac_max[a] for a in range(3)]
            element = unreal.KBoxElem()
            element.set_editor_property("center", unreal.Vector(*[(lo[a] + hi[a]) * 0.5 for a in range(3)]))
            element.set_editor_property("x", hi[0] - lo[0])
            element.set_editor_property("y", hi[1] - lo[1])
            element.set_editor_property("z", hi[2] - lo[2])
            elements.append(element)
        body = mesh.get_editor_property("body_setup")
        geometry = body.get_editor_property("agg_geom")
        geometry.set_editor_property("box_elems", elements)
        body.set_editor_property("agg_geom", geometry)
        mesh.set_editor_property("body_setup", body)
    elif kind == "trunk":
        radius, height = spec[1], spec[2]
        body = mesh.get_editor_property("body_setup")
        geometry = body.get_editor_property("agg_geom")
        capsule = unreal.KSphylElem()
        capsule.set_editor_property("center", unreal.Vector(0.0, 0.0, height * 0.5))
        capsule.set_editor_property("radius", radius)
        capsule.set_editor_property("length", max(height - 2.0 * radius, 1.0))
        geometry.set_editor_property("sphyl_elems", [capsule])
        body.set_editor_property("agg_geom", geometry)
        mesh.set_editor_property("body_setup", body)
    count = library.get_simple_collision_count(mesh)
    return f"{kind}:{count}"


def apply_build_settings(mesh, name):
    library = mesh_subsystem()
    settings = library.get_lod_build_settings(mesh, 0)
    settings.set_editor_property("generate_distance_field_as_if_two_sided", bool(TWO_SIDED_DF.search(name)))
    settings.set_editor_property("distance_field_resolution_scale", 1.0)
    settings.set_editor_property("generate_lightmap_u_vs", False)
    library.set_lod_build_settings(mesh, 0, settings)


def measure(mesh):
    box = mesh.get_bounding_box()
    return [box.min.x, box.min.y, box.min.z], [box.max.x, box.max.y, box.max.z]


def check_bbox(actual_min, actual_max, expected):
    """Compara con el informe invirtiendo Y (el importador pasa a mano izquierda)."""
    exp_min = [expected["min"][0], -expected["max"][1], expected["min"][2]]
    exp_max = [expected["max"][0], -expected["min"][1], expected["max"][2]]
    size = max(e_max - e_min for e_min, e_max in zip(exp_min, exp_max))
    worst = 0.0
    for axis in range(3):
        for actual, wanted in ((actual_min[axis], exp_min[axis]), (actual_max[axis], exp_max[axis])):
            error = abs(actual - wanted)
            if error > BBOX_ABS_TOLERANCE:
                worst = max(worst, error / size)
    return worst <= BBOX_TOLERANCE, worst


def process_mesh(kit, fbx_path, name, expected, instances):
    destination = f"{ART_ROOT}/{kit}"
    asset_path = f"{destination}/{name}"
    material_name = first_match(MATERIAL_RULES, name)
    collision = first_match(COLLISION_RULES, name)
    key = "|".join([IMPORT_VERSION, file_sha256(fbx_path), material_name, json.dumps(collision),
                    str(bool(TWO_SIDED_DF.search(name)))])
    entry = {"asset": asset_path, "material": material_name}
    mesh = None
    if cu.EAL.does_asset_exist(asset_path):
        mesh = cu.EAL.load_asset(asset_path)
        if mesh and cu.EAL.get_metadata_tag(mesh, KEY_TAG) == key:
            entry["action"] = "unchanged"
    if entry.get("action") != "unchanged":
        mesh = import_fbx(fbx_path, destination)
        mesh.set_material(0, instances[material_name])
        apply_build_settings(mesh, name)
        entry["collision"] = apply_collision(mesh, collision)
        cu.EAL.set_metadata_tag(mesh, KEY_TAG, key)
        cu.require(cu.EAL.save_loaded_asset(mesh, False), f"No se pudo guardar {asset_path}")
        entry["action"] = "imported"
    entry["collisionCount"] = mesh_subsystem().get_simple_collision_count(mesh)
    actual_min, actual_max = measure(mesh)
    entry["bboxMin"] = [round(v, 2) for v in actual_min]
    entry["bboxMax"] = [round(v, 2) for v in actual_max]
    if expected is None:
        entry["bboxOk"] = False
        entry["error"] = "sin entrada en los informes de Art/Export"
    else:
        ok, worst = check_bbox(actual_min, actual_max, expected)
        entry["bboxOk"] = ok
        entry["bboxWorstRelative"] = round(worst, 4)
        entry["reportTris"] = expected["tris"]
    entry["collisionOk"] = (collision is None) == (entry["collisionCount"] == 0)
    return entry


def write_result(result):
    os.makedirs(os.path.dirname(RESULT_PATH), exist_ok=True)
    with open(RESULT_PATH, "w", encoding="utf-8") as handle:
        json.dump(result, handle, indent=2, ensure_ascii=False)


def main():
    result = {"ok": False, "error": "", "meshes": {}, "changed": []}
    try:
        expected = load_expected()
        files = find_fbx_files()
        cu.require(files, f"No hay FBX en {EXPORT_DIR}: regenerarlos con Tools/Art (ver Tools/Art/*/README.md)")
        material, material_changed = build_lowpoly_material()
        if material_changed:
            result["changed"].append(LOWPOLY_PATH)
        instances, changed = build_material_instances(material, material_changed)
        result["changed"] += changed
        for kit, fbx_path, name in files:
            entry = process_mesh(kit, fbx_path, name, expected.get(name), instances)
            result["meshes"][name] = entry
            if entry["action"] == "imported":
                result["changed"].append(entry["asset"])
        bad = [name for name, entry in result["meshes"].items() if not entry["bboxOk"] or not entry["collisionOk"]]
        missing = sorted(set(expected) - set(result["meshes"]))
        result["count"] = len(result["meshes"])
        result["missing"] = missing
        result["failed"] = bad
        result["ok"] = not bad and not missing
        if not result["ok"]:
            result["error"] = f"bounding box o colisión fuera de tolerancia: {bad}; sin FBX: {missing}"
        unreal.log(f"[LiminalArt] {result['count']} mallas, {len(result['changed'])} assets cambiados, fallos: {bad}, sin FBX: {missing}")
    except Exception as error:  # noqa: BLE001 - se registra y se refleja en el JSON de resultado
        result["error"] = f"{type(error).__name__}: {error}"
        unreal.log_error(f"[LiminalArt] {traceback.format_exc()}")
    write_result(result)


main()
