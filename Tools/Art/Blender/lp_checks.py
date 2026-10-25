"""Comprobaciones puras de los kits: presupuestos, hueco de puerta libre, pivote y especificación.

Las medidas de la puerta salen de Source/LiminalPortals (ALiminalHub, ALiminalBiome, ALiminalPortal,
LiminalWorldLayout::MakeDoorOpening): hueco 140 × 230 cm, marco 12 cm, pared de 20 cm, el plano del
portal en la cara visible de la pared (X = 0) y la pared en X ∈ [−20, 0].
"""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Sequence

from lp_geom import Mesh, Vec

DOOR_WIDTH = 140.0
DOOR_HEIGHT = 230.0
DOOR_FRAME = 12.0
WALL_THICKNESS = 20.0
PORTAL_FRONT_OVERHANG = 5.0
FREESTANDING_WALL = (20.0, 600.0, 350.0)  # ALiminalBiome::WallSize (X grosor, Y ancho, Z alto)
LIGHT_PANEL = (120.0, 60.0)  # FHubLayout::LightPanelSize
FBX_UNIT_SCALE = 100.0  # FBX en metros, como Tools/Art/Packs: UE importa con import_uniform_scale = 100

# Presupuestos de docs/ARTE.md; las categorías que el documento no fija van marcadas como propias.
CATEGORY_BUDGETS = {
    "tree": 3000,
    "plant": 800,
    "rock": 600,
    "hub_prop": 1500,
    "wall": 2000,
    "crop_instance": 300,  # encargo: mata de trigo instanciada en filas
    "prop": 1500,          # propio: props de bioma (balas, cerca, espantapájaros)
    "horizon": 3000,       # propio: piezas de horizonte (icebergs, granero)
}


@dataclass(frozen=True)
class AssetSpec:
    """Lo que el comprobador exige a una malla exportada."""
    name: str
    category: str
    budget: int
    door_clear: bool = False
    expect_emissive: bool = False
    size_range: tuple[float, float] | None = None  # dimensión mayor admitida en cm
    exact_bbox: tuple[Vec, Vec] | None = None       # bbox esperado (± tolerancia)
    pivot: str = "base_center"                      # base_center | portal | wall | ceiling | fence
    notes: str = ""

    def __post_init__(self) -> None:
        cap = CATEGORY_BUDGETS.get(self.category)
        if cap is None:
            raise ValueError(f"{self.name}: categoría desconocida {self.category!r}")
        if self.budget > cap:
            raise ValueError(f"{self.name}: presupuesto {self.budget} > tope de {self.category} ({cap})")


@dataclass(frozen=True)
class Asset:
    spec: AssetSpec
    mesh: Mesh
    gradient: float = 0.12
    extra: dict = field(default_factory=dict)


# --- Intersección triángulo-caja (Akenine-Möller, teorema del eje separador) --------------------

def _sub(a: Vec, b: Vec) -> Vec:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _cross(a: Vec, b: Vec) -> Vec:
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _dot(a: Vec, b: Vec) -> float:
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def tri_box_overlap(tri: Sequence[Vec], center: Vec, half: Vec) -> bool:
    """True si el triángulo corta el interior de la caja (centro, semiejes)."""
    v = [_sub(p, center) for p in tri]
    axes_box = ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0))
    edges = [_sub(v[1], v[0]), _sub(v[2], v[1]), _sub(v[0], v[2])]

    def separated(axis: Vec) -> bool:
        if _dot(axis, axis) < 1e-18:
            return False
        p = [_dot(axis, q) for q in v]
        r = sum(half[i] * abs(_dot(axis, axes_box[i])) for i in range(3))
        return min(p) >= r or max(p) <= -r

    for e in edges:
        for a in axes_box:
            if separated(_cross(a, e)):
                return False
    for a in axes_box:
        if separated(a):
            return False
    return not separated(_cross(edges[0], edges[1]))


def door_clearance_hits(tris: Sequence[Sequence[Vec]], width: float = DOOR_WIDTH, height: float = DOOR_HEIGHT,
                        margin: float = 0.05, depth: float = 10000.0) -> int:
    """Triángulos que invaden el hueco libre |y| < width/2, 0 < z < height (cualquier X).

    `margin` encoge el hueco para que las caras que lo tocan justo en el borde no cuenten.
    """
    half = (depth, width * 0.5 - margin, height * 0.5 - margin)
    center = (0.0, 0.0, height * 0.5)
    return sum(1 for t in tris if tri_box_overlap(t, center, half))


def bbox_of(points: Sequence[Vec]) -> tuple[Vec, Vec]:
    if not points:
        raise ValueError("sin puntos")
    xs, ys, zs = zip(*points)
    return (min(xs), min(ys), min(zs)), (max(xs), max(ys), max(zs))


def evaluate(spec: AssetSpec, measured: dict, tol: float = 0.05) -> dict:
    """Compara lo medido en el FBX con la especificación. `measured` trae tris, bbox_min/max (cm),
    materials, color_layers, emissive_corners, unit_scale, door_hits y lcl_scaling."""
    lo, hi = measured["bbox_min"], measured["bbox_max"]
    size = [hi[i] - lo[i] for i in range(3)]
    checks = {
        "tris_within_budget": measured["tris"] <= spec.budget,
        "z_min_is_zero": abs(lo[2]) <= tol,
        "single_material": measured["materials"] == 1,
        "has_col_attribute": "Col" in measured["color_layers"],
        "unit_is_m_scale_100": abs(measured["unit_scale"] - FBX_UNIT_SCALE) <= 1e-6,
        "no_object_scale": all(abs(s - 1.0) <= 1e-6 for s in measured["lcl_scaling"]),
        "emissive_as_expected": (measured["emissive_corners"] > 0) == spec.expect_emissive,
    }
    if spec.pivot == "base_center":
        checks["pivot_centered_xy"] = all(abs(lo[i] + hi[i]) <= 1.0 for i in (0, 1))
    if spec.door_clear:
        checks["door_clear_140x230"] = measured["door_hits"] == 0
    if spec.size_range:
        checks["size_in_range"] = spec.size_range[0] <= max(size) <= spec.size_range[1]
    if spec.exact_bbox:
        elo, ehi = spec.exact_bbox
        checks["bbox_matches"] = all(abs(lo[i] - elo[i]) <= 0.5 and abs(hi[i] - ehi[i]) <= 0.5 for i in range(3))
    return checks
