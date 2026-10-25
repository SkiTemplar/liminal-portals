"""Crea el contenido de LiminalPortals por script (etapa E2 de docs/PLAN-NOCHE.md). Idempotente.

- /Game/Portal/Materials/M_PortalSurface: unlit, two-sided; emisivo = TextureSampleParameter2D
  «PortalTexture» muestreado en ScreenPosition.ViewportUV (el render target del portal, en HDR lineal).
- /Game/Portal/Materials/M_PortalFallback: unlit, color liminal plano (reserva visible solo en capturas).
- /Game/Common/Materials/M_Basic: lit con parámetros Color, Roughness y Emissive, e instancias
  MI_Calib_* con la paleta de calibración.
- /Game/Maps/L_Calibration: sala abierta con geometría de colores variados, sol, SkyAtmosphere,
  SkyLight, post-process global con exposición manual y los portales A/B coincidentes mirando a
  lados opuestos (paso = identidad). Sin niebla: se aplicaría también sobre la superficie del portal.
- /Game/Maps/L_Hub (etapa E3, mapa por defecto): ALiminalHub en el origen, cuatro ALiminalBiome a
  k × 20 km en X (k = 1..4: selva, desierto, Antártida, cultivos), ocho portales enlazados por
  parejas (puerta del hub <-> puerta de vuelta), el mismo sol, cielo, SkyLight y post-process global
  que la calibración, y el PlayerStart dentro del hub. Materiales MI_Hub_* y MI_Biome_*.
- Etapa E4: /Game/Biomes/Materials/M_Terrain (color por vértice del terreno procedural, guardado en
  sRGB, por un parámetro Color) con MI_Biome_<Tipo> como hijos. El sol de L_Hub llega por delante y de
  lado a las puertas de vuelta (HUB_SUN_ROTATION) y su SkyLight va al doble (HUB_SKY_INTENSITY, D-28);
  el sol y el cielo de L_Calibration no cambian.
- Arte low-poly: el hub, la dispersión y las paredes de vuelta usan las mallas de /Game/Art (las importa
  Tools/Content/import_art.py, que va antes en Tools/content.ps1); aquí solo se comprueba que existen. Los
  portales de L_Hub no dibujan su marco de cajas (show_frame = False): lo dibujan SM_Hub_DoorFrame_01 y
  SM_ReturnWall_* (D-32). Borra los MI_Scatter_* y MI_Hub_LightPanel de las formas básicas.

Uso: powershell -File Tools/content.ps1 (lanza UnrealEditor-Cmd -run=pythonscript con este script).
Escribe Saved/Content/build_content.json con el resultado; content.ps1 lo comprueba.
"""

import json
import os
import random
import sys
import traceback

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import content_utils as cu  # noqa: E402 - necesita el sys.path de arriba

PROJECT_DIR = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
RESULT_PATH = os.path.join(PROJECT_DIR, "Saved", "Content", "build_content.json")

SURFACE_PATH = "/Game/Portal/Materials/M_PortalSurface"
FALLBACK_PATH = "/Game/Portal/Materials/M_PortalFallback"
BASIC_PATH = "/Game/Common/Materials/M_Basic"
CALIB_MI_FOLDER = "/Game/Calibration/Materials"
CALIBRATION_MAP = "/Game/Maps/L_Calibration"
HUB_MAP = "/Game/Maps/L_Hub"
PORTAL_CLASS = "/Script/LiminalPortals.LiminalPortal"
HUB_CLASS = "/Script/LiminalPortals.LiminalHub"
BIOME_CLASS = "/Script/LiminalPortals.LiminalBiome"
HUB_MI_FOLDER = "/Game/Hub/Materials"
BIOME_MI_FOLDER = "/Game/Biomes/Materials"
TERRAIN_PATH = "/Game/Biomes/Materials/M_Terrain"

# Debe coincidir con SurfaceTextureParameter de Config/DefaultGame.ini.
SURFACE_TEXTURE_PARAMETER = "PortalTexture"
SURFACE_DEFAULT_TEXTURE = "/Engine/EngineResources/Black"
FALLBACK_COLOR = (0.78, 0.72, 0.52)

# Paleta de calibración en color lineal: saturados y neutros para tener bordes con contraste.
PALETTE = {
    "Red": (0.80, 0.05, 0.05),
    "Green": (0.05, 0.60, 0.10),
    "Blue": (0.05, 0.15, 0.80),
    "Yellow": (0.85, 0.70, 0.05),
    "Cyan": (0.05, 0.60, 0.70),
    "Magenta": (0.70, 0.05, 0.60),
    "Orange": (0.90, 0.30, 0.02),
    "White": (0.80, 0.80, 0.80),
    "Grey": (0.35, 0.35, 0.35),
    "Dark": (0.06, 0.06, 0.06),
}

# Geometría de L_Calibration (cm). El portal A está en el origen mirando a +X; la cámara, delante.
PORTAL_SIZE = (300.0, 300.0)
CAMERA_X = 400.0
PLAYER_START_Z = 100.0
ROOM_MIN_X, ROOM_MAX_X = -3200.0, 1600.0
ROOM_HALF_Y = 2000.0
TILE = 400.0
WALL_HEIGHT = 800.0
SCATTER_SEED = 20261025
SCATTER_COUNT = 28
# Exposición manual (EV) del post-process global; con el sol a 10 lux da tonos medios.
EXPOSURE_BIAS = 0.0
SUN_LUX = 10.0
# unreal.Rotator(roll, pitch, yaw). Calibración: el sol de E2 (no se toca para no mover la medida).
CALIBRATION_SUN_ROTATION = (0.0, -42.0, 35.0)
# L_Hub (E4, D-24): la luz viaja hacia -X y +Y, así que llega a la cara frontal (+X) de cada pared de
# vuelta y su sombra cae detrás y a un lado; desde el hub (mirando a +X) el paisaje tiene luz lateral.
HUB_SUN_ROTATION = (0.0, -35.0, 125.0)
# Luz de cielo: L_Calibration conserva la de E2 (1,0). En L_Hub (D-28) se dobla: con la SkyLight de la
# SkyAtmosphere a 1,0 la sombra del sol sobre los suelos oscuros de la selva salía negra (97 % de los
# píxeles del primer plano por debajo de 20).
CALIBRATION_SKY_INTENSITY = 1.0
HUB_SKY_INTENSITY = 2.0


def build_surface_material():
    material = cu.recreate_material(SURFACE_PATH)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)

    screen = cu.expr(material, unreal.MaterialExpressionScreenPosition, -700, 0)
    sample = cu.expr(material, unreal.MaterialExpressionTextureSampleParameter2D, -400, 0)
    texture = cu.require(unreal.load_asset(SURFACE_DEFAULT_TEXTURE), f"Falta {SURFACE_DEFAULT_TEXTURE}")
    sample.set_editor_property("parameter_name", SURFACE_TEXTURE_PARAMETER)
    sample.set_editor_property("texture", texture)
    sample.set_editor_property("sampler_type", cu.sampler_type_for(texture))
    cu.connect(screen, "ViewportUV", sample, "UVs")
    cu.to_property(sample, "RGB", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    cu.finish_material(material)
    return material


def build_fallback_material():
    material = cu.recreate_material(FALLBACK_PATH)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    color = cu.vector_param(material, "Color", FALLBACK_COLOR, -400, 0)
    cu.to_property(color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    cu.finish_material(material)
    return material


def build_basic_material():
    material = cu.recreate_material(BASIC_PATH)
    # El hub y los biomas son cubos instanciados (ISM): sin este uso, el juego cae al material por defecto.
    material.set_editor_property("used_with_instanced_static_meshes", True)
    color = cu.vector_param(material, "Color", (0.5, 0.5, 0.5), -400, -200)
    roughness = cu.scalar_param(material, "Roughness", 0.6, -400, 0)
    emissive = cu.vector_param(material, "Emissive", (0.0, 0.0, 0.0), -400, 200)
    cu.to_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    cu.to_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    cu.to_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    cu.finish_material(material)
    return material


def build_calibration_instances(basic):
    instances = {}
    for name, color in PALETTE.items():
        roughness = 0.35 if name in ("Red", "Blue", "Yellow") else 0.7
        instances[name] = cu.ensure_material_instance(f"{CALIB_MI_FOLDER}/MI_Calib_{name}", basic,
                                                      {"Color": color}, {"Roughness": roughness})
    return instances


def place_room(actors, mats):
    """Suelo en damero de colores y paredes con paneles; sin techo (se ve el cielo)."""
    colors = list(PALETTE.keys())
    tiles_x = int((ROOM_MAX_X - ROOM_MIN_X) / TILE)
    tiles_y = int(2 * ROOM_HALF_Y / TILE)
    for i in range(tiles_x):
        for j in range(tiles_y):
            x = ROOM_MIN_X + (i + 0.5) * TILE
            y = -ROOM_HALF_Y + (j + 0.5) * TILE
            name = "White" if (i + j) % 2 == 0 else colors[(i * 3 + j * 5) % len(colors)]
            cu.spawn_shape(actors, "cube", unreal.Vector(x, y, -10.0), (TILE / 100.0, TILE / 100.0, 0.2),
                           mats[name], f"Floor_{i:02d}_{j:02d}")

    panel_z = WALL_HEIGHT * 0.5
    for j in range(tiles_y):
        y = -ROOM_HALF_Y + (j + 0.5) * TILE
        name = colors[(j * 7 + 1) % len(colors)]
        cu.spawn_shape(actors, "cube", unreal.Vector(ROOM_MIN_X - 50.0, y, panel_z), (1.0, TILE / 100.0, WALL_HEIGHT / 100.0),
                       mats[name], f"WallBack_{j:02d}")
    for i in range(tiles_x):
        x = ROOM_MIN_X + (i + 0.5) * TILE
        for side, sign in (("L", -1.0), ("R", 1.0)):
            name = colors[(i * 3 + (2 if side == "L" else 5)) % len(colors)]
            cu.spawn_shape(actors, "cube", unreal.Vector(x, sign * (ROOM_HALF_Y + 50.0), panel_z),
                           (TILE / 100.0, 1.0, WALL_HEIGHT / 100.0), mats[name], f"Wall{side}_{i:02d}")


def place_scatter(actors, mats):
    """Formas de colores detrás del plano del portal (lo que se ve a través de él), deterministas."""
    rng = random.Random(SCATTER_SEED)
    shapes = ["cube", "sphere", "cylinder", "cone"]
    colors = list(PALETTE.keys())
    for index in range(SCATTER_COUNT):
        shape = shapes[index % len(shapes)]
        size_xy = rng.uniform(60.0, 260.0)
        size_z = rng.uniform(60.0, 420.0) if shape in ("cube", "cylinder") else size_xy
        x = rng.uniform(-2900.0, -250.0)
        y = rng.uniform(-1700.0, 1700.0)
        z = size_z * 0.5 + (rng.uniform(0.0, 150.0) if shape == "sphere" else 0.0)
        cu.spawn_shape(actors, shape, unreal.Vector(x, y, z), (size_xy / 100.0, size_xy / 100.0, size_z / 100.0),
                       mats[colors[rng.randrange(len(colors))]], f"Scatter_{index:02d}_{shape}", rng.uniform(0.0, 90.0))


def place_lighting(actors, sun_rotation=CALIBRATION_SUN_ROTATION, sky_intensity=CALIBRATION_SKY_INTENSITY):
    sun = cu.spawn(actors, unreal.DirectionalLight, unreal.Vector(0.0, 0.0, 1000.0), "Sun")
    sun.set_actor_rotation(unreal.Rotator(*sun_rotation), False)
    sun_component = sun.get_component_by_class(unreal.DirectionalLightComponent)
    sun_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sun_component.set_editor_property("intensity", SUN_LUX)
    sun_component.set_editor_property("atmosphere_sun_light", True)

    cu.spawn(actors, unreal.SkyAtmosphere, unreal.Vector(0.0, 0.0, 0.0), "SkyAtmosphere")

    sky = cu.spawn(actors, unreal.SkyLight, unreal.Vector(0.0, 0.0, 500.0), "SkyLight")
    sky_component = sky.get_component_by_class(unreal.SkyLightComponent)
    sky_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sky_component.set_editor_property("real_time_capture", True)
    sky_component.set_editor_property("intensity", sky_intensity)


def place_post_process(actors):
    """Único post-process (decisión 5): exposición manual fija, sin cámara física."""
    post = cu.spawn(actors, unreal.PostProcessVolume, unreal.Vector(0.0, 0.0, 0.0), "PostProcess_Global")
    post.set_editor_property("unbound", True)
    settings = post.get_editor_property("settings")
    settings.set_editor_property("override_auto_exposure_method", True)
    settings.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL)
    settings.set_editor_property("override_auto_exposure_bias", True)
    settings.set_editor_property("auto_exposure_bias", EXPOSURE_BIAS)
    settings.set_editor_property("override_auto_exposure_apply_physical_camera_exposure", True)
    settings.set_editor_property("auto_exposure_apply_physical_camera_exposure", False)
    post.set_editor_property("settings", settings)
    return post


def place_portals(actors):
    """A en el origen mirando a +X y B en el mismo sitio mirando a -X: el paso es la identidad."""
    portal_class = cu.load_class(PORTAL_CLASS)
    portal_a = cu.spawn(actors, portal_class, unreal.Vector(0.0, 0.0, 0.0), "Portal_A", 0.0)
    portal_b = cu.spawn(actors, portal_class, unreal.Vector(0.0, 0.0, 0.0), "Portal_B", 180.0)
    for portal in (portal_a, portal_b):
        portal.set_editor_property("portal_size", unreal.Vector2D(*PORTAL_SIZE))
        portal.set_editor_property("embedded", False)
    portal_b.set_editor_property("exit_only", True)
    portal_a.set_editor_property("linked_portal", portal_b)
    portal_b.set_editor_property("linked_portal", portal_a)
    return portal_a, portal_b


def check_portals(portal_a, portal_b):
    cu.require(portal_a.get_editor_property("linked_portal") == portal_b, "Portal_A no enlaza con Portal_B")
    cu.require(portal_b.get_editor_property("exit_only"), "Portal_B debe ser solo de salida")
    cu.require(not portal_a.get_editor_property("embedded"), "Portal_A no debe estar empotrado")
    yaw_a = portal_a.get_actor_rotation().yaw
    yaw_b = portal_b.get_actor_rotation().yaw
    cu.require(abs(abs((yaw_b - yaw_a) % 360.0) - 180.0) < 1e-3, f"A y B deben mirar a lados opuestos ({yaw_a}, {yaw_b})")


def build_calibration_map(mats):
    level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if cu.EAL.does_asset_exist(CALIBRATION_MAP):
        cu.require(cu.EAL.delete_asset(CALIBRATION_MAP), f"No se pudo borrar {CALIBRATION_MAP}")
    cu.require(level_subsystem.new_level(CALIBRATION_MAP, False), f"No se pudo crear {CALIBRATION_MAP}")

    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property("force_no_precomputed_lighting", True)

    place_room(actors, mats)
    place_scatter(actors, mats)
    place_lighting(actors)
    place_post_process(actors)
    portal_a, portal_b = place_portals(actors)
    check_portals(portal_a, portal_b)
    cu.spawn(actors, unreal.PlayerStart, unreal.Vector(CAMERA_X, 0.0, PLAYER_START_Z), "PlayerStart", 180.0)

    cu.require(unreal.EditorLoadingAndSavingUtils.save_map(world, CALIBRATION_MAP), f"No se pudo guardar {CALIBRATION_MAP}")
    count = len(actors.get_all_level_actors())
    unreal.log(f"[LiminalContent] {CALIBRATION_MAP}: {count} actores")
    return count


# L_Hub. Colores lineales de la paleta del hub (docs/ARTE.md): pared #E8DDB5; las losas del suelo y del
# techo quedan bajo la moqueta y sobre las placas (moqueta oscura #8F7A42 y techo #F2EEDD).
HUB_COLORS = {
    "Wall": ((0.807, 0.723, 0.462), 0.85),
    "Carpet": ((0.274, 0.195, 0.054), 0.95),
    "Ceiling": ((0.888, 0.855, 0.723), 0.9),
}
# Assets de las formas básicas que ya no se usan (sustituidos por el arte de /Game/Art).
OBSOLETE_FOLDER_PREFIXES = ("/Game/Biomes/Materials/MI_Scatter_",)
OBSOLETE_ASSETS = ("/Game/Hub/Materials/MI_Hub_LightPanel",)
# Separación de los biomas (decisión 11): k × 20 km en X; igual que LiminalWorldLayout::BiomeSpacing.
BIOME_SPACING = 2000000.0
BIOMES = [("JUNGLE", 1001), ("DESERT", 1002), ("ANTARCTIC", 1003), ("FARMLAND", 1004)]
HUB_PLAYER_START = (-650.0, 0.0, 100.0)
# Las dos puertas de una pareja están en mundos distintos: a más de 1 km.
MIN_PAIR_DISTANCE = 100000.0
# Presupuesto orientativo de instancias por bioma (igual que LiminalScatterMath::MaxInstancesPerBiome).
MAX_SCATTER_INSTANCES = 15000


def build_hub_instances(basic):
    instances = {}
    for name, (color, roughness) in HUB_COLORS.items():
        instances[name] = cu.ensure_material_instance(f"{HUB_MI_FOLDER}/MI_Hub_{name}", basic,
                                                      {"Color": color}, {"Roughness": roughness})
    return instances


def delete_obsolete_assets():
    """Borra los materiales de las formas básicas que ya no usa nadie; devuelve las rutas borradas."""
    deleted = []
    candidates = list(OBSOLETE_ASSETS)
    for prefix in OBSOLETE_FOLDER_PREFIXES:
        folder = prefix.rsplit("/", 1)[0]
        for path in cu.EAL.list_assets(folder, recursive=False):
            package = path.split(".")[0]
            if package.startswith(prefix):
                candidates.append(package)
    for path in candidates:
        if cu.EAL.does_asset_exist(path):
            cu.require(cu.EAL.delete_asset(path), f"No se pudo borrar {path}")
            deleted.append(path)
    return deleted


def check_art_meshes():
    """Las mallas del catálogo C++ (LiminalArtCatalog) tienen que estar importadas en /Game/Art."""
    names = list(unreal.LiminalBiome.get_art_mesh_names())
    missing = []
    for name in names:
        kit = name.split("_")[1]
        if not cu.EAL.does_asset_exist(f"/Game/Art/{kit}/{name}"):
            missing.append(name)
    cu.require(not missing, f"Faltan mallas de arte (Tools/content.ps1 -Mode Art): {missing}")
    return len(names)


def build_terrain_material():
    """Terreno procedural: color base = Color × VertexColor^2,2 (el C++ guarda los colores en sRGB)."""
    material = cu.recreate_material(TERRAIN_PATH)
    vertex = cu.expr(material, unreal.MaterialExpressionVertexColor, -800, -200)
    power = cu.expr(material, unreal.MaterialExpressionPower, -550, -200)
    power.set_editor_property("const_exponent", 2.2)
    cu.connect(vertex, "", power, "Base")
    color = cu.vector_param(material, "Color", (1.0, 1.0, 1.0), -550, -50)
    multiply = cu.expr(material, unreal.MaterialExpressionMultiply, -300, -150)
    cu.connect(power, "", multiply, "A")
    cu.connect(color, "", multiply, "B")
    roughness = cu.scalar_param(material, "Roughness", 0.9, -400, 100)
    cu.to_property(multiply, "", unreal.MaterialProperty.MP_BASE_COLOR)
    cu.to_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    cu.finish_material(material)
    return material


# Rugosidad del terreno por tipo: la arena y el hielo brillan algo más que la tierra.
TERRAIN_ROUGHNESS = {"JUNGLE": 0.9, "DESERT": 0.8, "ANTARCTIC": 0.55, "FARMLAND": 0.95}


def build_biome_instances(terrain):
    """Un MI de terreno por tipo de bioma, hijo de M_Terrain: el color sale de los vértices (C++)."""
    instances = {}
    for type_name, _seed in BIOMES:
        biome_type = getattr(unreal.LiminalBiomeType, type_name)
        label = unreal.LiminalBiome.get_biome_type_name(biome_type)
        instances[type_name] = cu.ensure_material_instance(f"{BIOME_MI_FOLDER}/MI_Biome_{label}", terrain,
                                                           {"Color": (1.0, 1.0, 1.0)}, {"Roughness": TERRAIN_ROUGHNESS[type_name]})
    return instances


def check_generated(actor, label, material):
    """El OnConstruction del actor debe haber generado cajas con el material asignado."""
    components = actor.get_components_by_class(unreal.InstancedStaticMeshComponent)
    cu.require(components, f"{label} no tiene componentes de cajas")
    filled = [c for c in components if c.get_instance_count() > 0]
    cu.require(filled, f"{label} no generó geometría")
    cu.require(any(c.get_material(0) == material for c in filled), f"{label} no tiene el material asignado")


def place_hub(actors, hub_mats):
    hub = cu.spawn(actors, cu.load_class(HUB_CLASS), unreal.Vector(0.0, 0.0, 0.0), "Hub")
    # Cada set_editor_property vuelve a ejecutar OnConstruction (PostEditChangeProperty).
    hub.set_editor_property("wall_material", hub_mats["Wall"])
    hub.set_editor_property("carpet_material", hub_mats["Carpet"])
    hub.set_editor_property("ceiling_material", hub_mats["Ceiling"])
    check_generated(hub, "Hub", hub_mats["Wall"])
    cu.require(hub.get_num_doors() == len(BIOMES), f"El hub tiene {hub.get_num_doors()} puertas y hay {len(BIOMES)} biomas")
    dressing = [c for c in hub.get_components_by_class(unreal.InstancedStaticMeshComponent)
                if c.get_name().startswith("Dressing_") and c.get_instance_count() > 0]
    cu.require(len(dressing) >= 12, f"El hub solo tiene {len(dressing)} piezas de atrezo con instancias")
    return hub


def check_biome_content(biome, label):
    """OnConstruction tiene que haber generado el terreno y la dispersión (componentes transitorios, D-23)."""
    terrains = biome.get_components_by_class(unreal.ProceduralMeshComponent)
    cu.require(len(terrains) == 1, f"{label} no tiene terreno procedural")
    instances = biome.get_editor_property("scatter_instance_count")
    vertices = biome.get_editor_property("terrain_vertex_count")
    cu.require(0 < instances <= MAX_SCATTER_INSTANCES, f"{label}: {instances} instancias (presupuesto {MAX_SCATTER_INSTANCES})")
    cu.require(vertices > 0, f"{label} sin vértices de terreno")
    wall = [c for c in biome.get_components_by_class(unreal.StaticMeshComponent)
            if c.get_name() == "ReturnWallMesh" and c.get_editor_property("static_mesh")]
    cu.require(wall, f"{label} sin pared de vuelta low-poly")
    return {"instances": instances, "triangles": biome.get_editor_property("scatter_triangle_count"),
            "nearInstances": biome.get_editor_property("near_instance_count"),
            "nearTriangles": biome.get_editor_property("near_triangle_count"),
            "components": len(biome.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent)),
            "terrainVertices": vertices}


def place_biomes(actors, biome_mats, wall_material, hub):
    biomes = []
    stats = {}
    door_size = hub.get_editor_property("door_size")
    frame = hub.get_editor_property("door_frame_thickness")
    for index, (type_name, seed) in enumerate(BIOMES):
        biome_type = getattr(unreal.LiminalBiomeType, type_name)
        label = unreal.LiminalBiome.get_biome_type_name(biome_type)
        location = unreal.Vector((index + 1) * BIOME_SPACING, 0.0, 0.0)
        biome = cu.spawn(actors, cu.load_class(BIOME_CLASS), location, f"Biome_{label}")
        # Cada set_editor_property regenera el bioma (OnConstruction): el contenido procedural se
        # desactiva mientras se configura y se activa al final, para generarlo una sola vez.
        biome.set_editor_property("generate_procedural_content", False)
        biome.set_editor_property("biome_type", biome_type)
        biome.set_editor_property("seed", seed)
        biome.set_editor_property("door_size", door_size)
        biome.set_editor_property("door_frame_thickness", frame)
        biome.set_editor_property("ground_material", biome_mats[type_name])
        biome.set_editor_property("wall_material", wall_material)
        biome.set_editor_property("generate_procedural_content", True)
        check_generated(biome, f"Biome_{label}", wall_material)
        stats[label] = check_biome_content(biome, f"Biome_{label}")
        biomes.append((label, biome))
    return biomes, stats


def spawn_portal(actors, portal_class, transform, label, size, frame):
    portal = cu.spawn(actors, portal_class, transform.translation, label)
    portal.set_actor_transform(transform, False, False)
    portal.set_editor_property("portal_size", size)
    portal.set_editor_property("frame_thickness", frame)
    # El marco visible es la malla low-poly del hub o de la pared de vuelta; las cajas solo colisionan (D-32).
    portal.set_editor_property("show_frame", False)
    return portal


def place_hub_portals(actors, hub, biomes):
    """Ocho portales: la puerta k del hub enlaza con la puerta de vuelta del bioma k y al revés."""
    portal_class = cu.load_class(PORTAL_CLASS)
    size = hub.get_editor_property("door_size")
    frame = hub.get_editor_property("door_frame_thickness")
    pairs = []
    for index, (label, biome) in enumerate(biomes):
        hub_portal = spawn_portal(actors, portal_class, hub.get_door_transform(index), f"Portal_Hub_{label}", size, frame)
        biome_portal = spawn_portal(actors, portal_class, biome.get_return_door_transform(), f"Portal_{label}_Return", size, frame)
        hub_portal.set_editor_property("linked_portal", biome_portal)
        biome_portal.set_editor_property("linked_portal", hub_portal)
        pairs.append((hub_portal, biome_portal))
    return pairs


def check_hub_portals(pairs):
    cu.require(len(pairs) == len(BIOMES), "Faltan parejas de portales")
    for hub_portal, biome_portal in pairs:
        cu.require(hub_portal.get_editor_property("linked_portal") == biome_portal, f"{hub_portal.get_actor_label()} mal enlazado")
        cu.require(biome_portal.get_editor_property("linked_portal") == hub_portal, f"{biome_portal.get_actor_label()} mal enlazado")
        cu.require(hub_portal.get_editor_property("embedded") and biome_portal.get_editor_property("embedded"),
                   "Los portales del hub van empotrados (decisión 3)")
        distance = (hub_portal.get_actor_location() - biome_portal.get_actor_location()).length()
        cu.require(distance > MIN_PAIR_DISTANCE, f"Pareja demasiado cerca ({distance:.0f} cm)")


def build_hub_map(basic):
    level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    art_meshes = check_art_meshes()
    hub_mats = build_hub_instances(basic)
    biome_mats = build_biome_instances(build_terrain_material())
    deleted = delete_obsolete_assets()
    if cu.EAL.does_asset_exist(HUB_MAP):
        cu.require(cu.EAL.delete_asset(HUB_MAP), f"No se pudo borrar {HUB_MAP}")
    cu.require(level_subsystem.new_level(HUB_MAP, False), f"No se pudo crear {HUB_MAP}")

    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property("force_no_precomputed_lighting", True)

    hub = place_hub(actors, hub_mats)
    biomes, biome_stats = place_biomes(actors, biome_mats, hub_mats["Wall"], hub)
    pairs = place_hub_portals(actors, hub, biomes)
    check_hub_portals(pairs)
    place_lighting(actors, HUB_SUN_ROTATION, HUB_SKY_INTENSITY)
    place_post_process(actors)
    cu.spawn(actors, unreal.PlayerStart, unreal.Vector(*HUB_PLAYER_START), "PlayerStart", 0.0)

    cu.require(unreal.EditorLoadingAndSavingUtils.save_map(world, HUB_MAP), f"No se pudo guardar {HUB_MAP}")
    count = len(actors.get_all_level_actors())
    unreal.log(f"[LiminalContent] {HUB_MAP}: {count} actores, {2 * len(pairs)} portales, {art_meshes} mallas de arte")
    for label, stats in biome_stats.items():
        unreal.log(f"[LiminalContent] {label}: {stats['instances']} instancias ({stats['triangles']} triángulos) en "
                   f"{stats['components']} HISM; vistas desde la puerta: {stats['nearInstances']} ({stats['nearTriangles']} triángulos)")
    if deleted:
        unreal.log(f"[LiminalContent] Borrados por obsoletos: {deleted}")
    materials = list(hub_mats.values()) + list(biome_mats.values())
    assets = [TERRAIN_PATH] + [mi.get_path_name() for mi in materials]
    return count, 2 * len(pairs), assets, biome_stats


def write_result(result):
    os.makedirs(os.path.dirname(RESULT_PATH), exist_ok=True)
    with open(RESULT_PATH, "w", encoding="utf-8") as f:
        json.dump(result, f, indent=2, ensure_ascii=False)


def main():
    result = {"ok": False, "assets": [], "error": ""}
    try:
        surface = build_surface_material()
        fallback = build_fallback_material()
        basic = build_basic_material()
        mats = build_calibration_instances(basic)
        actor_count = build_calibration_map(mats)
        hub_actors, hub_portals, hub_assets, biome_stats = build_hub_map(basic)
        result["assets"] = [surface.get_path_name(), fallback.get_path_name(), basic.get_path_name(), CALIBRATION_MAP, HUB_MAP]
        result["assets"] += [mi.get_path_name() for mi in mats.values()] + hub_assets
        result["calibrationActors"] = actor_count
        result["hubActors"] = hub_actors
        result["hubPortals"] = hub_portals
        result["biomes"] = biome_stats
        result["ok"] = True
        unreal.log("[LiminalContent] OK")
    except Exception as error:  # noqa: BLE001 - se registra y se refleja en el JSON de resultado
        result["error"] = f"{type(error).__name__}: {error}"
        unreal.log_error(f"[LiminalContent] {traceback.format_exc()}")
    write_result(result)


main()
