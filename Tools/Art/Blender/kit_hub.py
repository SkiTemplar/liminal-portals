"""Kit Hub: oficina/pasillo liminal vacío. Geometría pura (sin bpy), en cm, +X delante.

Pivotes: el marco de puerta usa el origen del portal (centro inferior del hueco en el plano de la
cara visible de la pared, X hacia fuera; la pared ocupa X ∈ [−20, 0]). Las piezas de pared
(rodapié, rejilla, enchufe, cartel) tienen el pivote en el plano de la pared (X = 0) y sobresalen
hacia +X. Las de techo (placa, panel) tienen el pivote en el plano del techo (Z = 0) y suben.
"""
from __future__ import annotations

import random

from lp_checks import (
    DOOR_FRAME, DOOR_HEIGHT, DOOR_WIDTH, LIGHT_PANEL, PORTAL_FRONT_OVERHANG, WALL_THICKNESS, Asset, AssetSpec,
)
from lp_geom import Mesh, hex_color, make_mesh, merge, mix, pivot_base_center, rot_x, rot_y, shade, transform, translate
from lp_geom import drop_to_ground, orient_towards
from lp_shapes import box, cylinder_between, frustum, sweep_planar

KIT_NAME = "Hub"
SEED = 1101

WALL = hex_color("#E8DDB5")
CARPET = hex_color("#B39A5A")
CARPET_DARK = hex_color("#8F7A42")
CEILING = hex_color("#F2EEDD")
GRILLE = hex_color("#CFC8B0")
FRAME = hex_color("#6E6A5E")
FLUO = hex_color("#FFF8E0", emissive=True)
BASEBOARD = hex_color("#9C927A")
# Fuera de la paleta del hub, justificado: verde normativo de señal de evacuación (emisivo).
EXIT_GREEN = hex_color("#2F9E5B", emissive=True)

# Perfil del tapajuntas (n hacia fuera del hueco, d hacia fuera de la pared), cm.
CASING_PROFILE = ((0.0, 0.0), (10.0, 0.0), (9.5, 2.5), (8.0, 4.0), (3.5, 5.0), (2.5, 6.0), (0.0, 6.0))
CASING_WIDTH = 10.0
PLINTH_WIDTH = CASING_WIDTH + 1.0


def _casing(front: bool) -> Mesh:
    """Tapajuntas con ingletes alrededor del hueco de pared (exterior del marco del portal)."""
    out_y = DOOR_WIDTH * 0.5 + DOOR_FRAME
    out_z = DOOR_HEIGHT + DOOR_FRAME
    path = ((-out_y, 0.0), (-out_y, out_z), (out_y, out_z), (out_y, 0.0))
    if front:
        return sweep_planar(path, CASING_PROFILE, lambda u, v, d: (d, u, v), FRAME)
    return sweep_planar(path, CASING_PROFILE, lambda u, v, d: (-WALL_THICKNESS - d, u, v), FRAME)


def _plinths(front: bool) -> Mesh:
    """Tacos de base del tapajuntas, algo más gruesos que él."""
    y0 = DOOR_WIDTH * 0.5 + DOOR_FRAME - 0.5
    y1 = DOOR_WIDTH * 0.5 + DOOR_FRAME + PLINTH_WIDTH
    x0, x1 = (0.0, 7.0) if front else (-WALL_THICKNESS - 7.0, -WALL_THICKNESS)
    return merge(*(box((x0, lo, 0.0), (x1, hi, 16.0), shade(FRAME, 0.9), skip=("-z",))
                   for lo, hi in ((-y1, -y0), (y0, y1))))


def door_frame(double_sided: bool) -> Mesh:
    """Marco con forro de 12 cm (sin invadir el hueco de 140 × 230) y tapajuntas en una o dos caras."""
    half = DOOR_WIDTH * 0.5
    x_back = -WALL_THICKNESS - (PORTAL_FRONT_OVERHANG if double_sided else 0.0)
    x_front = PORTAL_FRONT_OVERHANG
    lining = shade(FRAME, 1.08)
    parts = [
        box((x_back, -half - DOOR_FRAME, 0.0), (x_front, -half, DOOR_HEIGHT), lining, skip=("-z", "+z", "-y")),
        box((x_back, half, 0.0), (x_front, half + DOOR_FRAME, DOOR_HEIGHT), lining, skip=("-z", "+z", "+y")),
        box((x_back, -half - DOOR_FRAME, DOOR_HEIGHT), (x_front, half + DOOR_FRAME, DOOR_HEIGHT + DOOR_FRAME),
            lining, skip=("+z",)),
        _casing(True), _plinths(True),
    ]
    if double_sided:
        parts += [_casing(False), _plinths(False)]
    return merge(*parts)


def baseboard(corner: bool) -> Mesh:
    """Rodapié modular de 100 cm a lo largo de Y, o esquina interior de 2 × 30 cm."""
    profile = ((0.0, 0.0), (1.6, 0.0), (1.6, 7.5), (1.1, 9.0), (0.0, 10.0))
    path = ((0.0, 30.0), (0.0, 0.0), (30.0, 0.0)) if corner else ((0.0, 50.0), (0.0, -50.0))
    return sweep_planar(path, profile, lambda u, v, d: (u, v, d), BASEBOARD)


def ceiling_tile(stained: bool) -> Mesh:
    """Placa de 60 × 60 con media perfilería en T (1,2 cm por lado); la cara vista está en Z ≈ 0."""
    rim = 28.8
    tee = merge(
        box((-30.0, -30.0, 0.0), (30.0, -rim, 3.5), GRILLE, skip=("+z",)),
        box((-30.0, rim, 0.0), (30.0, 30.0, 3.5), GRILLE, skip=("+z",)),
        box((-30.0, -rim, 0.0), (-rim, rim, 3.5), GRILLE, skip=("+z", "-y", "+y")),
        box((rim, -rim, 0.0), (30.0, rim, 3.5), GRILLE, skip=("+z", "-y", "+y")),
    )
    cells = 6
    step = 2.0 * rim / cells
    rng = random.Random(SEED + 7)
    verts, faces, colors = [], [], []
    for i in range(cells + 1):
        for j in range(cells + 1):
            x, y = -rim + i * step, -rim + j * step
            sag = 0.0
            if stained and 0 < i < cells and 0 < j < cells:
                sag = 0.35 * (1.0 - (abs(i - 3) + abs(j - 3)) / 4.0)
            verts.append((x, y, 0.8 - max(sag, 0.0)))
    for i in range(cells):
        for j in range(cells):
            a = i * (cells + 1) + j
            faces.append((a, a + 1, a + cells + 2, a + cells + 1))
            dist = ((i - 3.6) ** 2 + (j - 2.4) ** 2) ** 0.5
            stain = stained and dist < 2.4 + rng.random() * 0.6
            tone = mix(CEILING, CARPET_DARK, 0.35 if dist < 1.3 else 0.18) if stain else shade(CEILING, 1.0 - 0.02 * ((i + j) % 2))
            colors.append(tone)
    face_mesh = make_mesh(verts, faces, colors)
    oriented = make_mesh(face_mesh.verts, [orient_towards(f, face_mesh.verts, (0.0, 0.0, -1.0)) for f in face_mesh.faces],
                         face_mesh.colors)
    body = box((-rim, -rim, 0.8), (rim, rim, 2.3), CEILING, skip=("-z",))
    return merge(tee, oriented, body)


def light_panel() -> Mesh:
    """Panel fluorescente empotrado de 120 × 60 (LightPanelSize): difusor emisivo y lamas."""
    hx, hy = LIGHT_PANEL[0] * 0.5, LIGHT_PANEL[1] * 0.5
    ix, iy = hx - 3.0, hy - 3.0
    frame = merge(
        box((-hx, -hy, 0.0), (hx, -iy, 1.5), GRILLE, skip=("+z",)),
        box((-hx, iy, 0.0), (hx, hy, 1.5), GRILLE, skip=("+z",)),
        box((-hx, -iy, 0.0), (-ix, iy, 1.5), GRILLE, skip=("+z", "-y", "+y")),
        box((ix, -iy, 0.0), (hx, iy, 1.5), GRILLE, skip=("+z", "-y", "+y")),
    )
    well = merge(
        box((-ix, -iy, 1.5), (ix, -iy + 0.5, 4.5), CEILING, skip=("-z", "+z", "-y")),
        box((-ix, iy - 0.5, 1.5), (ix, iy, 4.5), CEILING, skip=("-z", "+z", "+y")),
        box((-ix, -iy, 1.5), (-ix + 0.5, iy, 4.5), CEILING, skip=("-z", "+z", "-x")),
        box((ix - 0.5, -iy, 1.5), (ix, iy, 4.5), CEILING, skip=("-z", "+z", "+x")),
    )
    diffuser = make_mesh([(-ix, -iy, 4.5), (-ix, iy, 4.5), (ix, iy, 4.5), (ix, -iy, 4.5)], [(0, 1, 2, 3)], FLUO)
    housing = box((-hx, -hy, 4.5), (hx, hy, 9.0), shade(GRILLE, 0.9), skip=("-z",))
    louvers = [box((-ix, y - 0.3, 1.5), (ix, y + 0.3, 4.2), GRILLE, skip=("+z",)) for y in (-iy / 3.0, iy / 3.0)]
    louvers += [box((x - 0.3, -iy, 1.5), (x + 0.3, iy, 4.2), GRILLE, skip=("+z",))
                for x in (-ix * 0.6, -ix * 0.2, ix * 0.2, ix * 0.6)]
    return merge(frame, well, diffuser, housing, *louvers)


def vent() -> Mesh:
    """Rejilla de ventilación de pared 40 × 25 con lamas inclinadas sobre fondo oscuro."""
    w, h = 20.0, 25.0
    frame = merge(
        box((0.0, -w, 0.0), (1.5, w, 2.0), GRILLE),
        box((0.0, -w, h - 2.0), (1.5, w, h), GRILLE),
        box((0.0, -w, 2.0), (1.5, -w + 2.0, h - 2.0), GRILLE, skip=("-z", "+z")),
        box((0.0, w - 2.0, 2.0), (1.5, w, h - 2.0), GRILLE, skip=("-z", "+z")),
    )
    back = make_mesh([(0.2, -w + 2, 2), (0.2, w - 2, 2), (0.2, w - 2, h - 2), (0.2, -w + 2, h - 2)],
                     [(0, 1, 2, 3)], shade(FRAME, 0.7))
    slats = []
    for k in range(6):
        z = 4.5 + k * 3.2
        slat = box((-0.9, -w + 2.0, -0.25), (0.9, w - 2.0, 0.25), shade(GRILLE, 0.95), skip=("-y", "+y"))
        slats.append(transform(slat, rot_y(-35.0), (0.9, 0.0, z)))
    return merge(frame, back, *slats)


def _wall_plate() -> Mesh:
    return frustum((0.0, 0.0, 0.0), 6.1, 5.6, 1.0, 4, CEILING, phase=0.785398, cap_bottom=False)


def outlet() -> Mesh:
    """Enchufe tipo Schuko: placa 8,6 × 8,6 con alveolo redondo y dos orificios."""
    plate = transform(_wall_plate(), rot_y(90.0), (0.0, 0.0, 4.3))
    socket = transform(frustum((0.0, 0.0, 0.0), 2.6, 2.4, 0.6, 10, shade(CEILING, 0.93), cap_bottom=False),
                       rot_y(90.0), (1.0, 0.0, 4.3))
    holes = [transform(frustum((0.0, 0.0, 0.0), 0.35, 0.35, 0.1, 6, shade(FRAME, 0.6), cap_bottom=False),
                       rot_y(90.0), (1.6, y, 4.3)) for y in (-0.95, 0.95)]
    return merge(plate, socket, *holes)


def switch() -> Mesh:
    """Interruptor de tecla basculante sobre la misma placa."""
    plate = transform(_wall_plate(), rot_y(90.0), (0.0, 0.0, 4.3))
    rocker = transform(box((-0.4, -2.0, -2.8), (0.4, 2.0, 2.8), shade(CEILING, 0.97), skip=("-x",)),
                       rot_y(-4.0), (1.4, 0.0, 4.3))
    return merge(plate, rocker)


def exit_sign() -> Mesh:
    """Cartel de salida de pared 36 × 15 × 5: cara verde emisiva y pictograma claro emisivo."""
    housing = box((0.0, -18.0, 0.0), (4.0, 18.0, 15.0), CEILING, skip=("-x",))
    face = make_mesh([(4.05, -16.5, 1.5), (4.05, 16.5, 1.5), (4.05, 16.5, 13.5), (4.05, -16.5, 13.5)],
                     [(0, 1, 2, 3)], EXIT_GREEN)
    x = 4.12
    door = ((-14.5, 3.0), (-10.5, 3.0), (-10.5, 12.0), (-14.5, 12.0))
    # Figura corriendo hacia la flecha: cuadriláteros inclinados (y, z) sobre la cara verde.
    figure = (
        ((-6.4, 10.4), (-4.6, 10.4), (-4.6, 12.2), (-6.4, 12.2)),   # cabeza
        ((-7.6, 6.2), (-6.0, 6.0), (-4.6, 9.8), (-6.2, 10.0)),      # torso
        ((-9.8, 3.0), (-8.2, 3.0), (-6.0, 6.4), (-7.6, 6.6)),       # pierna trasera
        ((-5.0, 3.0), (-3.4, 3.0), (-5.4, 5.6), (-7.0, 6.4)),       # pierna delantera
        ((-6.0, 8.6), (-2.6, 7.4), (-2.2, 8.4), (-5.6, 9.8)),       # brazo
    )
    shaft = ((0.5, 6.8), (10.0, 6.8), (10.0, 8.2), (0.5, 8.2))
    parts = [make_mesh([(x, y, z) for y, z in quad], [(0, 1, 2, 3)], FLUO) for quad in (door, *figure, shaft)]
    parts.append(make_mesh([(x, 10.0, 4.5), (x, 14.5, 7.5), (x, 10.0, 10.5)], [(0, 1, 2)], FLUO))
    return merge(housing, face, *parts)


def carpet_tile(pattern: str) -> Mesh:
    """Losa de moqueta de 100 × 100 × 1 cm, enlosable, con patrón en los dos tonos de la paleta."""
    cells, size = 10, 10.0
    verts = [(-50.0 + i * size, -50.0 + j * size, 1.0) for i in range(cells + 1) for j in range(cells + 1)]
    faces, colors = [], []
    for i in range(cells):
        for j in range(cells):
            a, b = i * (cells + 1) + j, (i + 1) * (cells + 1) + j
            quad = (a, b, b + 1, a + 1)
            if pattern == "diamond":
                tris = ((a, b, b + 1), (a, b + 1, a + 1)) if (i + j) % 2 == 0 else ((a, b, a + 1), (b, b + 1, a + 1))
                for t in tris:
                    cu = sum(verts[k][0] for k in t) / 3.0 + 50.0
                    cv = sum(verts[k][1] for k in t) / 3.0 + 50.0
                    dark = (int((cu + cv) // 20.0) + int((cu - cv + 100.0) // 20.0)) % 2 == 1
                    faces.append(t)
                    colors.append(CARPET_DARK if dark else CARPET)
            else:
                faces.append(quad)
                colors.append(CARPET_DARK if i % 5 in (0, 3) else CARPET)
    top = make_mesh(verts, faces, colors)
    sides = box((-50.0, -50.0, 0.0), (50.0, 50.0, 1.0), CARPET_DARK, skip=("+z", "-z"))
    return merge(top, sides)


def chair() -> Mesh:
    """Silla monobloque de plástico mirando a +X (asiento a 44 cm)."""
    color = FRAME
    seat = frustum((0.0, 0.0, 41.0), 30.0, 31.0, 3.0, 8, color, phase=0.3927, stretch=(0.72, 0.72))
    legs = [cylinder_between((sx * 23.0, sy * 22.0, 0.0), (sx * 16.0, sy * 17.0, 41.5), 1.6, 2.6, 4, color)
            for sx in (-1, 1) for sy in (-1, 1)]
    top_rail = cylinder_between((-28.0, -20.0, 83.0), (-28.0, 20.0, 83.0), 2.2, 2.2, 4, color)
    panel = box((-1.2, -19.0, 0.0), (1.2, 19.0, 34.0), color)
    panel = transform(panel, rot_y(-10.0), (-26.0, 0.0, 47.0))
    posts = [cylinder_between((-20.0, s * 19.0, 43.0), (-28.0, s * 19.0, 84.0), 1.8, 1.8, 4, color) for s in (-1, 1)]
    arms = [cylinder_between((-24.0, s * 22.0, 64.0), (18.0, s * 23.0, 62.0), 1.8, 1.8, 4, color) for s in (-1, 1)]
    arm_legs = [cylinder_between((17.0, s * 23.0, 62.0), (16.0, s * 18.0, 43.0), 1.6, 1.6, 4, color) for s in (-1, 1)]
    return merge(seat, *legs, panel, top_rail, *posts, *arms, *arm_legs)


def build() -> list[Asset]:
    door_box_y = DOOR_WIDTH * 0.5 + DOOR_FRAME + PLINTH_WIDTH
    door_top = DOOR_HEIGHT + DOOR_FRAME + CASING_WIDTH
    return [
        Asset(AssetSpec("SM_Hub_DoorFrame_01", "hub_prop", 1500, door_clear=True, pivot="portal",
                        exact_bbox=((-WALL_THICKNESS, -door_box_y, 0.0), (7.0, door_box_y, door_top)),
                        notes="marco + tapajuntas por la cara visible (hub)"), door_frame(False), 0.05),
        Asset(AssetSpec("SM_Hub_DoorFrame_02", "hub_prop", 1500, door_clear=True, pivot="portal",
                        exact_bbox=((-WALL_THICKNESS - 7.0, -door_box_y, 0.0), (7.0, door_box_y, door_top)),
                        notes="marco + tapajuntas por las dos caras"), door_frame(True), 0.05),
        Asset(AssetSpec("SM_Hub_Baseboard_01", "hub_prop", 1500, pivot="wall", size_range=(99.0, 101.0),
                        notes="tramo recto de 100 cm a lo largo de Y"), baseboard(False), 0.0),
        Asset(AssetSpec("SM_Hub_Baseboard_02", "hub_prop", 1500, pivot="wall", notes="esquina interior 30 + 30 cm"),
              baseboard(True), 0.0),
        Asset(AssetSpec("SM_Hub_CeilingTile_01", "hub_prop", 1500, pivot="ceiling", size_range=(59.9, 60.1),
                        notes="cara vista hacia −Z: pivote en el plano del techo, sin girar"),
              ceiling_tile(False), 0.0),
        Asset(AssetSpec("SM_Hub_CeilingTile_02", "hub_prop", 1500, pivot="ceiling", size_range=(59.9, 60.1),
                        notes="placa con mancha de humedad y leve combado"), ceiling_tile(True), 0.0),
        Asset(AssetSpec("SM_Hub_LightPanel_01", "hub_prop", 1500, pivot="ceiling", expect_emissive=True,
                        size_range=(119.9, 120.1), notes="difusor emisivo (alfa 0)"), light_panel(), 0.0),
        Asset(AssetSpec("SM_Hub_Vent_01", "hub_prop", 1500, pivot="wall"), drop_to_ground(vent()), 0.0),
        Asset(AssetSpec("SM_Hub_Outlet_01", "hub_prop", 1500, pivot="wall"), drop_to_ground(outlet()), 0.0),
        Asset(AssetSpec("SM_Hub_Switch_01", "hub_prop", 1500, pivot="wall"), drop_to_ground(switch()), 0.0),
        Asset(AssetSpec("SM_Hub_ExitSign_01", "hub_prop", 1500, pivot="wall", expect_emissive=True,
                        notes="cara verde y pictograma emisivos (alfa 0)"), exit_sign(), 0.0),
        Asset(AssetSpec("SM_Hub_Carpet_01", "hub_prop", 1500, size_range=(99.9, 100.1),
                        notes="losa 100 × 100 enlosable, rombos"), carpet_tile("diamond"), 0.0),
        Asset(AssetSpec("SM_Hub_Carpet_02", "hub_prop", 1500, size_range=(99.9, 100.1),
                        notes="losa 100 × 100 enlosable, franjas"), carpet_tile("stripes"), 0.0),
        Asset(AssetSpec("SM_Hub_Chair_01", "hub_prop", 1500, size_range=(70.0, 95.0)),
              pivot_base_center(chair()), 0.08),
    ]
