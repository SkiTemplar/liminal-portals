"""Kit Crops (bioma de cultivos, Farmland en el código): trigo, maíz, fila verde, balas de heno, cerca, espantapájaros y granero.

Geometría pura y determinista. Pivote en el centro de la base salvo el tramo de cerca, que lo tiene
en el pie de su poste inicial y se extiende 300 cm hacia +Y (encadenar tramos cada 300 cm).
"""
from __future__ import annotations

import math
import random

from lp_checks import Asset, AssetSpec
from lp_geom import (
    Color, Mesh, Vec, add, face_normal, hex_color, make_mesh, merge, mix, normalize, orient_convex, pivot_base_center,
    polygon_normal, recolor, rot_x, scale, shade, sub, transform,
)
from lp_shapes import box, convex_hull, cylinder_between, ellipsoid, frustum, strip

KIT_NAME = "Crops"
SEED = 4404

WHEAT, WHEAT_DARK = hex_color("#E3C565"), hex_color("#C9A54A")
GREEN, GREEN_DARK = hex_color("#8DB45A"), hex_color("#5E8C3A")
SOIL, SOIL_DARK = hex_color("#7A5434"), hex_color("#5C3E26")
WOOD = hex_color("#A47B4E")
BARN = hex_color("#A63D2F")
HAY = hex_color("#D8B65A")
FENCE_SPAN = 300.0


def _bend(base: Vec, direction: Vec, length: float, droop: float, t: float) -> Vec:
    """Punto a lo largo de una hoja arqueada: avanza en `direction` y cae con t²."""
    p = add(base, scale(direction, length * t))
    return (p[0], p[1], p[2] - droop * t * t)


def leaf(base: Vec, yaw: float, rise: float, length: float, width: float, droop: float, color: Color,
         segments: int = 4) -> Mesh:
    """Hoja estrecha de doble cara con nervio central plegado (facetas visibles)."""
    d = normalize((math.cos(math.radians(yaw)) * math.cos(math.radians(rise)),
                   math.sin(math.radians(yaw)) * math.cos(math.radians(rise)), math.sin(math.radians(rise))))
    side = (-math.sin(math.radians(yaw)), math.cos(math.radians(yaw)), 0.0)
    spine, left, right = [], [], []
    for k in range(segments + 1):
        t = k / segments
        w = width * math.sin(math.pi * min(1.0, t * 1.15 + 0.08)) * (1.0 - t * 0.5)
        p = _bend(base, d, length, droop, t)
        spine.append(add(p, (0.0, 0.0, w * 0.25)))
        left.append(add(p, scale(side, w * 0.5)))
        right.append(add(p, scale(side, -w * 0.5)))
    return merge(strip(left, spine, color), strip(spine, right, shade(color, 0.92)))


def wheat_clump(rng: random.Random, stalks: int, height: float) -> Mesh:
    """Mata de trigo para instanciar en filas: tallos de 3 lados y espigas romboidales."""
    parts = []
    for _ in range(stalks):
        a, r = rng.uniform(0, 2 * math.pi), rng.uniform(0.0, 9.0)
        base = (math.cos(a) * r, math.sin(a) * r, 0.0)
        h = height * rng.uniform(0.82, 1.0)
        lean = rng.uniform(4.0, 14.0)
        top = (base[0] + math.cos(a) * h * math.tan(math.radians(lean)),
               base[1] + math.sin(a) * h * math.tan(math.radians(lean)), h)
        parts.append(cylinder_between(base, top, 0.7, 0.4, 3, WHEAT_DARK, caps=False, phase=rng.uniform(0, 2)))
        axis = normalize(sub(top, base))
        tip = add(top, scale(axis, 13.0))
        mid = add(top, scale(axis, 5.0))
        head_base = cylinder_between(top, mid, 0.4, 2.0, 4, WHEAT, caps=False)
        head_tip = cylinder_between(mid, tip, 2.0, 0.0, 4, WHEAT, caps=False)
        parts += [head_base, head_tip]
    for k in range(3):
        parts.append(leaf((0.0, 0.0, rng.uniform(8, 20)), 120.0 * k + rng.uniform(-20, 20), rng.uniform(35, 55),
                          rng.uniform(22, 32), 1.6, 9.0, WHEAT_DARK, segments=2))
    return pivot_base_center(merge(*parts))


def corn_plant(rng: random.Random) -> Mesh:
    """Planta de maíz de ~2,2 m: caña de 6 lados, hojas arqueadas alternas, dos mazorcas y penacho."""
    h = 215.0
    parts = [cylinder_between((0, 0, 0), (0, 0, h * 0.55), 1.7, 1.3, 6, GREEN_DARK, caps=False),
             cylinder_between((0, 0, h * 0.55), (0, 0, h), 1.3, 0.6, 6, GREEN_DARK, caps=False)]
    for k in range(9):
        z = 25.0 + k * 19.0
        yaw = (180.0 * k) + rng.uniform(-30, 30)
        parts.append(leaf((0.0, 0.0, z), yaw, rng.uniform(30, 50), rng.uniform(60, 85) * (1.0 - k * 0.05),
                          rng.uniform(9.0, 11.5), rng.uniform(25, 40), GREEN if k % 2 else shade(GREEN, 0.9)))
    for z, yaw in ((95.0, 40.0), (118.0, 220.0)):
        d = (math.cos(math.radians(yaw)), math.sin(math.radians(yaw)))
        base = (d[0] * 2.0, d[1] * 2.0, z)
        tip = (d[0] * 9.0, d[1] * 9.0, z + 20.0)
        parts.append(cylinder_between(base, tip, 2.6, 0.8, 6, mix(GREEN, WHEAT, 0.45)))
    for k in range(6):
        yaw = 60.0 * k + rng.uniform(-10, 10)
        tip = (math.cos(math.radians(yaw)) * 12.0, math.sin(math.radians(yaw)) * 12.0, h + rng.uniform(12, 22))
        parts.append(cylinder_between((0, 0, h - 2.0), tip, 0.5, 0.0, 3, WHEAT, caps=False))
    return pivot_base_center(merge(*parts))


def crop_row(rng: random.Random) -> Mesh:
    """Fila de 3 m a lo largo de X: caballón de tierra y nueve plantas verdes jóvenes."""
    seg, length = 6, 300.0
    profile = ((-28.0, 0.0), (-16.0, 9.0), (0.0, 13.0), (16.0, 9.0), (28.0, 0.0))
    verts, faces, colors = [], [], []
    for i in range(seg + 1):
        x = -length * 0.5 + i * length / seg
        for y, z in profile:
            j = rng.uniform(-1.5, 1.5) if 0 < i < seg and z > 0 else 0.0
            verts.append((x, y + j, z + (j if z > 0 else 0.0)))
    n = len(profile)
    for i in range(seg):
        for k in range(n - 1):
            a = i * n + k
            faces.append((a, a + n, a + n + 1, a + 1))
            colors.append(SOIL if k in (1, 2) else SOIL_DARK)
    ridge = make_mesh(verts, faces, colors)
    ridge = make_mesh(ridge.verts, [f if _up(ridge, f) else tuple(reversed(f)) for f in ridge.faces], ridge.colors)
    caps = []
    for x in (-length * 0.5, length * 0.5):
        cap = make_mesh([(x, y, z) for y, z in profile], [tuple(range(n))], SOIL_DARK)
        caps.append(cap if (x > 0) == (_normal_x(cap) > 0) else make_mesh(cap.verts, [tuple(reversed(cap.faces[0]))], SOIL_DARK))
    plants = []
    for k in range(9):
        x = -length * 0.5 + (k + 0.5) * length / 9 + rng.uniform(-4, 4)
        for m in range(5):
            yaw = 72.0 * m + rng.uniform(-15, 15) + k * 17.0
            color = GREEN if (m + k) % 2 else GREEN_DARK
            plants.append(leaf((x, 0.0, 12.0), yaw, rng.uniform(35, 60), rng.uniform(13, 19), rng.uniform(6, 8), 7.0,
                               color, segments=2))
    return pivot_base_center(merge(ridge, *caps, *plants))


def _up(mesh: Mesh, face) -> bool:
    return polygon_normal([mesh.verts[i] for i in face])[2] >= 0


def _normal_x(mesh: Mesh) -> float:
    return face_normal(mesh, 0)[0]


def round_bale(rng: random.Random) -> Mesh:
    """Bala redonda de 150 × 120 tumbada sobre el eje Y, con espiral insinuada en las tapas."""
    sides, r, w = 12, 75.0, 120.0
    phase = math.pi / sides
    body = frustum((0, 0, -w * 0.5), r, r, w, sides, HAY, phase=phase, cap_bottom=False, cap_top=False)
    rings = []
    for z0, z1, rr in ((-w * 0.5 - 1.5, -w * 0.5, 0.0), (w * 0.5, w * 0.5 + 1.5, 0.0)):
        rings.append(frustum((0, 0, z0), r * 0.98, r * 0.8, z1 - z0, sides, shade(HAY, 0.88), phase=phase,
                             cap_bottom=False) if z0 > 0 else
                     frustum((0, 0, z0), r * 0.8, r * 0.98, z1 - z0, sides, shade(HAY, 0.88), phase=phase,
                             cap_top=False))
    bale = transform(merge(body, *rings), rot_x(90.0))
    bands = [transform(frustum((0, 0, y - 1.5), r + 0.6, r + 0.6, 3.0, sides, shade(WOOD, 0.8), phase=phase,
                                cap_bottom=False, cap_top=False), rot_x(90.0)) for y in (-30.0, 30.0)]
    return pivot_base_center(merge(bale, *bands))


def square_bale(rng: random.Random) -> Mesh:
    """Bala pequeña de 90 × 46 × 36 achaflanada, con dos cuerdas."""
    lx, ly, lz, c = 45.0, 23.0, 36.0, 4.0
    pts = []
    for sx in (-1, 1):
        for sy in (-1, 1):
            for z in (0.0, lz):
                pts.append((sx * (lx - c), sy * (ly - c), z))
            for z in (c, lz - c):
                pts += [(sx * lx, sy * (ly - c), z), (sx * (lx - c), sy * ly, z)]
    body = recolor(convex_hull(pts, HAY), lambda cen, n, col: shade(HAY, 1.0 if n[2] > 0.5 else 0.94))
    cords = [box((x - 1.0, -ly - 0.4, -0.01), (x + 1.0, ly + 0.4, lz + 0.4), shade(WOOD, 0.8), skip=("-z",))
             for x in (-20.0, 20.0)]
    return pivot_base_center(merge(body, *cords))


def fence_post(rng: random.Random, height: float = 130.0) -> Mesh:
    shaft = frustum((0, 0, 0), 7.0, 6.5, height - 8.0, 6, WOOD, phase=rng.uniform(0, 1), cap_top=False)
    cap = frustum((0, 0, height - 8.0), 6.5, 1.5, 8.0, 6, shade(WOOD, 1.05), phase=0.2, cap_bottom=False)
    return merge(shaft, cap)


def fence_section(rng: random.Random) -> Mesh:
    """Poste inicial + dos largueros de 3 m hacia +Y (el siguiente tramo pone el siguiente poste)."""
    rails = []
    for z in (55.0, 100.0):
        z0, z1 = z + rng.uniform(-2, 2), z + rng.uniform(-3, 3)
        rails.append(cylinder_between((6.0, 0.0, z0), (6.0, FENCE_SPAN, z1), 4.5, 4.0, 4,
                                      shade(WOOD, rng.uniform(0.9, 1.0)), phase=math.pi / 4))
    return merge(fence_post(rng), *rails)


def scarecrow(rng: random.Random) -> Mesh:
    """Espantapájaros mirando a +X: poste en cruz, camisa roja, cabeza de saco y sombrero."""
    sack = mix(WOOD, HAY, 0.5)
    parts = [
        cylinder_between((0, 0, 0), (0, 0, 175), 3.5, 3.0, 6, WOOD),
        cylinder_between((0, -62, 148), (0, 62, 148), 2.8, 2.8, 6, WOOD),
        frustum((0, 0, 92), 20.0, 24.0, 58.0, 6, BARN, phase=0.5, stretch=(0.55, 1.0)),
        cylinder_between((0, -12, 147), (0, -55, 145), 7.5, 6.0, 6, shade(BARN, 0.9)),
        cylinder_between((0, 12, 147), (0, 55, 145), 7.5, 6.0, 6, shade(BARN, 0.9)),
        cylinder_between((0, -7, 95), (2, -11, 52), 7.0, 6.0, 6, SOIL_DARK),
        cylinder_between((0, 7, 95), (2, 11, 52), 7.0, 6.0, 6, SOIL_DARK),
        ellipsoid((0, 0, 170), (13.0, 12.0, 15.0), 8, 5, sack),
        frustum((0, 0, 181), 25.0, 23.0, 2.0, 10, shade(SOIL_DARK, 1.2)),
        frustum((0, 0, 183), 12.0, 9.0, 14.0, 8, shade(SOIL_DARK, 1.2)),
        box((11.5, -5.5, 171.0), (13.5, -2.5, 174.0), SOIL_DARK),
        box((11.5, 2.5, 171.0), (13.5, 5.5, 174.0), SOIL_DARK),
    ]
    for y, d in ((-55, -1), (55, 1)):
        for k in range(4):
            tip = (rng.uniform(-6, 6), y + d * rng.uniform(10, 16), 145 + rng.uniform(-9, 5))
            parts.append(cylinder_between((0, y, 145), tip, 2.2, 0.0, 3, HAY, caps=False))
    for k in range(6):
        a = 60.0 * k
        tip = (math.cos(math.radians(a)) * 14, math.sin(math.radians(a)) * 14, 158 + rng.uniform(-3, 3))
        parts.append(cylinder_between((0, 0, 156), tip, 2.0, 0.0, 3, HAY, caps=False))
    return pivot_base_center(merge(*parts))


def barn() -> Mesh:
    """Granero pequeño de horizonte (8 × 6 m, 6,2 m de alto), con el portón en la cara +X."""
    gable = ((-300.0, 0.0), (300.0, 0.0), (300.0, 350.0), (220.0, 520.0), (0.0, 620.0), (-220.0, 520.0),
             (-300.0, 350.0))
    n = len(gable)
    verts = [(x, y, z) for x in (-400.0, 400.0) for (y, z) in gable]
    faces = [tuple(range(n)), tuple(n + k for k in range(n))]
    faces += [(k, (k + 1) % n, n + (k + 1) % n, n + k) for k in range(1, n)]
    body = orient_convex(make_mesh(verts, faces, BARN))
    roof_color = shade(SOIL_DARK, 0.95)
    roof = []
    for (y0, z0), (y1, z1) in zip(gable[2:6], gable[3:7]):
        normal = normalize((0.0, -(z1 - z0), y1 - y0))
        if normal[2] < 0:
            normal = scale(normal, -1.0)
        quad = [(x, y, z) for x in (-430.0, 430.0) for (y, z) in ((y0, z0), (y1, z1))]
        lifted = [add(p, scale(normal, 1.0)) for p in quad] + [add(p, scale(normal, 14.0)) for p in quad]
        roof.append(convex_hull(_extend_eave(lifted, y0, y1), roof_color))
    trim = shade(HAY, 1.05)
    door = [box((400.0, -110.0, 0.0), (404.0, 110.0, 300.0), shade(BARN, 0.85), skip=("-z", "-x")),
            box((404.0, -110.0, 0.0), (408.0, -98.0, 300.0), trim, skip=("-z", "-x")),
            box((404.0, 98.0, 0.0), (408.0, 110.0, 300.0), trim, skip=("-z", "-x")),
            box((404.0, -110.0, 288.0), (408.0, 110.0, 300.0), trim, skip=("-x",)),
            box((403.0, -60.0, 400.0), (407.0, 60.0, 480.0), trim, skip=("-x",))]
    brace = transform(box((-2.0, -6.0, -150.0), (2.0, 6.0, 150.0), trim), rot_x(36.0), (406.0, 0.0, 145.0))
    corners = [box((sx * 400.0 - 6.0, sy * 300.0 - 6.0, 0.0), (sx * 400.0 + 6.0, sy * 300.0 + 6.0, 350.0), trim,
                   skip=("-z",)) for sx in (-1, 1) for sy in (-1, 1)]
    return pivot_base_center(merge(body, *roof, *door, brace, *corners))


def _extend_eave(points: list[Vec], y0: float, y1: float) -> list[Vec]:
    """Alarga el faldón inferior del tejado 30 cm hacia fuera (alero)."""
    out = []
    for p in points:
        if abs(abs(p[1]) - 300.0) < 20.0 and p[2] < 380.0:
            out.append((p[0], p[1] + math.copysign(30.0, p[1]), p[2] - 15.0))
        else:
            out.append(p)
    return out


def build() -> list[Asset]:
    rng = random.Random(SEED)
    return [
        Asset(AssetSpec("SM_Crops_Wheat_01", "crop_instance", 300, size_range=(60.0, 130.0),
                        notes="mata para instanciar en filas"), wheat_clump(rng, 13, 100.0), 0.3),
        Asset(AssetSpec("SM_Crops_Wheat_02", "crop_instance", 300, size_range=(60.0, 130.0),
                        notes="mata más baja y rala"), wheat_clump(rng, 10, 85.0), 0.3),
        Asset(AssetSpec("SM_Crops_Corn_01", "plant", 800, size_range=(180.0, 260.0)), corn_plant(rng), 0.25),
        Asset(AssetSpec("SM_Crops_CropRow_01", "plant", 800, size_range=(299.0, 302.0),
                        notes="fila de 3 m a lo largo de X"), crop_row(rng), 0.15),
        Asset(AssetSpec("SM_Crops_HayBaleRound_01", "prop", 1500, size_range=(140.0, 160.0)), round_bale(rng), 0.12),
        Asset(AssetSpec("SM_Crops_HayBaleSquare_01", "prop", 1500, size_range=(85.0, 95.0)), square_bale(rng), 0.12),
        Asset(AssetSpec("SM_Crops_FencePost_01", "prop", 1500, size_range=(125.0, 135.0)), fence_post(rng), 0.15),
        Asset(AssetSpec("SM_Crops_FenceSection_01", "prop", 1500, pivot="fence", size_range=(300.0, 312.0),
                        notes="pivote al pie del poste inicial; tramo de 300 cm hacia +Y"), fence_section(rng), 0.15),
        Asset(AssetSpec("SM_Crops_Scarecrow_01", "prop", 1500, size_range=(150.0, 210.0)), scarecrow(rng), 0.1),
        Asset(AssetSpec("SM_Crops_Barn_01", "horizon", 3000, size_range=(800.0, 900.0),
                        notes="portón en +X"), barn(), 0.12),
    ]
