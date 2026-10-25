"""Lógica pura del kit de packs (sin ``bpy``): color sRGB/Oklab, recoloreado a la paleta del
bioma con degradado vertical, medida y pivote, y validación del catálogo y de las mallas
exportadas. Se prueba con pytest sin Blender; los scripts de Blender la aplican.

Adaptado de Explored (``Tools/Packs/normalize_core.py``): aquí no hay atlas de paleta, el color
va solo en el atributo de vértice ``Col`` (sRGB en el FBX), como pide ``docs/ARTE.md``.
"""

from __future__ import annotations

import json
import math
import re
from collections.abc import Iterable, Sequence
from pathlib import Path
from typing import Any, Protocol

RGB = tuple[float, float, float]
Lab = tuple[float, float, float]

BIOMES = ("Jungle", "Desert", "Antarctic", "Farmland")
MESH_RE = re.compile(r"^SM_(?P<biome>[A-Za-z]+)_(?P<thing>[A-Za-z]+)_(?P<nn>\d{2})$")
DATA_FORMATS = (".obj", ".gltf", ".glb", ".fbx")
PIVOT_TOL_M = 0.01
PALETTE_TOL = 0.02
DEFAULT_SPREAD = 0.08


class Point(Protocol):
    @property
    def x(self) -> float: ...
    @property
    def y(self) -> float: ...
    @property
    def z(self) -> float: ...


# ---------------------------------------------------------------------------
# Color
# ---------------------------------------------------------------------------

def hex_to_srgb(h: str) -> RGB:
    """``#rrggbb`` (con o sin ``#``) a sRGB en [0, 1]."""
    h = h.lstrip("#")
    if len(h) != 6 or any(c not in "0123456789abcdefABCDEF" for c in h):
        raise ValueError(f"color hexadecimal no válido: #{h}")
    return (int(h[0:2], 16) / 255.0, int(h[2:4], 16) / 255.0, int(h[4:6], 16) / 255.0)


def srgb_to_linear(c: float) -> float:
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def linear_to_srgb(c: float) -> float:
    c = min(max(c, 0.0), 1.0)
    return 12.92 * c if c <= 0.0031308 else 1.055 * c ** (1 / 2.4) - 0.055


def rgb3(values: Iterable[float]) -> RGB:
    r, g, b = list(values)[:3]
    return (float(r), float(g), float(b))


def oklab(srgb: Sequence[float]) -> Lab:
    r, g, b = (srgb_to_linear(c) for c in srgb[:3])
    l_ = 0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b
    m_ = 0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b
    s_ = 0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b
    l_, m_, s_ = (math.copysign(abs(v) ** (1 / 3), v) for v in (l_, m_, s_))
    return (
        0.2104542553 * l_ + 0.7936177850 * m_ - 0.0040720468 * s_,
        1.9779984951 * l_ - 2.4285922050 * m_ + 0.4505937099 * s_,
        0.0259040371 * l_ + 0.7827717662 * m_ - 0.8086757660 * s_,
    )


def oklab_to_srgb(lab: Sequence[float]) -> RGB:
    """Inversa de ``oklab`` (recorta al gamut sRGB)."""
    L, a, b = lab
    l_ = (L + 0.3963377774 * a + 0.2158037573 * b) ** 3
    m_ = (L - 0.1055613458 * a - 0.0638541728 * b) ** 3
    s_ = (L - 0.0894841775 * a - 1.2914855480 * b) ** 3
    r = 4.0767416621 * l_ - 3.3077115913 * m_ + 0.2309699292 * s_
    g = -1.2684380046 * l_ + 2.6097574011 * m_ - 0.3413193965 * s_
    bl = -0.0041960863 * l_ - 0.7034186147 * m_ + 1.7076147010 * s_
    return (linear_to_srgb(r), linear_to_srgb(g), linear_to_srgb(bl))


def delta_e(a: Sequence[float], b: Sequence[float]) -> float:
    return math.dist(a, b)


def to_hex(srgb: Sequence[float]) -> str:
    return "#" + "".join(f"{round(min(max(c, 0), 1) * 255):02x}" for c in srgb[:3])


def load_json(p: Path) -> dict[str, Any]:
    return json.loads(p.read_text(encoding="utf-8"))


# ---------------------------------------------------------------------------
# Recoloreado
# ---------------------------------------------------------------------------

def compile_rules(recolor: dict[str, Any]) -> tuple[list[tuple[Lab, str]], float, str | None]:
    """Reglas ``from`` (sRGB del pack) → ``to`` (muestra del bioma) en Oklab, tolerancia y default."""
    compiled = [(oklab(hex_to_srgb(r["from"])), r["to"]) for r in recolor.get("rules", [])]
    return compiled, float(recolor.get("tolerance", 0.12)), recolor.get("default")


def pick_swatch(color: Sequence[float], compiled: list[tuple[Lab, str]], tol: float, default: str | None,
                mesh: str = "?") -> str:
    """Muestra de la regla más cercana en Oklab; ``default`` si ninguna está a ``tol`` o menos.

    A igual distancia gana la primera regla, para que no dependa del orden de iteración.
    """
    lab = oklab(color)
    best, dist = None, math.inf
    for flab, to in compiled:
        d = delta_e(flab, lab)
        if d < dist:
            best, dist = to, d
    if best is None or dist > tol:
        if default is None:
            raise RuntimeError(f"{mesh}: color {to_hex(color)} sin regla y sin default")
        return default
    return best


def gradient_srgb(base_hex: str, h: float, spread: float = 0.08) -> RGB:
    """Color sRGB de la muestra a la altura relativa ``h`` (0 abajo, 1 arriba).

    Degradado vertical suave en Oklab: solo cambia la luminosidad, ``-spread/2`` abajo y
    ``+spread/2`` arriba, así que el tono de la paleta se conserva. ``h`` se recorta a [0, 1].
    """
    if spread < 0:
        raise ValueError(f"spread negativo: {spread}")
    h = min(max(h, 0.0), 1.0)
    L, a, b = oklab(hex_to_srgb(base_hex))
    return oklab_to_srgb((min(max(L + spread * (h - 0.5), 0.0), 1.0), a, b))


def share_percent(share: dict[str, float]) -> dict[str, float]:
    """Área por muestra en % (un decimal), de mayor a menor."""
    total = sum(share.values()) or 1.0
    return {k: round(100 * v / total, 1) for k, v in sorted(share.items(), key=lambda kv: -kv[1])}


# ---------------------------------------------------------------------------
# Medida, pivote y presupuesto
# ---------------------------------------------------------------------------

def bbox_dims(vs: Sequence[Point]) -> RGB:
    if not vs:
        raise ValueError("malla sin vértices")
    return (
        max(v.x for v in vs) - min(v.x for v in vs),
        max(v.y for v in vs) - min(v.y for v in vs),
        max(v.z for v in vs) - min(v.z for v in vs),
    )


def axis_value(dims: Sequence[float], axis: str) -> float:
    if axis == "max":
        return max(dims)
    if axis in ("x", "y", "z"):
        return dims["xyz".index(axis)]
    raise ValueError(f"eje de medida no válido: {axis}")


def size_factor(vs: Sequence[Point], size: dict[str, Any]) -> float:
    """Escala uniforme para que el eje ``size.axis`` (x, y, z o max) mida ``size.m`` metros."""
    cur = axis_value(bbox_dims(vs), size.get("axis", "max"))
    if cur <= 0:
        raise ValueError(f"la malla es plana en el eje {size.get('axis', 'max')}")
    return float(size["m"]) / cur


def base_pivot(vs: Sequence[Point]) -> RGB:
    """Centro de la huella (x, y) a la z mínima: el punto que pasa a ser el origen."""
    if not vs:
        raise ValueError("malla sin vértices")
    return (
        (min(v.x for v in vs) + max(v.x for v in vs)) / 2,
        (min(v.y for v in vs) + max(v.y for v in vs)) / 2,
        min(v.z for v in vs),
    )


def decimate_ratio(tris: int, budget: int, margin: float = 0.97) -> float | None:
    """Proporción de colapso para quedar bajo ``budget`` con margen; ``None`` si ya cabe."""
    if budget <= 0:
        raise ValueError(f"presupuesto no válido: {budget}")
    if tris <= budget:
        return None
    return budget * margin / tris


# ---------------------------------------------------------------------------
# Validación
# ---------------------------------------------------------------------------

def in_range(value: float, rng: Sequence[float]) -> bool:
    return rng[0] <= value <= rng[1]


def check_mesh(stats: dict[str, Any], category: dict[str, Any]) -> list[str]:
    """Errores de una malla exportada frente a su categoría (vacío si pasa).

    ``stats``: ``tris``, ``dims`` (x, y, z en m), ``zmin`` (m), ``materials`` y ``hasCol``.
    """
    errors: list[str] = []
    if stats["tris"] > category["budget"]:
        errors.append(f"{stats['tris']} triángulos > presupuesto {category['budget']}")
    for axis, rng in category["range"].items():
        value = axis_value(stats["dims"], axis)
        if not in_range(value, rng):
            errors.append(f"{axis} = {value:.2f} m fuera de [{rng[0]}, {rng[1]}]")
    if abs(stats["zmin"]) > PIVOT_TOL_M:
        errors.append(f"z mínima = {stats['zmin'] * 100:.1f} cm (tolerancia {PIVOT_TOL_M * 100:.0f} cm)")
    center = stats.get("center")
    if center is not None and max(abs(center[0]), abs(center[1])) > PIVOT_TOL_M:
        errors.append(f"pivote fuera del centro de la huella ({center[0] * 100:.1f}, {center[1] * 100:.1f}) cm")
    if stats["materials"] != 1:
        errors.append(f"{stats['materials']} materiales (se espera 1)")
    if not stats["hasCol"]:
        errors.append("sin atributo de color Col")
    off = stats.get("paletteMaxDE")
    if off is not None and off > PALETTE_TOL:
        errors.append(f"color a ΔE {off:.3f} de la paleta (tolerancia {PALETTE_TOL})")
    return errors


def palette_distance(srgb: Sequence[float], swatches: Iterable[str], spread: float = 0.08) -> float:
    """Distancia Oklab de ``srgb`` a la muestra más cercana sin contar el degradado vertical.

    El degradado solo mueve la luminosidad ``±spread/2``: se descuenta esa parte de ΔL.
    """
    L, a, b = oklab(srgb)
    best = math.inf
    for hx in swatches:
        sl, sa, sb = oklab(hex_to_srgb(hx))
        dl = max(abs(L - sl) - spread / 2, 0.0)
        best = min(best, math.sqrt(dl * dl + (a - sa) ** 2 + (b - sb) ** 2))
    return best


def _entry_errors(e: dict[str, Any], catalog: dict[str, Any], palettes: dict[str, Any],
                  packs: set[str]) -> list[str]:
    mesh = e.get("mesh", "?")
    errors: list[str] = []
    m = MESH_RE.match(mesh)
    if not m:
        return [f"{mesh}: nombre fuera de SM_<Bioma>_<Cosa>_<NN>"]
    if m["biome"] != e.get("biome") or e.get("biome") not in BIOMES:
        errors.append(f"{mesh}: bioma {e.get('biome')} no cuadra con el nombre o no existe")
    cat = catalog["categories"].get(e.get("category", ""))
    if cat is None:
        errors.append(f"{mesh}: categoría desconocida {e.get('category')}")
    elif "axis" in e.get("size", {}) and e["size"]["axis"] in cat["range"]:
        if not in_range(float(e["size"]["m"]), cat["range"][e["size"]["axis"]]):
            errors.append(f"{mesh}: size {e['size']['m']} m fuera del rango de {e['category']}")
    if e.get("pack") not in packs:
        errors.append(f"{mesh}: pack desconocido {e.get('pack')}")
    if Path(e.get("file", "")).suffix.lower() not in DATA_FORMATS:
        errors.append(f"{mesh}: {e.get('file')} no es un formato de datos admitido")
    swatches = palettes.get(e.get("biome", ""), {})
    rec = e.get("recolor", {})
    used = [r["to"] for r in rec.get("rules", [])] + ([rec["default"]] if rec.get("default") else [])
    if not used:
        errors.append(f"{mesh}: recolor sin reglas ni default")
    errors += [f"{mesh}: muestra {s} no está en la paleta de {e.get('biome')}" for s in used if s not in swatches]
    for r in rec.get("rules", []):
        try:
            hex_to_srgb(r["from"])
        except ValueError as exc:
            errors.append(f"{mesh}: {exc}")
    return errors


def validate_catalog(catalog: dict[str, Any], palettes: dict[str, Any], packs: set[str]) -> list[str]:
    """Errores del catálogo: nombres, biomas, categorías, packs, muestras y mínimos por bioma."""
    errors: list[str] = []
    seen: set[str] = set()
    counts: dict[tuple[str, str], int] = {}
    for e in catalog["entries"]:
        mesh = e.get("mesh", "?")
        if mesh in seen:
            errors.append(f"{mesh}: repetida")
        seen.add(mesh)
        errors += _entry_errors(e, catalog, palettes, packs)
        key = (e.get("biome", ""), e.get("category", ""))
        counts[key] = counts.get(key, 0) + 1
    for biome, mins in catalog.get("requirements", {}).items():
        for cat, n in mins.items():
            if counts.get((biome, cat), 0) < n:
                errors.append(f"{biome}: {counts.get((biome, cat), 0)} mallas de {cat}, se piden {n}")
    return errors


def flatten_palettes(palettes: dict[str, Any]) -> dict[str, dict[str, str]]:
    """``{bioma: {muestra: hex}}`` validando cada color."""
    out: dict[str, dict[str, str]] = {}
    for biome, swatches in palettes["biomes"].items():
        for hx in swatches.values():
            hex_to_srgb(hx)
        out[biome] = dict(swatches)
    return out


def parse_args(argv: list[str]) -> dict[str, Any]:
    """Argumentos tras ``--`` en la línea de Blender: ``--biome X`` y ``--meshes a b``."""
    args: dict[str, Any] = {"biome": None, "meshes": []}
    collecting = False
    it = iter(argv)
    for a in it:
        if a == "--biome":
            value = next(it, None)
            if value is None or value.startswith("--"):
                raise SystemExit("--biome necesita un valor")
            if value not in BIOMES:
                raise SystemExit(f"bioma desconocido: {value} (hay {', '.join(BIOMES)})")
            args["biome"] = value
            collecting = False
        elif a == "--meshes":
            collecting = True
        elif collecting and not a.startswith("--"):
            args["meshes"].append(a)
        else:
            raise SystemExit(f"argumento desconocido: {a}")
    return args
