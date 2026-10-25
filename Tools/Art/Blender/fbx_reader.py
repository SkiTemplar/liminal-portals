"""Lector mínimo de FBX binario (7.x) en Python puro, para validar lo que recibe Unreal.

Lee los valores crudos del fichero (no los reinterpreta un importador): unidad de la escena, ejes,
transform del modelo, vértices, polígonos, capas de color y materiales.
"""
from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass, field

MAGIC = b"Kaydara FBX Binary  \x00"
_ARRAY_CODES = {"f": ("f", 4), "d": ("d", 8), "l": ("q", 8), "i": ("i", 4), "b": ("?", 1)}
_SCALAR_CODES = {"Y": ("<h", 2), "C": ("<?", 1), "I": ("<i", 4), "F": ("<f", 4), "D": ("<d", 8), "L": ("<q", 8)}


@dataclass
class Node:
    name: str
    props: list
    children: list["Node"] = field(default_factory=list)

    def find(self, name: str) -> "Node | None":
        return next((c for c in self.children if c.name == name), None)

    def find_all(self, name: str) -> list["Node"]:
        return [c for c in self.children if c.name == name]


def _read_prop(data: bytes, pos: int) -> tuple[object, int]:
    code = chr(data[pos])
    pos += 1
    if code in _SCALAR_CODES:
        fmt, size = _SCALAR_CODES[code]
        return struct.unpack_from(fmt, data, pos)[0], pos + size
    if code in _ARRAY_CODES:
        count, encoding, comp_len = struct.unpack_from("<III", data, pos)
        pos += 12
        raw = data[pos:pos + comp_len]
        pos += comp_len
        if encoding == 1:
            raw = zlib.decompress(raw)
        elif encoding != 0:
            raise ValueError(f"codificación de array desconocida: {encoding}")
        fmt, size = _ARRAY_CODES[code]
        if len(raw) != count * size:
            raise ValueError("array FBX truncado")
        return list(struct.unpack(f"<{count}{fmt}", raw)), pos
    if code in ("S", "R"):
        (size,) = struct.unpack_from("<I", data, pos)
        pos += 4
        raw = data[pos:pos + size]
        return (raw.decode("utf-8", "replace") if code == "S" else raw), pos + size
    raise ValueError(f"tipo de propiedad FBX desconocido: {code!r} en {pos - 1}")


def _read_node(data: bytes, pos: int, wide: bool) -> tuple[Node | None, int]:
    head = "<QQQB" if wide else "<IIIB"
    end, num_props, _plen, name_len = struct.unpack_from(head, data, pos)
    pos += struct.calcsize(head)
    if end == 0:
        return None, pos
    name = data[pos:pos + name_len].decode("ascii", "replace")
    pos += name_len
    props = []
    for _ in range(num_props):
        value, pos = _read_prop(data, pos)
        props.append(value)
    node = Node(name, props)
    while pos < end:
        child, pos = _read_node(data, pos, wide)
        if child is None:
            break
        node.children.append(child)
    return node, end


def parse(data: bytes) -> tuple[int, list[Node]]:
    """Devuelve (versión, nodos raíz) de un FBX binario."""
    if not data.startswith(MAGIC):
        raise ValueError("no es un FBX binario")
    (version,) = struct.unpack_from("<I", data, 23)
    wide = version >= 7500
    pos = 27
    nodes = []
    while pos < len(data):
        node, pos = _read_node(data, pos, wide)
        if node is None:
            break
        nodes.append(node)
    return version, nodes


def properties70(node: Node | None) -> dict[str, list]:
    """Diccionario nombre → valores de un bloque Properties70."""
    block = node.find("Properties70") if node else None
    return {p.props[0]: p.props[4:] for p in block.find_all("P")} if block else {}


@dataclass
class FbxSummary:
    version: int
    unit_scale: float
    up_axis: int
    front_axis: int
    models: list[dict]
    materials: list[str]
    verts: list[tuple[float, float, float]]
    polygons: list[list[int]]
    color_layers: list[str]
    corner_alphas: list[float]

    @property
    def tris(self) -> int:
        return sum(len(p) - 2 for p in self.polygons)


def _split_polygons(indices: list[int]) -> list[list[int]]:
    polys, current = [], []
    for i in indices:
        if i < 0:
            current.append(~i)
            polys.append(current)
            current = []
        else:
            current.append(i)
    if current:
        raise ValueError("PolygonVertexIndex sin cerrar")
    return polys


def _corner_alphas(layer: Node, corner_count: int) -> list[float]:
    colors_node = layer.find("Colors")
    if colors_node is None:
        return []
    rgba = colors_node.props[0]
    mapping = (layer.find("MappingInformationType").props[0]
               if layer.find("MappingInformationType") else "ByPolygonVertex")
    reference = (layer.find("ReferenceInformationType").props[0]
                 if layer.find("ReferenceInformationType") else "Direct")
    if mapping != "ByPolygonVertex":
        raise ValueError(f"mapeo de color no soportado: {mapping}")
    if reference == "IndexToDirect":
        index = layer.find("ColorIndex").props[0]
    else:
        index = list(range(corner_count))
    return [rgba[i * 4 + 3] for i in index]


def summarize(data: bytes) -> FbxSummary:
    version, roots = parse(data)
    by_name = {n.name: n for n in roots}
    settings = properties70(by_name.get("GlobalSettings"))
    objects = by_name.get("Objects")
    if objects is None:
        raise ValueError("FBX sin bloque Objects")
    geometries = [n for n in objects.find_all("Geometry") if n.props[2] == "Mesh"]
    if len(geometries) != 1:
        raise ValueError(f"se esperaba una geometría y hay {len(geometries)}")
    geo = geometries[0]
    raw = geo.find("Vertices").props[0]
    verts = [(raw[i], raw[i + 1], raw[i + 2]) for i in range(0, len(raw), 3)]
    polygons = _split_polygons(geo.find("PolygonVertexIndex").props[0])
    layers = geo.find_all("LayerElementColor")
    names = [(l.find("Name").props[0] if l.find("Name") else "") for l in layers]
    corners = sum(len(p) for p in polygons)
    alphas = _corner_alphas(layers[0], corners) if layers else []
    models = []
    for model in objects.find_all("Model"):
        p = properties70(model)
        models.append({
            "name": model.props[1].split("\x00")[0],
            "translation": p.get("Lcl Translation", [0.0, 0.0, 0.0])[:3],
            "rotation": p.get("Lcl Rotation", [0.0, 0.0, 0.0])[:3],
            "scaling": p.get("Lcl Scaling", [1.0, 1.0, 1.0])[:3],
        })
    materials = [m.props[1].split("\x00")[0] for m in objects.find_all("Material")]
    return FbxSummary(
        version=version,
        unit_scale=float(settings.get("UnitScaleFactor", [1.0])[0]),
        up_axis=int(settings.get("UpAxis", [1])[0]),
        front_axis=int(settings.get("FrontAxis", [2])[0]),
        models=models, materials=materials, verts=verts, polygons=polygons,
        color_layers=names, corner_alphas=alphas,
    )
