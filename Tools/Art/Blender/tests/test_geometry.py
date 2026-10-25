"""Tests de las funciones puras de geometría (lp_geom, lp_shapes)."""
from collections import Counter

import pytest

from lp_geom import (
    EMISSIVE_ALPHA, Mesh, compact, corner_colors, face_centroid, face_normal, hex_color, make_mesh, merge,
    pivot_base_center, scale_matrix, transform, triangles,
)
from lp_shapes import box, convex_hull, extrude_planar, frustum, heightfield_disk, offset_polyline, sweep_planar

GREY = hex_color("#808080")


def signed_volume(mesh: Mesh) -> float:
    total = 0.0
    for a, b, c in triangles(mesh):
        total += (a[0] * (b[1] * c[2] - b[2] * c[1]) - a[1] * (b[0] * c[2] - b[2] * c[0])
                  + a[2] * (b[0] * c[1] - b[1] * c[0]))
    return total / 6.0


def edge_counts(mesh: Mesh) -> Counter:
    counts = Counter()
    for f in mesh.faces:
        for k in range(len(f)):
            a, b = f[k], f[(k + 1) % len(f)]
            counts[(min(a, b), max(a, b))] += 1
    return counts


def corners(mesh):
    return [(fi, i) for fi, f in enumerate(mesh.faces) for i in f]


def test_hex_color_and_emissive_alpha():
    assert hex_color("#FF0000") == (1.0, 0.0, 0.0, 1.0)
    assert hex_color("FFF8E0", emissive=True)[3] == EMISSIVE_ALPHA
    with pytest.raises(ValueError):
        hex_color("#12345")


def test_mesh_rejects_bad_indices():
    with pytest.raises(ValueError):
        make_mesh([(0, 0, 0), (1, 0, 0), (0, 1, 0)], [(0, 1, 3)], GREY)


def test_box_is_closed_and_outward():
    b = box((0, 0, 0), (2, 3, 4), GREY)
    assert b.tri_count == 12
    assert signed_volume(b) == pytest.approx(24.0)
    assert all(c == 2 for c in edge_counts(b).values())


def test_mirror_transform_keeps_outward_orientation():
    mirrored = transform(box((0, 0, 0), (1, 1, 1), GREY), scale_matrix(-1, 1, 1))
    assert signed_volume(mirrored) == pytest.approx(1.0)


def test_pivot_base_center():
    lo, hi = pivot_base_center(box((10, 20, 5), (14, 30, 9), GREY)).bbox()
    assert lo == (-2.0, -5.0, 0.0) and hi == (2.0, 5.0, 4.0)


def test_corner_colors_gradient_spares_emissive_faces():
    glow = hex_color("#FFF8E0", emissive=True)
    mesh = merge(box((0, 0, 0), (1, 1, 1), GREY), box((0, 0, 2), (1, 1, 3), glow))
    colors = corner_colors(mesh, 0.5)
    pairs = list(zip(colors, corners(mesh)))
    bottom = [c for c, (f, i) in pairs if mesh.verts[i][2] == 0.0 and mesh.colors[f][3] == 1.0]
    assert bottom and all(c[0] == pytest.approx(GREY[0] * 0.5) for c in bottom)
    glowing = [c for c, (f, _) in pairs if mesh.colors[f][3] == EMISSIVE_ALPHA]
    assert glowing and all(c == glow for c in glowing)


def test_compact_drops_unused_vertices():
    mesh = make_mesh([(9, 9, 9), (0, 0, 0), (1, 0, 0), (0, 1, 0)], [(1, 2, 3)], GREY)
    assert len(compact(mesh).verts) == 3


def test_convex_hull_of_cube_with_interior_points():
    pts = [(x, y, z) for x in (0, 1) for y in (0, 1) for z in (0, 1)] + [(0.5, 0.5, 0.5), (0.2, 0.7, 0.4)]
    hull = convex_hull(pts, GREY)
    assert len(hull.verts) == 8
    assert signed_volume(hull) == pytest.approx(1.0)
    assert all(c == 2 for c in edge_counts(hull).values())


def test_convex_hull_rejects_coplanar_points():
    with pytest.raises(ValueError):
        convex_hull([(0, 0, 0), (1, 0, 0), (0, 1, 0), (1, 1, 0)], GREY)


def test_frustum_closed_outward():
    f = frustum((0, 0, 0), 1.0, 0.5, 2.0, 6, GREY)
    assert signed_volume(f) > 0
    assert all(c == 2 for c in edge_counts(f).values())


def test_offset_polyline_miters_right_angle():
    out = offset_polyline([(0, 0), (0, 10), (10, 10)], 1.0)
    assert out[0] == pytest.approx((-1.0, 0.0))
    assert out[1] == pytest.approx((-1.0, 11.0))
    assert out[2] == pytest.approx((10.0, 11.0))


def test_sweep_faces_point_away_from_profile():
    profile = ((0.0, 0.0), (2.0, 0.0), (2.0, 5.0), (0.0, 5.0))
    swept = sweep_planar(((0.0, 10.0), (0.0, -10.0)), profile, lambda u, v, d: (u, v, d), GREY)
    checked = 0
    for i in range(len(swept.faces)):
        n, c = face_normal(swept, i), face_centroid(swept, i)
        if c[2] == pytest.approx(5.0):
            assert n[2] > 0.99
            checked += 1
        if c[0] == pytest.approx(2.0):
            assert n[0] > 0.99
            checked += 1
    assert checked == 2


def test_extrude_planar_square_with_hole_is_closed():
    pts = [(c, r) for r in range(4) for c in range(4)]
    quads = [(r * 4 + c, r * 4 + c + 1, r * 4 + c + 5, r * 4 + c + 4)
             for r in range(3) for c in range(3) if (r, c) != (1, 1)]
    solid = extrude_planar(pts, quads, lambda u, v, d: (u, v, d), 2.0,
                           lambda i: GREY, lambda i: GREY, lambda a, b: GREY)
    assert all(c == 2 for c in edge_counts(solid).values())
    assert signed_volume(solid) == pytest.approx(8 * 2.0)


def test_heightfield_disk_faces_up_and_rests_on_ground():
    mound = heightfield_disk([0.0, 1.0, 2.0], [1.0, 0.5, 0.0], 8, GREY)
    assert all(face_normal(mound, i)[2] > 0 for i in range(len(mound.faces)))
    assert mound.bbox()[0][2] == 0.0
    with pytest.raises(ValueError):
        heightfield_disk([1.0, 2.0], [1.0, 0.0], 8, GREY)
