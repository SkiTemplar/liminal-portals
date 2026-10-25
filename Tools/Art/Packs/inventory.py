"""inventory.py — revisa candidatos de los packs antes de escribirlos en el catálogo.

    blender -b --factory-startup --python Tools/Art/Packs/inventory.py -- \
        <salida.png> <pack>/<fichero> [<pack>/<fichero> ...]

Por fichero imprime una línea ``INV`` con medidas tal como vienen, triángulos, si algún
material lleva textura y los colores del pack agrupados (ΔE < 0.05) con su parte del área y
la altura media (0 abajo, 1 arriba): es lo que hace falta para escribir las reglas de color.
Después renderiza todos los originales en fila (``bl_sheet.render_row``) en ``<salida.png>``.
"""

from __future__ import annotations

import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

from bl_mesh import (  # noqa: E402
    face_colors,
    has_texture,
    import_file,
    mesh_of,
    require_version,
    reset_scene,
    triangle_count,
    write_color_attr,
)
from bl_sheet import render_row  # noqa: E402
from fetch_packs import cache_dir  # noqa: E402
from packs_core import delta_e, oklab, srgb_to_linear, to_hex  # noqa: E402


def clusters(obj) -> list[tuple[str, float, float]]:
    me = mesh_of(obj)
    cols = face_colors(obj)
    zs = [v.co.z for v in me.vertices]
    zmin, span = min(zs), max(max(zs) - min(zs), 1e-6)
    groups: list[dict] = []
    total = 0.0
    for p, c in zip(me.polygons, cols, strict=True):
        lab, area, h = oklab(c), p.area, (p.center.z - zmin) / span
        total += area
        for g in groups:
            if delta_e(g["lab"], lab) < 0.05:
                g["area"] += area
                g["h"] += h * area
                break
        else:
            groups.append({"lab": lab, "srgb": c, "area": area, "h": h * area})
    groups.sort(key=lambda g: -g["area"])
    total = total or 1.0
    return [(to_hex(g["srgb"]), round(100 * g["area"] / total, 1), round(g["h"] / max(g["area"], 1e-9), 2))
            for g in groups[:6]]


def main() -> None:
    require_version()
    argv = sys.argv[sys.argv.index("--") + 1:]
    out, files = Path(argv[0]).resolve(), argv[1:]
    reset_scene()
    root = cache_dir()
    items = []
    for rel in files:
        src = root / rel
        obj = import_file(src, name=Path(rel).stem)
        me = mesh_of(obj)
        tex = any(has_texture(m) for m in me.materials)
        d = tuple(round(x, 2) for x in obj.dimensions)
        print(f"INV {rel} dims={d} tris={triangle_count(me)} mats={len(me.materials)} tex={tex} {clusters(obj)}")
        cols = face_colors(obj)
        loop = [None] * len(me.loops)
        for p in me.polygons:
            lin = [srgb_to_linear(c) for c in cols[p.index]]
            for li in p.loop_indices:
                loop[li] = lin
        write_color_attr(obj, "Col", loop)
        items.append((obj, f"{Path(rel).stem}\n{d[2]} m  {triangle_count(me)} t"))
    render_row(items, out, fit=True, px_per_item=260)
    print(f"INV sheet {out}")


if __name__ == "__main__":
    main()
