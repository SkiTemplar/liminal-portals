"""Kit ReturnWall: pared exenta de vuelta, un trozo del hub arrancado y plantado en el bioma.

Medidas de ALiminalBiome::WallSize (20 × 600 × 350) y del hueco de LiminalWorldLayout::MakeDoorOpening
(164 × 242). Pivote en el origen del portal: puerta mirando a +X y pared en X ∈ [−20, 0].
Cuatro variantes de zócalo teñidas por bioma, con bordes superior y laterales rotos y desgaste.
"""
from __future__ import annotations

import math
import random

from kit_hub import WALL, door_frame
from lp_checks import DOOR_FRAME, DOOR_HEIGHT, DOOR_WIDTH, FREESTANDING_WALL, Asset, AssetSpec
from lp_geom import Color, Mesh, hex_color, make_mesh, merge, mix, orient_towards, shade
from lp_shapes import box, extrude_planar, strip

KIT_NAME = "ReturnWall"
SEED = 2202

THICKNESS, WIDTH, HEIGHT = FREESTANDING_WALL
OPEN_HALF = DOOR_WIDTH * 0.5 + DOOR_FRAME          # 82: medio ancho del hueco de pared
OPEN_TOP = DOOR_HEIGHT + DOOR_FRAME                # 242
KEEP_OUT_HALF = OPEN_HALF + 12.0                   # tapajuntas y tacos
KEEP_OUT_TOP = OPEN_TOP + 12.0
CORE = shade(WALL, 0.78)
SKIRT_HEIGHT = 14.0

VARIANTS = (
    ("Sand", hex_color("#D9A866")),   # Desierto
    ("Snow", hex_color("#DDE7EF")),   # Antártida
    ("Moss", hex_color("#2E6B36")),   # Selva
    ("Mud", hex_color("#5C3E26")),    # Cultivos
)


def wall_columns() -> list[float]:
    half = WIDTH * 0.5
    side = [-half + k * (half - OPEN_HALF) / 6.0 for k in range(6)]
    return side + [-OPEN_HALF, -OPEN_HALF / 2.0, 0.0, OPEN_HALF / 2.0, OPEN_HALF] + [-u for u in reversed(side)]


WALL_ROWS = (0.0, SKIRT_HEIGHT, 60.0, 120.0, 180.0, OPEN_TOP, 296.0, HEIGHT)


def in_opening(u: float, v: float) -> bool:
    return abs(u) < OPEN_HALF - 1e-6 and v < OPEN_TOP - 1e-6


def wall_grid(rng: random.Random) -> tuple[list[tuple[float, float]], list[tuple[int, ...]], list[int]]:
    """Malla plana (u, v) de la pared con el hueco y los bordes superior y laterales rotos.

    Devuelve puntos, quads antihorarios y la fila de cada quad (para el tinte del bioma).
    """
    cols, rows = wall_columns(), WALL_ROWS
    points: list[tuple[float, float]] = []
    index: dict[tuple[int, int], int] = {}
    bites = set(rng.sample(range(1, len(cols) - 1), 2))
    for i, u in enumerate(cols):
        for j, v in enumerate(rows):
            du = dv = 0.0
            if j == len(rows) - 1:
                dv = -rng.uniform(4.0, 18.0) - (rng.uniform(14.0, 24.0) if i in bites else 0.0)
            if (i == 0 or i == len(cols) - 1) and v >= 120.0:
                du = rng.uniform(4.0, 22.0) * (1.0 if i == 0 else -1.0)
            index[(i, j)] = len(points)
            points.append((u + du, v + dv))
    quads, row_of = [], []
    for i in range(len(cols) - 1):
        for j in range(len(rows) - 1):
            mid_u, mid_v = (cols[i] + cols[i + 1]) * 0.5, (rows[j] + rows[j + 1]) * 0.5
            if in_opening(mid_u, mid_v):
                continue
            quads.append((index[(i, j)], index[(i + 1, j)], index[(i + 1, j + 1)], index[(i, j + 1)]))
            row_of.append(j)
    return points, quads, row_of


def _face_mapper(u: float, v: float, d: float):
    return (d, u, v)


def wall_body(rng: random.Random, tint: Color) -> Mesh:
    points, quads, rows = wall_grid(rng)

    def face_color(i: int) -> Color:
        return {0: mix(WALL, tint, 0.45), 1: mix(WALL, tint, 0.18)}.get(rows[i], WALL)

    def side_color(a, b) -> Color:
        on_hole = abs(abs(a[0]) - OPEN_HALF) < 1e-6 and abs(abs(b[0]) - OPEN_HALF) < 1e-6
        on_lintel = abs(a[1] - OPEN_TOP) < 1e-6 and abs(b[1] - OPEN_TOP) < 1e-6
        return WALL if on_hole or on_lintel else CORE

    return extrude_planar(points, quads, _face_mapper, THICKNESS, face_color, face_color, side_color)


def skirting(rng: random.Random, tint: Color) -> Mesh:
    """Zócalo por las dos caras, en tramos de altura y tono algo irregulares (desgaste)."""
    half = WIDTH * 0.5
    parts = []
    for x0, x1 in ((0.0, 1.5), (-THICKNESS - 1.5, -THICKNESS)):
        for start, end in ((-half, -OPEN_HALF), (OPEN_HALF, half)):
            cuts = [start + (end - start) * k / 3.0 for k in range(4)]
            for k in range(3):
                h = SKIRT_HEIGHT - rng.uniform(0.0, 2.5)
                parts.append(box((x0, cuts[k], 0.0), (x1, cuts[k + 1], h), shade(tint, rng.uniform(0.9, 1.05)),
                                 skip=("-z",)))
    return merge(*parts)


def _on_face(points2d, front: bool, offset: float, color: Color) -> Mesh:
    x = offset if front else -THICKNESS - offset
    return make_mesh([(x, u, v) for u, v in points2d], [tuple(range(len(points2d)))], color)


def _oriented(mesh: Mesh, front: bool) -> Mesh:
    direction = (1.0, 0.0, 0.0) if front else (-1.0, 0.0, 0.0)
    return make_mesh(mesh.verts, [orient_towards(f, mesh.verts, direction) for f in mesh.faces], mesh.colors)


def crack(rng: random.Random, start: tuple[float, float], angle: float, front: bool) -> Mesh:
    """Grieta facetada: tira quebrada que se estrecha, 0,15 cm por delante de la cara."""
    pts = [start]
    heading = angle
    for _ in range(6):
        heading += rng.uniform(-35.0, 35.0)
        step = rng.uniform(9.0, 16.0)
        u, v = pts[-1]
        nxt = (u + step * math.cos(math.radians(heading)), v + step * math.sin(math.radians(heading)))
        if nxt[1] > 300.0 or nxt[1] < 30.0 or abs(nxt[0]) > WIDTH * 0.5 - 30.0 or \
                (abs(nxt[0]) < KEEP_OUT_HALF and nxt[1] < KEEP_OUT_TOP):
            break
        pts.append(nxt)
    if len(pts) < 2:
        return Mesh((), (), ())
    left, right = [], []
    x = 0.15 if front else -THICKNESS - 0.15
    for k, (u, v) in enumerate(pts):
        a = pts[min(k + 1, len(pts) - 1)]
        b = pts[max(k - 1, 0)]
        du, dv = a[0] - b[0], a[1] - b[1]
        n = math.hypot(du, dv) or 1.0
        w = 1.4 * (1.0 - k / (len(pts) - 1)) + 0.05
        left.append((x, u - dv / n * w, v + du / n * w))
        right.append((x, u + dv / n * w, v - du / n * w))
    return _oriented(strip(left, right, shade(WALL, 0.5), double_sided=False), front)


def chip(rng: random.Random, center: tuple[float, float], front: bool) -> Mesh:
    """Desconchón: dos capas irregulares de yeso más oscuro sobre la cara."""
    radius = rng.uniform(6.0, 13.0)
    sides = rng.choice((5, 6, 7))
    angles = sorted(rng.uniform(0, 360) for _ in range(sides))
    outer = [(center[0] + radius * rng.uniform(0.7, 1.2) * math.cos(math.radians(a)),
              center[1] + radius * rng.uniform(0.6, 1.0) * math.sin(math.radians(a))) for a in angles]
    inner = [(center[0] + (u - center[0]) * 0.55, center[1] + (v - center[1]) * 0.55) for u, v in outer]
    return merge(_oriented(_on_face(outer, front, 0.2, shade(WALL, 0.86)), front),
                 _oriented(_on_face(inner, front, 0.3, shade(WALL, 0.74)), front))


def _free_spot(rng: random.Random) -> tuple[float, float]:
    for _ in range(100):
        u, v = rng.uniform(-260.0, 260.0), rng.uniform(40.0, 290.0)
        if not (abs(u) < KEEP_OUT_HALF + 15.0 and v < KEEP_OUT_TOP + 15.0):
            return u, v
    raise RuntimeError("no hay sitio libre para el desconchón")


def wear(rng: random.Random) -> Mesh:
    parts = []
    for front in (True, False):
        side = rng.choice((-1.0, 1.0))
        parts.append(crack(rng, (side * (KEEP_OUT_HALF + 1.0), KEEP_OUT_TOP + 1.0), 90.0 - side * 35.0, front))
        parts.append(crack(rng, (rng.uniform(-240.0, 240.0), 298.0), -90.0 + rng.uniform(-25.0, 25.0), front))
        for _ in range(3 if front else 2):
            parts.append(chip(rng, _free_spot(rng), front))
    return merge(*parts)


def return_wall(seed: int, tint: Color) -> Mesh:
    rng = random.Random(seed)
    return merge(wall_body(rng, tint), skirting(rng, tint), wear(rng), door_frame(True))


def build() -> list[Asset]:
    assets = []
    for k, (label, tint) in enumerate(VARIANTS):
        spec = AssetSpec(f"SM_ReturnWall_{label}_01", "wall", 2000, door_clear=True, pivot="portal",
                         size_range=(WIDTH - 1.0, WIDTH + 1.0),
                         notes=f"zócalo {label.lower()}; pared en X ∈ [−20, 0], hueco libre 140 × 230")
        assets.append(Asset(spec, return_wall(SEED + 31 * k, tint), 0.06))
    return assets
