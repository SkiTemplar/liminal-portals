"""Geometría pura (sin bpy) de los kits low-poly de LiminalPortals.

Unidades en cm, Z arriba, +X delante. Una malla es inmutable: cada función devuelve una copia
nueva. El color va por cara en sRGB (0..1) con alfa; alfa 0 marca una cara emisiva (ver
`EMISSIVE_ALPHA` y docs/ARTE.md).
"""
from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Callable, Iterable, Sequence

Vec = tuple[float, float, float]
Color = tuple[float, float, float, float]
Matrix = tuple[Vec, Vec, Vec]

OPAQUE_ALPHA = 1.0
EMISSIVE_ALPHA = 0.0
EPS = 1e-9


# --- Color -------------------------------------------------------------------------------------

def hex_color(code: str, emissive: bool = False) -> Color:
    """'#RRGGBB' en sRGB a tupla (r, g, b, a); a = 0 si la cara es emisiva."""
    digits = code.lstrip("#")
    if len(digits) != 6:
        raise ValueError(f"color hexadecimal no válido: {code!r}")
    r, g, b = (int(digits[i:i + 2], 16) / 255.0 for i in (0, 2, 4))
    return (r, g, b, EMISSIVE_ALPHA if emissive else OPAQUE_ALPHA)


def shade(color: Color, factor: float) -> Color:
    """Multiplica el RGB (sRGB) por factor, recortado a [0, 1]; conserva el alfa."""
    r, g, b, a = color
    return (min(1.0, r * factor), min(1.0, g * factor), min(1.0, b * factor), a)


def mix(first: Color, second: Color, t: float) -> Color:
    """Interpolación lineal en sRGB; el alfa es el del primer color."""
    return tuple(first[i] + (second[i] - first[i]) * t for i in range(3)) + (first[3],)


def is_emissive(color: Color) -> bool:
    return color[3] <= EMISSIVE_ALPHA + 1e-6


# --- Vectores ------------------------------------------------------------------------------------

def add(a: Vec, b: Vec) -> Vec:
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def sub(a: Vec, b: Vec) -> Vec:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def scale(a: Vec, s: float) -> Vec:
    return (a[0] * s, a[1] * s, a[2] * s)


def dot(a: Vec, b: Vec) -> float:
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def cross(a: Vec, b: Vec) -> Vec:
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def length(a: Vec) -> float:
    return math.sqrt(dot(a, a))


def normalize(a: Vec) -> Vec:
    n = length(a)
    return (0.0, 0.0, 0.0) if n < EPS else scale(a, 1.0 / n)


def lerp(a: Vec, b: Vec, t: float) -> Vec:
    return add(a, scale(sub(b, a), t))


# --- Matrices de rotación ------------------------------------------------------------------------

def rot_x(deg: float) -> Matrix:
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    return ((1.0, 0.0, 0.0), (0.0, c, -s), (0.0, s, c))


def rot_y(deg: float) -> Matrix:
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    return ((c, 0.0, s), (0.0, 1.0, 0.0), (-s, 0.0, c))


def rot_z(deg: float) -> Matrix:
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    return ((c, -s, 0.0), (s, c, 0.0), (0.0, 0.0, 1.0))


def scale_matrix(sx: float, sy: float, sz: float) -> Matrix:
    return ((sx, 0.0, 0.0), (0.0, sy, 0.0), (0.0, 0.0, sz))


def mat_mul(a: Matrix, b: Matrix) -> Matrix:
    return tuple(tuple(sum(a[r][k] * b[k][c] for k in range(3)) for c in range(3)) for r in range(3))


def mat_vec(m: Matrix, v: Vec) -> Vec:
    return tuple(m[r][0] * v[0] + m[r][1] * v[1] + m[r][2] * v[2] for r in range(3))


def mat_det(m: Matrix) -> float:
    return dot(m[0], cross(m[1], m[2]))


# --- Malla ---------------------------------------------------------------------------------------

@dataclass(frozen=True)
class Mesh:
    verts: tuple[Vec, ...]
    faces: tuple[tuple[int, ...], ...]
    colors: tuple[Color, ...]

    def __post_init__(self) -> None:
        if len(self.faces) != len(self.colors):
            raise ValueError("cada cara necesita un color")
        count = len(self.verts)
        for face in self.faces:
            if len(face) < 3 or any(i < 0 or i >= count for i in face):
                raise ValueError(f"cara no válida: {face}")

    @property
    def tri_count(self) -> int:
        return sum(len(f) - 2 for f in self.faces)

    def bbox(self) -> tuple[Vec, Vec]:
        if not self.verts:
            raise ValueError("malla vacía")
        xs, ys, zs = zip(*self.verts)
        return (min(xs), min(ys), min(zs)), (max(xs), max(ys), max(zs))


EMPTY = Mesh((), (), ())


def make_mesh(verts: Iterable[Vec], faces: Iterable[Sequence[int]], colors: Iterable[Color] | Color) -> Mesh:
    face_list = tuple(tuple(int(i) for i in f) for f in faces)
    if isinstance(colors, tuple) and len(colors) == 4 and isinstance(colors[0], float):
        color_list = (colors,) * len(face_list)
    else:
        color_list = tuple(colors)
    return Mesh(tuple(tuple(float(c) for c in v) for v in verts), face_list, color_list)


def merge(*meshes: Mesh) -> Mesh:
    verts: list[Vec] = []
    faces: list[tuple[int, ...]] = []
    colors: list[Color] = []
    for mesh in meshes:
        base = len(verts)
        verts.extend(mesh.verts)
        faces.extend(tuple(i + base for i in f) for f in mesh.faces)
        colors.extend(mesh.colors)
    return Mesh(tuple(verts), tuple(faces), tuple(colors))


def transform(mesh: Mesh, matrix: Matrix | None = None, offset: Vec = (0.0, 0.0, 0.0)) -> Mesh:
    """v' = M·v + offset. Si M invierte la orientación, se invierte el orden de las caras."""
    m = matrix or ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0))
    verts = tuple(add(mat_vec(m, v), offset) for v in mesh.verts)
    faces = mesh.faces if mat_det(m) > 0 else tuple(tuple(reversed(f)) for f in mesh.faces)
    return Mesh(verts, faces, mesh.colors)


def translate(mesh: Mesh, offset: Vec) -> Mesh:
    return transform(mesh, None, offset)


def face_points(mesh: Mesh, index: int) -> list[Vec]:
    return [mesh.verts[i] for i in mesh.faces[index]]


def polygon_normal(points: Sequence[Vec]) -> Vec:
    """Normal de Newell (robusta para polígonos no del todo planos)."""
    nx = ny = nz = 0.0
    for i, p in enumerate(points):
        q = points[(i + 1) % len(points)]
        nx += (p[1] - q[1]) * (p[2] + q[2])
        ny += (p[2] - q[2]) * (p[0] + q[0])
        nz += (p[0] - q[0]) * (p[1] + q[1])
    return normalize((nx, ny, nz))


def centroid(points: Sequence[Vec]) -> Vec:
    n = float(len(points))
    return (sum(p[0] for p in points) / n, sum(p[1] for p in points) / n, sum(p[2] for p in points) / n)


def face_normal(mesh: Mesh, index: int) -> Vec:
    return polygon_normal(face_points(mesh, index))


def face_centroid(mesh: Mesh, index: int) -> Vec:
    return centroid(face_points(mesh, index))


def recolor(mesh: Mesh, fn: Callable[[Vec, Vec, Color], Color]) -> Mesh:
    """Nuevo color por cara a partir de (centroide, normal, color actual)."""
    colors = tuple(fn(face_centroid(mesh, i), face_normal(mesh, i), c) for i, c in enumerate(mesh.colors))
    return Mesh(mesh.verts, mesh.faces, colors)


def flip(mesh: Mesh) -> Mesh:
    return Mesh(mesh.verts, tuple(tuple(reversed(f)) for f in mesh.faces), mesh.colors)


def orient_from(mesh: Mesh, center: Vec) -> Mesh:
    """Orienta cada cara hacia fuera de `center` (válido para sólidos convexos o estrellados)."""
    faces = []
    for i, face in enumerate(mesh.faces):
        outward = sub(face_centroid(mesh, i), center)
        faces.append(face if dot(face_normal(mesh, i), outward) >= 0 else tuple(reversed(face)))
    return Mesh(mesh.verts, tuple(faces), mesh.colors)


def orient_convex(mesh: Mesh) -> Mesh:
    return orient_from(mesh, centroid(mesh.verts))


def orient_towards(face: Sequence[int], verts: Sequence[Vec], direction: Vec) -> tuple[int, ...]:
    pts = [verts[i] for i in face]
    return tuple(face) if dot(polygon_normal(pts), direction) >= 0 else tuple(reversed(face))


def pivot_base_center(mesh: Mesh) -> Mesh:
    """Centro del bounding box en XY a 0 y z mínima a 0."""
    lo, hi = mesh.bbox()
    return translate(mesh, (-(lo[0] + hi[0]) * 0.5, -(lo[1] + hi[1]) * 0.5, -lo[2]))


def drop_to_ground(mesh: Mesh) -> Mesh:
    lo, _ = mesh.bbox()
    return translate(mesh, (0.0, 0.0, -lo[2]))


def triangles(mesh: Mesh) -> list[tuple[Vec, Vec, Vec]]:
    """Triangulación en abanico (las caras de los kits son convexas)."""
    tris = []
    for face in mesh.faces:
        a = mesh.verts[face[0]]
        for k in range(1, len(face) - 1):
            tris.append((a, mesh.verts[face[k]], mesh.verts[face[k + 1]]))
    return tris


def corner_colors(mesh: Mesh, gradient: float) -> list[Color]:
    """Color por esquina: color de la cara oscurecido hacia abajo (degradado vertical suave).

    gradient = 0.15 oscurece un 15 % en la base de la malla. Las caras emisivas no se tocan.
    """
    if not mesh.verts:
        return []
    lo, hi = mesh.bbox()
    height = max(hi[2] - lo[2], EPS)
    out: list[Color] = []
    for face, color in zip(mesh.faces, mesh.colors):
        for index in face:
            if is_emissive(color) or gradient <= 0:
                out.append(color)
                continue
            t = (mesh.verts[index][2] - lo[2]) / height
            out.append(shade(color, 1.0 - gradient * (1.0 - t)))
    return out


def compact(mesh: Mesh) -> Mesh:
    """Quita los vértices que no usa ninguna cara (sin cambiar el orden del resto)."""
    used = sorted({i for f in mesh.faces for i in f})
    remap = {old: new for new, old in enumerate(used)}
    return Mesh(tuple(mesh.verts[i] for i in used), tuple(tuple(remap[i] for i in f) for f in mesh.faces), mesh.colors)
