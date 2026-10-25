"""Primitivas low-poly puras (sin bpy). Unidades en cm; todas devuelven mallas nuevas y orientadas."""
from __future__ import annotations

import math
from typing import Callable, Sequence

from lp_geom import (
    EPS, Color, Mesh, Vec, add, centroid, compact, cross, dot, make_mesh, merge, normalize, orient_convex,
    orient_towards, polygon_normal, scale, sub,
)

BOX_SIDES = ("-x", "+x", "-y", "+y", "-z", "+z")


def box(lo: Vec, hi: Vec, color: Color, skip: Sequence[str] = ()) -> Mesh:
    """Caja alineada con los ejes; `skip` omite caras ('-x', '+z', …) que quedan ocultas."""
    if any(hi[i] - lo[i] <= EPS for i in range(3)):
        raise ValueError(f"caja degenerada: {lo} {hi}")
    x0, y0, z0 = lo
    x1, y1, z1 = hi
    verts = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
             (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]
    quads = {
        "-x": (0, 4, 7, 3), "+x": (1, 2, 6, 5), "-y": (0, 1, 5, 4),
        "+y": (3, 7, 6, 2), "-z": (0, 3, 2, 1), "+z": (4, 5, 6, 7),
    }
    faces = [quads[side] for side in BOX_SIDES if side not in skip]
    return make_mesh(verts, faces, color)


def hull_box(lo: Vec, hi: Vec, color: Color) -> Mesh:
    """Caja cerrada como sólido convexo (útil para transformarla después)."""
    return box(lo, hi, color)


def frustum(center: Vec, r0: float, r1: float, height: float, sides: int, color: Color,
            phase: float = 0.0, cap_bottom: bool = True, cap_top: bool = True,
            stretch: tuple[float, float] = (1.0, 1.0)) -> Mesh:
    """Tronco de prisma de `sides` lados sobre el eje Z; r1 = 0 lo cierra en punta."""
    if sides < 3 or height <= EPS:
        raise ValueError("frustum necesita ≥ 3 lados y altura positiva")
    cx, cy, cz = center
    ring = []
    for level, radius in ((0.0, r0), (height, r1)):
        for k in range(sides):
            a = phase + 2.0 * math.pi * k / sides
            ring.append((cx + radius * math.cos(a) * stretch[0], cy + radius * math.sin(a) * stretch[1], cz + level))
    faces: list[tuple[int, ...]] = []
    pointed = r1 <= EPS
    verts = ring[:sides] + ([(cx, cy, cz + height)] if pointed else ring[sides:])
    for k in range(sides):
        n = (k + 1) % sides
        faces.append((k, n, sides) if pointed else (k, n, sides + n, sides + k))
    if cap_bottom:
        faces.append(tuple(range(sides)))
    if cap_top and not pointed:
        faces.append(tuple(range(sides, 2 * sides)))
    return orient_convex(make_mesh(verts, faces, color))


def _basis(axis: Vec) -> tuple[Vec, Vec]:
    helper = (0.0, 0.0, 1.0) if abs(axis[2]) < 0.9 else (1.0, 0.0, 0.0)
    u = normalize(cross(axis, helper))
    return u, cross(axis, u)


def cylinder_between(p0: Vec, p1: Vec, r0: float, r1: float, sides: int, color: Color,
                     caps: bool = True, phase: float = 0.0) -> Mesh:
    """Tronco de prisma entre dos puntos cualesquiera (patas, tallos, postes inclinados)."""
    axis = normalize(sub(p1, p0))
    if sides < 3 or axis == (0.0, 0.0, 0.0):
        raise ValueError("cylinder_between necesita ≥ 3 lados y dos puntos distintos")
    u, v = _basis(axis)
    verts: list[Vec] = []
    for base, radius in ((p0, r0), (p1, r1)):
        for k in range(sides):
            a = phase + 2.0 * math.pi * k / sides
            verts.append(add(base, add(scale(u, radius * math.cos(a)), scale(v, radius * math.sin(a)))))
    pointed = r1 <= EPS
    if pointed:
        verts = verts[:sides] + [p1]
    faces: list[tuple[int, ...]] = []
    for k in range(sides):
        n = (k + 1) % sides
        faces.append((k, n, sides) if pointed else (k, n, sides + n, sides + k))
    if caps:
        faces.append(tuple(range(sides)))
        if not pointed:
            faces.append(tuple(range(sides, 2 * sides)))
    return orient_convex(make_mesh(verts, faces, color))


def ellipsoid(center: Vec, radii: Vec, segments: int, rings: int, color: Color,
              jitter: Callable[[Vec], Vec] | None = None) -> Mesh:
    """Elipsoide UV facetado (polos únicos); `jitter` desplaza cada vértice de forma determinista."""
    if segments < 3 or rings < 2:
        raise ValueError("ellipsoid necesita ≥ 3 segmentos y ≥ 2 anillos")
    verts: list[Vec] = [(0.0, 0.0, -1.0)]
    for r in range(1, rings):
        phi = math.pi * r / rings - math.pi / 2.0
        for s in range(segments):
            theta = 2.0 * math.pi * s / segments
            verts.append((math.cos(phi) * math.cos(theta), math.cos(phi) * math.sin(theta), math.sin(phi)))
    verts.append((0.0, 0.0, 1.0))
    top = len(verts) - 1
    faces: list[tuple[int, ...]] = []
    for s in range(segments):
        n = (s + 1) % segments
        faces.append((0, 1 + n, 1 + s))
        last = 1 + (rings - 2) * segments
        faces.append((top, last + s, last + n))
    for r in range(rings - 2):
        a = 1 + r * segments
        b = a + segments
        for s in range(segments):
            n = (s + 1) % segments
            faces.append((a + s, a + n, b + n, b + s))
    placed = []
    for v in verts:
        p = (center[0] + v[0] * radii[0], center[1] + v[1] * radii[1], center[2] + v[2] * radii[2])
        placed.append(jitter(p) if jitter else p)
    mesh = make_mesh(placed, faces, color)
    return orient_convex(mesh)


def offset_polyline(points: Sequence[tuple[float, float]], distance: float) -> list[tuple[float, float]]:
    """Desplaza una polilínea abierta a su izquierda (normal (−dv, du)) con ingletes en los vértices."""
    if len(points) < 2:
        raise ValueError("la polilínea necesita al menos dos puntos")

    def seg_normal(a, b):
        du, dv = b[0] - a[0], b[1] - a[1]
        n = math.hypot(du, dv)
        if n < EPS:
            raise ValueError("segmento degenerado en la polilínea")
        return (-dv / n, du / n)

    normals = [seg_normal(points[i], points[i + 1]) for i in range(len(points) - 1)]
    out = []
    for i, p in enumerate(points):
        if i == 0:
            nu, nv = normals[0]
        elif i == len(points) - 1:
            nu, nv = normals[-1]
        else:
            a, b = normals[i - 1], normals[i]
            denom = 1.0 + a[0] * b[0] + a[1] * b[1]
            if denom < 1e-3:
                raise ValueError("inglete demasiado cerrado")
            nu, nv = (a[0] + b[0]) / denom, (a[1] + b[1]) / denom
        out.append((p[0] + nu * distance, p[1] + nv * distance))
    return out


def sweep_planar(path: Sequence[tuple[float, float]], profile: Sequence[tuple[float, float]],
                 mapper: Callable[[float, float, float], Vec], color: Color) -> Mesh:
    """Barre un perfil cerrado (n, d) por una polilínea plana abierta (u, v) con ingletes.

    n desplaza a la izquierda de la polilínea, d sale del plano; `mapper(u, v, d)` lleva a 3D.
    Sin tapas en los extremos (los kits los apoyan en el suelo o contra otra pieza).
    """
    rings = [[mapper(u, v, d) for (u, v) in offset_polyline(path, n)] for (n, d) in profile]
    verts: list[Vec] = [p for ring in rings for p in ring]
    count = len(path)
    area = sum(profile[i][0] * profile[(i + 1) % len(profile)][1] - profile[(i + 1) % len(profile)][0] * profile[i][1]
               for i in range(len(profile)))
    ccw = area > 0
    faces: list[tuple[int, ...]] = []
    for k in range(len(profile)):
        k2 = (k + 1) % len(profile)
        (n0, d0), (n1, d1) = profile[k], profile[k2]
        # Normal exterior del lado del perfil en (n, d): a la derecha si el perfil es antihorario.
        en, ed = (d1 - d0, -(n1 - n0)) if ccw else (-(d1 - d0), n1 - n0)
        for j in range(count - 1):
            quad = (k * count + j, k * count + j + 1, k2 * count + j + 1, k2 * count + j)
            mid_uv = ((path[j][0] + path[j + 1][0]) * 0.5, (path[j][1] + path[j + 1][1]) * 0.5)
            off = offset_polyline([path[j], path[j + 1]], en * 1.0)
            base3 = mapper(mid_uv[0], mid_uv[1], 0.0)
            n_dir = sub(mapper((off[0][0] + off[1][0]) * 0.5, (off[0][1] + off[1][1]) * 0.5, 0.0), base3)
            d_dir = sub(mapper(mid_uv[0], mid_uv[1], ed), base3)
            faces.append(orient_towards(quad, verts, add(n_dir, d_dir)))
    return make_mesh(verts, faces, color)


def extrude_planar(points2d: Sequence[tuple[float, float]], faces2d: Sequence[Sequence[int]],
                   mapper: Callable[[float, float, float], Vec], depth: float,
                   front: Callable[[int], Color], back: Callable[[int], Color],
                   side: Callable[[tuple[float, float], tuple[float, float]], Color]) -> Mesh:
    """Extruye una malla plana (u, v) de polígonos antihorarios entre d = 0 y d = −depth.

    Cara frontal en d = 0 (normal +d), trasera en d = −depth y laterales en las aristas de borde
    (las que usa una sola cara). `side(a, b)` da el color de cada lateral.
    """
    n = len(points2d)
    verts = [mapper(u, v, 0.0) for (u, v) in points2d] + [mapper(u, v, -depth) for (u, v) in points2d]
    d_dir = sub(mapper(0.0, 0.0, 1.0), mapper(0.0, 0.0, 0.0))
    faces: list[tuple[int, ...]] = []
    colors: list[Color] = []
    edge_use: dict[tuple[int, int], int] = {}
    for i, f in enumerate(faces2d):
        faces.append(orient_towards(tuple(f), verts, d_dir))
        colors.append(front(i))
        faces.append(orient_towards(tuple(n + k for k in f), verts, scale(d_dir, -1.0)))
        colors.append(back(i))
        for k in range(len(f)):
            a, b = f[k], f[(k + 1) % len(f)]
            key = (min(a, b), max(a, b))
            edge_use[key] = edge_use.get(key, 0) + 1
    for i, f in enumerate(faces2d):
        for k in range(len(f)):
            a, b = f[k], f[(k + 1) % len(f)]
            if edge_use[(min(a, b), max(a, b))] != 1:
                continue
            pa, pb = points2d[a], points2d[b]
            # Polígono antihorario: el exterior queda a la derecha de a→b.
            out2 = (pb[1] - pa[1], -(pb[0] - pa[0]))
            mid = ((pa[0] + pb[0]) * 0.5, (pa[1] + pb[1]) * 0.5)
            out3 = sub(mapper(mid[0] + out2[0], mid[1] + out2[1], 0.0), mapper(mid[0], mid[1], 0.0))
            faces.append(orient_towards((a, b, n + b, n + a), verts, out3))
            colors.append(side(pa, pb))
    return compact(make_mesh(verts, faces, colors))


def convex_hull(points: Sequence[Vec], color: Color, eps: float = 1e-6) -> Mesh:
    """Envolvente convexa 3D incremental: triángulos orientados hacia fuera, sin vértices sueltos."""
    pts = [tuple(float(c) for c in p) for p in points]
    if len(pts) < 4:
        raise ValueError("la envolvente necesita al menos 4 puntos")
    a = min(range(len(pts)), key=lambda i: pts[i])
    b = max(range(len(pts)), key=lambda i: sum((pts[i][k] - pts[a][k]) ** 2 for k in range(3)))
    ab = sub(pts[b], pts[a])
    c = max(range(len(pts)), key=lambda i: length_sq(cross(ab, sub(pts[i], pts[a]))))
    normal = cross(ab, sub(pts[c], pts[a]))
    d = max(range(len(pts)), key=lambda i: abs(dot(normal, sub(pts[i], pts[a]))))
    if length_sq(normal) < eps or abs(dot(normal, sub(pts[d], pts[a]))) < eps:
        raise ValueError("puntos degenerados (coplanares o colineales)")
    inside = centroid([pts[a], pts[b], pts[c], pts[d]])
    faces: list[tuple[int, int, int]] = []
    for tri in ((a, b, c), (a, b, d), (a, c, d), (b, c, d)):
        n = cross(sub(pts[tri[1]], pts[tri[0]]), sub(pts[tri[2]], pts[tri[0]]))
        faces.append(tri if dot(n, sub(pts[tri[0]], inside)) > 0 else (tri[0], tri[2], tri[1]))

    def plane_dist(face, p):
        n = normalize(cross(sub(pts[face[1]], pts[face[0]]), sub(pts[face[2]], pts[face[0]])))
        return dot(n, sub(p, pts[face[0]]))

    for i, p in enumerate(pts):
        if i in (a, b, c, d):
            continue
        visible = [f for f in faces if plane_dist(f, p) > eps]
        if not visible:
            continue
        edges = {(f[k], f[(k + 1) % 3]) for f in visible for k in range(3)}
        horizon = [e for e in edges if (e[1], e[0]) not in edges]
        faces = [f for f in faces if f not in visible] + [(e[0], e[1], i) for e in horizon]
    used = sorted({i for f in faces for i in f})
    remap = {old: new for new, old in enumerate(used)}
    return make_mesh([pts[i] for i in used], [tuple(remap[i] for i in f) for f in faces], color)


def length_sq(v: Vec) -> float:
    return dot(v, v)


def strip(left: Sequence[Vec], right: Sequence[Vec], color: Color, double_sided: bool = True) -> Mesh:
    """Tira de quads entre dos polilíneas (hojas, tallos planos); doble cara con vértices propios."""
    if len(left) != len(right) or len(left) < 2:
        raise ValueError("strip necesita dos polilíneas iguales de ≥ 2 puntos")
    n = len(left)
    verts = list(left) + list(right)
    faces = []
    for j in range(n - 1):
        quad = (j, j + 1, n + j + 1, n + j)
        kept = [i for k, i in enumerate(quad) if verts[i] != verts[quad[k - 1]]]
        if len(kept) >= 3:
            faces.append(tuple(kept))
    front = make_mesh(verts, faces, color)
    if not double_sided:
        return front
    back = make_mesh(front.verts, [tuple(reversed(f)) for f in front.faces], color)
    return merge(front, back)


def heightfield_disk(radii: Sequence[float], heights: Sequence[float], segments: int, color: Color,
                     radial_jitter: Callable[[int, int], float] | None = None,
                     stretch: tuple[float, float] = (1.0, 1.0)) -> Mesh:
    """Montículo abierto por debajo: anillos concéntricos (radio, altura) alrededor de un vértice central."""
    if len(radii) != len(heights) or len(radii) < 2 or segments < 3 or radii[0] > EPS:
        raise ValueError("heightfield_disk: anillos y alturas incoherentes")
    verts: list[Vec] = []
    for r_index, (radius, z) in enumerate(zip(radii, heights)):
        if radius <= EPS:
            verts.append((0.0, 0.0, z))
            continue
        for s in range(segments):
            a = 2.0 * math.pi * s / segments
            rr = radius * (1.0 + (radial_jitter(r_index, s) if radial_jitter else 0.0))
            verts.append((rr * math.cos(a) * stretch[0], rr * math.sin(a) * stretch[1], z))
    faces: list[tuple[int, ...]] = []
    start = 1
    for s in range(segments):  # abanico central
        faces.append((0, 1 + s, 1 + (s + 1) % segments))
    for r in range(len(radii) - 2):
        a = start + r * segments
        b = a + segments
        for s in range(segments):
            n = (s + 1) % segments
            faces.append((a + s, b + s, b + n, a + n))
    mesh = make_mesh(verts, faces, color)
    oriented = [orient_towards(f, mesh.verts, (0.0, 0.0, 1.0)) for f in mesh.faces]
    return make_mesh(mesh.verts, oriented, mesh.colors)


def face_up_ratio(points: Sequence[Vec]) -> float:
    """Componente Z de la normal de un polígono (1 = mira arriba)."""
    return polygon_normal(points)[2]
