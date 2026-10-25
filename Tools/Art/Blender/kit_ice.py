"""Kit Ice (Antártida): bloques, esquirlas, cresta de presión, montículos de nieve y piezas de horizonte.

Geometría pura y determinista (semilla fija). Envolventes convexas para el hielo (facetas nítidas) y
mallas de altura abiertas por debajo para la nieve. Pivote en el centro de la base, z mínima 0.
"""
from __future__ import annotations

import math
import random

from lp_checks import Asset, AssetSpec
from lp_geom import (
    Color, Mesh, Vec, drop_to_ground, hex_color, mat_mul, merge, pivot_base_center, recolor, rot_x, rot_y, rot_z,
    transform, translate,
)
from lp_shapes import convex_hull, heightfield_disk

KIT_NAME = "Ice"
SEED = 3303

SNOW = (hex_color("#F4F7FA"), hex_color("#DDE7EF"))
ICE_LIGHT, ICE_MID, ICE_DEEP = hex_color("#A9D6EA"), hex_color("#6FB3D6"), hex_color("#3C83B0")


def ice_colorizer(rng: random.Random, height: float, snow_cap: float = 0.72):
    """Color por cara: techo nevado, y bandas de hielo de claro (arriba) a profundo (abajo)."""
    bands = (ICE_DEEP, ICE_MID, ICE_LIGHT)

    def color(center: Vec, normal: Vec, _old: Color) -> Color:
        if normal[2] > snow_cap:
            return SNOW[0] if rng.random() < 0.7 else SNOW[1]
        if normal[2] < -0.3:
            return ICE_DEEP
        t = max(0.0, min(0.999, center[2] / max(height, 1e-6)))
        band = int(t * 3)
        if rng.random() < 0.25:
            band = max(0, min(2, band + rng.choice((-1, 1))))
        return bands[band]

    return color


def snow_colorizer(rng: random.Random):
    def color(_center: Vec, normal: Vec, _old: Color) -> Color:
        return SNOW[0] if normal[2] > 0.88 or (normal[2] > 0.7 and rng.random() < 0.5) else SNOW[1]
    return color


def _paint_ice(rng: random.Random, mesh: Mesh) -> Mesh:
    _, hi = mesh.bbox()
    return recolor(mesh, ice_colorizer(rng, hi[2]))


def ice_block(rng: random.Random, size: Vec, tilt: float) -> Mesh:
    """Bloque cúbico facetado: esquinas desplazadas y achaflanadas, base apoyada."""
    lx, ly, lz = size[0] * 0.5, size[1] * 0.5, size[2]
    pts: list[Vec] = []
    for sx in (-1, 1):
        for sy in (-1, 1):
            pts.append((sx * lx * rng.uniform(0.8, 1.0), sy * ly * rng.uniform(0.8, 1.0), 0.0))
            for _ in range(3):
                pts.append((sx * lx * rng.uniform(0.62, 0.95), sy * ly * rng.uniform(0.62, 0.95),
                            lz * rng.uniform(0.82, 1.0)))
    for k in range(10):
        a = 2.0 * math.pi * (k + rng.uniform(-0.3, 0.3)) / 10
        bulge = rng.uniform(0.98, 1.1)
        pts.append((math.cos(a) * lx * bulge, math.sin(a) * ly * bulge, lz * rng.uniform(0.2, 0.7)))
    hull = convex_hull(pts, ICE_MID)
    tilted = transform(hull, mat_mul(rot_z(rng.uniform(0, 360)), rot_y(tilt)))
    return _paint_ice(rng, pivot_base_center(tilted))


def shard(rng: random.Random, height: float, radius: float, lean: float) -> Mesh:
    """Esquirla alargada e inclinada: anillo de base, anillo medio y punta doble."""
    dx = math.tan(math.radians(lean)) * height
    pts: list[Vec] = []
    sides = 5
    for k in range(sides):
        a = 2.0 * math.pi * k / sides + rng.uniform(-0.3, 0.3)
        r = radius * rng.uniform(0.7, 1.0)
        pts.append((math.cos(a) * r, math.sin(a) * r * 0.7, 0.0))
        r2 = radius * rng.uniform(0.45, 0.7)
        pts.append((dx * 0.5 + math.cos(a) * r2, math.sin(a) * r2 * 0.7, height * rng.uniform(0.45, 0.6)))
    pts.append((dx, 0.0, height))
    pts.append((dx * 0.9 + radius * 0.2, radius * 0.1, height * 0.9))
    return convex_hull(pts, ICE_LIGHT)


def shard_cluster(rng: random.Random, count: int, height: float) -> Mesh:
    parts = []
    for k in range(count):
        h = height * rng.uniform(0.55, 1.0)
        piece = shard(rng, h, h * rng.uniform(0.16, 0.24), rng.uniform(5.0, 28.0))
        yaw = 360.0 * k / count + rng.uniform(-25, 25)
        offset = (rng.uniform(-0.15, 0.15) * height, rng.uniform(-0.15, 0.15) * height, 0.0)
        parts.append(transform(piece, rot_z(yaw), offset))
    return _paint_ice(rng, pivot_base_center(merge(*parts)))


def flat_shard(rng: random.Random, length: float) -> Mesh:
    """Lámina de hielo partida y medio levantada (cuña plana)."""
    pts: list[Vec] = []
    for k in range(7):
        a = 2.0 * math.pi * k / 7 + rng.uniform(-0.2, 0.2)
        r = length * 0.5 * rng.uniform(0.6, 1.0)
        pts.append((math.cos(a) * r, math.sin(a) * r * 0.55, 0.0))
        pts.append((math.cos(a) * r * 0.9, math.sin(a) * r * 0.5, length * rng.uniform(0.06, 0.1)))
    slab = convex_hull(pts, ICE_LIGHT)
    return _paint_ice(rng, pivot_base_center(transform(slab, rot_y(rng.uniform(18.0, 30.0)))))


def snow_mound(rng: random.Random, radius: float, height: float, stretch: tuple[float, float]) -> Mesh:
    rings = (0.0, 0.28, 0.55, 0.8, 1.0)
    heights = (height, height * 0.93, height * 0.66, height * 0.28, 0.0)
    jitter = {(r, s): rng.uniform(-0.14, 0.14) for r in range(len(rings)) for s in range(12)}
    mound = heightfield_disk([radius * r for r in rings], heights, 12, SNOW[0],
                             radial_jitter=lambda r, s: jitter[(r, s)], stretch=stretch)
    return recolor(pivot_base_center(transform(mound, rot_z(rng.uniform(0, 180)))), snow_colorizer(rng))


def pressure_ridge(rng: random.Random, length: float) -> Mesh:
    """Cresta de presión: losas de hielo volcadas a lo largo de Y sobre un caballón de nieve."""
    slabs = []
    count = 30
    for k in range(count):
        y = -length * 0.5 + (k + 0.5) * length / count + rng.uniform(-15.0, 15.0)
        lx, ly, lz = rng.uniform(140, 280), rng.uniform(110, 200), rng.uniform(35, 60)
        pts = [(sx * lx * 0.5 * rng.uniform(0.85, 1.0), sy * ly * 0.5 * rng.uniform(0.85, 1.0), sz * lz * 0.5)
               for sx in (-1, 1) for sy in (-1, 1) for sz in (-1, 1)]
        slab = convex_hull(pts, ICE_MID)
        tilt = rng.uniform(25.0, 70.0) * (1 if k % 2 else -1)
        rotated = transform(slab, mat_mul(rot_z(rng.uniform(-20, 20)), rot_y(tilt)))
        placed = drop_to_ground(translate(rotated, (rng.uniform(-30, 30), y, 0.0)))
        slabs.append(placed)
    ice = recolor(merge(*slabs), ice_colorizer(rng, 260.0))
    berm = heightfield_disk([0.0, 100.0, 190.0, 300.0], [125.0, 105.0, 55.0, 0.0], 10, SNOW[0],
                            radial_jitter=lambda r, s: rng.uniform(-0.1, 0.1), stretch=(0.55, length / 560.0))
    return pivot_base_center(merge(ice, recolor(berm, snow_colorizer(rng))))


def _ring(rng: random.Random, count: int, rx: float, ry: float, z: float, jitter: float) -> list[Vec]:
    pts = []
    for k in range(count):
        a = 2.0 * math.pi * (k + rng.uniform(-0.3, 0.3)) / count
        r = rng.uniform(1.0 - jitter, 1.0)
        pts.append((math.cos(a) * rx * r, math.sin(a) * ry * r, z))
    return pts


def tabular_iceberg(rng: random.Random) -> Mesh:
    """Iceberg tabular de ~48 × 26 × 14 m con dos trozos desprendidos."""
    h = 1400.0
    main = convex_hull(_ring(rng, 20, 2400, 1300, 0.0, 0.12) + _ring(rng, 20, 2350, 1270, h * 0.5, 0.08)
                       + [(x, y, h * rng.uniform(0.95, 1.0)) for x, y, _ in _ring(rng, 22, 2200, 1180, 0.0, 0.1)],
                       ICE_MID)
    chunks = []
    for side in (-1, 1):
        cx, ch = side * rng.uniform(1900, 2200), h * rng.uniform(0.45, 0.65)
        base = [(cx + x, y + side * 300, z) for x, y, z in _ring(rng, 8, 700, 500, 0.0, 0.2)]
        top = [(cx + x, y + side * 300, ch * rng.uniform(0.85, 1.0)) for x, y, _ in _ring(rng, 8, 520, 380, 0.0, 0.2)]
        chunks.append(convex_hull(base + top, ICE_MID))
    return _paint_ice(rng, pivot_base_center(merge(main, *chunks)))


def pinnacle_iceberg(rng: random.Random) -> Mesh:
    """Iceberg de picos: tres envolventes solapadas, ~36 m de largo y 24 m de alto."""
    peaks = []
    for cx, height, rx, ry in ((0.0, 2400.0, 1300.0, 950.0), (1100.0, 1500.0, 900.0, 700.0),
                               (-1200.0, 1000.0, 800.0, 650.0)):
        base = [(cx + x, y, z) for x, y, z in _ring(rng, 10, rx, ry, 0.0, 0.15)]
        mid = [(cx + x * 0.6 + rng.uniform(-80, 80), y * 0.6, height * rng.uniform(0.45, 0.6))
               for x, y, _ in _ring(rng, 7, rx, ry, 0.0, 0.2)]
        tip = [(cx + rng.uniform(-200, 200), rng.uniform(-150, 150), height * rng.uniform(0.92, 1.0)) for _ in range(3)]
        peaks.append(convex_hull(base + mid + tip, ICE_MID))
    return _paint_ice(rng, pivot_base_center(merge(*peaks)))


def serac_field(rng: random.Random) -> Mesh:
    """Seracs: torres de hielo inclinadas en fila, ~30 × 12 m y hasta ~26 m de alto."""
    towers = []
    for k in range(6):
        h = rng.uniform(1200.0, 2600.0)
        w, d = rng.uniform(380, 650), rng.uniform(300, 520)
        pts = [(sx * w * 0.5 * rng.uniform(0.8, 1.0), sy * d * 0.5 * rng.uniform(0.8, 1.0), 0.0)
               for sx in (-1, 1) for sy in (-1, 1)]
        pts += [(sx * w * 0.38 * rng.uniform(0.6, 1.0), sy * d * 0.38 * rng.uniform(0.6, 1.0), h * rng.uniform(0.8, 1.0))
                for sx in (-1, 1) for sy in (-1, 1)]
        pts.append((rng.uniform(-w, w) * 0.2, rng.uniform(-d, d) * 0.2, h * rng.uniform(1.0, 1.08)))
        tower = transform(convex_hull(pts, ICE_MID), mat_mul(rot_z(rng.uniform(-15, 15)), rot_x(rng.uniform(-8, 8))))
        towers.append(translate(tower, (rng.uniform(-300, 300), -1300.0 + k * 520.0, 0.0)))
    ground = [drop_to_ground(t) for t in towers]
    return _paint_ice(rng, pivot_base_center(merge(*ground)))


def build() -> list[Asset]:
    rng = random.Random(SEED)
    assets: list[Asset] = []
    block_sizes = ((120, 95, 80), (220, 160, 130), (300, 180, 90), (90, 80, 140))
    for k, size in enumerate(block_sizes):
        mesh = ice_block(rng, size, rng.uniform(0.0, 12.0))
        assets.append(Asset(AssetSpec(f"SM_Ice_Block_{k + 1:02d}", "rock", 600, size_range=(60.0, 350.0)), mesh, 0.1))
    shards = (shard_cluster(rng, 1, 120.0), shard_cluster(rng, 3, 95.0), flat_shard(rng, 160.0))
    for k, mesh in enumerate(shards):
        assets.append(Asset(AssetSpec(f"SM_Ice_Shard_{k + 1:02d}", "rock", 600, size_range=(30.0, 200.0)), mesh, 0.1))
    assets.append(Asset(AssetSpec("SM_Ice_PressureRidge_01", "prop", 1500, size_range=(800.0, 1600.0),
                                  notes="cresta a lo largo de Y"), pressure_ridge(rng, 1200.0), 0.08))
    mounds = ((220.0, 70.0, (1.4, 1.0)), (150.0, 45.0, (1.0, 1.0)), (260.0, 90.0, (2.0, 0.8)))
    for k, (r, h, st) in enumerate(mounds):
        assets.append(Asset(AssetSpec(f"SM_Ice_SnowMound_{k + 1:02d}", "rock", 600, size_range=(100.0, 1100.0),
                                      notes="abierto por debajo"), snow_mound(rng, r, h, st), 0.06))
    big = (("SM_Ice_Iceberg_01", tabular_iceberg(rng)), ("SM_Ice_Iceberg_02", pinnacle_iceberg(rng)),
           ("SM_Ice_Serac_01", serac_field(rng)))
    for name, mesh in big:
        assets.append(Asset(AssetSpec(name, "horizon", 3000, size_range=(2000.0, 6000.0), notes="horizonte"), mesh, 0.15))
    return assets
