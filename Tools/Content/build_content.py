"""Crea el contenido de LiminalPortals por script (etapa E2 de docs/PLAN-NOCHE.md). Idempotente.

- /Game/Portal/Materials/M_PortalSurface: unlit, two-sided; emisivo = TextureSampleParameter2D
  «PortalTexture» muestreado en ScreenPosition.ViewportUV (el render target del portal, en HDR lineal).
- /Game/Portal/Materials/M_PortalFallback: unlit, color liminal plano (reserva visible solo en capturas).
- /Game/Common/Materials/M_Basic: lit con parámetros Color, Roughness y Emissive, e instancias
  MI_Calib_* con la paleta de calibración.
- /Game/Maps/L_Calibration: sala abierta con geometría de colores variados, sol, SkyAtmosphere,
  SkyLight, post-process global con exposición manual y los portales A/B coincidentes mirando a
  lados opuestos (paso = identidad). Sin niebla: se aplicaría también sobre la superficie del portal.

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
PORTAL_CLASS = "/Script/LiminalPortals.LiminalPortal"

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


def place_lighting(actors):
    sun = cu.spawn(actors, unreal.DirectionalLight, unreal.Vector(0.0, 0.0, 1000.0), "Sun")
    sun.set_actor_rotation(unreal.Rotator(0.0, -42.0, 35.0), False)
    sun_component = sun.get_component_by_class(unreal.DirectionalLightComponent)
    sun_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sun_component.set_editor_property("intensity", SUN_LUX)
    sun_component.set_editor_property("atmosphere_sun_light", True)

    cu.spawn(actors, unreal.SkyAtmosphere, unreal.Vector(0.0, 0.0, 0.0), "SkyAtmosphere")

    sky = cu.spawn(actors, unreal.SkyLight, unreal.Vector(0.0, 0.0, 500.0), "SkyLight")
    sky_component = sky.get_component_by_class(unreal.SkyLightComponent)
    sky_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sky_component.set_editor_property("real_time_capture", True)


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
        result["assets"] = [surface.get_path_name(), fallback.get_path_name(), basic.get_path_name(), CALIBRATION_MAP]
        result["assets"] += [mi.get_path_name() for mi in mats.values()]
        result["calibrationActors"] = actor_count
        result["ok"] = True
        unreal.log("[LiminalContent] OK")
    except Exception as error:  # noqa: BLE001 - se registra y se refleja en el JSON de resultado
        result["error"] = f"{type(error).__name__}: {error}"
        unreal.log_error(f"[LiminalContent] {traceback.format_exc()}")
    write_result(result)


main()
