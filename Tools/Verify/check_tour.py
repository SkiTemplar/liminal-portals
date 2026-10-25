"""Comprueba las capturas de Liminal.Tour (etapas E3 y E4).

Para cada captura del índice tour.json:
- existe, no es negra ni está quemada: luminancia media (Rec. 709 sobre los valores sRGB 0-255) de la
  imagen completa y del recuadro central entre MIN_MEAN_LUMA y MAX_MEAN_LUMA;
- no es uniforme: la desviación típica de la luminancia supera MIN_LUMA_STD;
- en las vistas desde dentro del bioma (kind = inside), el primer plano no es negro: en la franja
  inferior (FOREGROUND_FRACTION del alto) como mucho MAX_DARK_FOREGROUND de los píxeles tiene
  luminancia < DARK_LUMA. Una sombra sin luz de relleno (selva con la luz de cielo de E4: 97 % de
  píxeles < 20) pasaba la media de la imagen entera.

En las vistas de puerta (kind = from_hub o from_biome; sin portalCorners, frameCorners o wallCorners
en píxeles del viewport, la captura falla):
- el hueco del portal no es un color plano (desviación > MIN_PORTAL_STD) y sus píxeles se alejan del
  color medio del marco más de MIN_PORTAL_FRAME_DIFF de media (distancia RGB): el portal muestra otra
  cosa que su propio marco (comparar solo las medias falla con un paisaje oscuro y variado);
- en las vistas desde el bioma (kind = from_biome), la franja de pared que rodea el marco (cara frontal
  de la pared exenta) tiene luminancia media >= MIN_LIT_WALL_LUMA: el sol le da de cara (D-24).

Uso: uv run --no-project --with numpy --with pillow python check_tour.py --tour Saved/Tour [--out result.json]
Sale con código 1 si alguna captura no cumple; escribe el resultado en JSON.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

LUMA = np.array([0.2126, 0.7152, 0.0722])
# Umbrales en niveles sRGB (0-255): por debajo, negra; por encima, quemada.
MIN_MEAN_LUMA = 15.0
MAX_MEAN_LUMA = 245.0
# Recuadro central: un 20 % del ancho y del alto.
CENTER_FRACTION = 0.2
# Imagen uniforme (gris plano, captura vacía): desviación típica de la luminancia por debajo de esto.
MIN_LUMA_STD = 6.0
# Hueco del portal: color plano (reserva, textura negra) y parecido con el marco.
MIN_PORTAL_STD = 4.0
MIN_PORTAL_FRAME_DIFF = 20.0
# Primer plano de las vistas desde dentro: franja inferior (0,3 del alto = y >= 504 a 720 px) y
# fracción máxima de píxeles casi negros (medido tras D-28: 0 % en los 4 biomas; antes, 97 % en la selva).
FOREGROUND_FRACTION = 0.3
DARK_LUMA = 20.0
MAX_DARK_FOREGROUND = 0.25
# Cara frontal de la pared de vuelta iluminada (con el sol de E3, en su propia sombra, rondaba 115).
MIN_LIT_WALL_LUMA = 150.0
# Vistas de puerta: tienen que traer los tres polígonos (sin ellos no se puede comprobar el hueco).
DOOR_KINDS = frozenset({"from_hub", "from_biome"})
# Píxeles que se recortan en cada borde de los polígonos (antialiasing y error de proyección).
EDGE_PX = 3


def luma(pixels: np.ndarray) -> np.ndarray:
    return pixels[..., :3].astype(np.float64) @ LUMA


def mean_luma(pixels: np.ndarray) -> float:
    return float(luma(pixels).mean())


def center_crop(pixels: np.ndarray, fraction: float = CENTER_FRACTION) -> np.ndarray:
    height, width = pixels.shape[:2]
    half_w = max(1, int(width * fraction / 2))
    half_h = max(1, int(height * fraction / 2))
    cx, cy = width // 2, height // 2
    return pixels[cy - half_h:cy + half_h, cx - half_w:cx + half_w]


def polygon_mask(size: tuple[int, int], corners, grow_px: int = 0) -> np.ndarray:
    """Máscara (alto × ancho) del polígono; grow_px > 0 la dilata y < 0 la erosiona."""
    width, height = size
    image = Image.new("L", (width, height), 0)
    ImageDraw.Draw(image).polygon([(float(x), float(y)) for x, y in corners], fill=255)
    if grow_px > 0:
        image = image.filter(ImageFilter.MaxFilter(2 * grow_px + 1))
    elif grow_px < 0:
        image = image.filter(ImageFilter.MinFilter(2 * -grow_px + 1))
    return np.asarray(image) > 127


def scale_corners(corners, viewport, image_size):
    sx = image_size[0] / viewport[0] if viewport and viewport[0] else 1.0
    sy = image_size[1] / viewport[1] if viewport and viewport[1] else 1.0
    return [[x * sx, y * sy] for x, y in corners]


def door_regions(shot: dict, size: tuple[int, int]) -> dict | None:
    """Máscaras del hueco, del anillo del marco y de la franja de pared; None si la vista no es de puerta."""
    keys = ("portalCorners", "frameCorners", "wallCorners")
    if not all(len(shot.get(key) or []) == 4 for key in keys):
        return None
    portal, frame, wall = (scale_corners(shot[key], shot.get("viewport"), size) for key in keys)
    return {
        "portal": polygon_mask(size, portal, -EDGE_PX),
        "frame": polygon_mask(size, frame, -EDGE_PX) & ~polygon_mask(size, portal, EDGE_PX),
        "wall": polygon_mask(size, wall, -EDGE_PX) & ~polygon_mask(size, frame, EDGE_PX),
    }


def check_door(pixels: np.ndarray, regions: dict, require_lit_wall: bool = False) -> dict:
    if not all(mask.any() for mask in regions.values()):
        return {"ok": False, "error": "polígono de puerta vacío o fuera de la imagen"}
    rgb = pixels[..., :3].astype(np.float64)
    frame_mean = rgb[regions["frame"]].mean(axis=0)
    portal_std = float(luma(pixels)[regions["portal"]].std())
    difference = float(np.linalg.norm(rgb[regions["portal"]] - frame_mean, axis=1).mean())
    wall_luma = float(luma(pixels)[regions["wall"]].mean())
    ok = portal_std > MIN_PORTAL_STD and difference > MIN_PORTAL_FRAME_DIFF
    if require_lit_wall:
        ok = ok and wall_luma >= MIN_LIT_WALL_LUMA
    return {"ok": ok, "portalStd": round(portal_std, 2), "portalFrameDiff": round(difference, 2),
            "frameLuma": round(float(frame_mean @ LUMA), 2), "wallLuma": round(wall_luma, 2)}


def dark_foreground_fraction(pixels: np.ndarray, fraction: float = FOREGROUND_FRACTION) -> float:
    """Fracción de píxeles con luminancia < DARK_LUMA en la franja inferior de la imagen."""
    height = pixels.shape[0]
    band = pixels[height - max(1, int(round(height * fraction))):]
    return float((luma(band) < DARK_LUMA).mean())


def check_image(path: Path, shot: dict | None = None) -> dict:
    pixels = np.asarray(Image.open(path).convert("RGB"))
    whole = mean_luma(pixels)
    center = mean_luma(center_crop(pixels))
    deviation = float(luma(pixels).std())
    ok = all(MIN_MEAN_LUMA <= value <= MAX_MEAN_LUMA for value in (whole, center)) and deviation > MIN_LUMA_STD
    result = {"file": path.name, "size": [int(pixels.shape[1]), int(pixels.shape[0])],
              "meanLuma": round(whole, 2), "centerLuma": round(center, 2), "lumaStd": round(deviation, 2)}
    if (shot or {}).get("kind") == "inside":
        dark = dark_foreground_fraction(pixels)
        result["darkForeground"] = round(dark, 4)
        ok = ok and dark <= MAX_DARK_FOREGROUND
    kind = (shot or {}).get("kind")
    regions = door_regions(shot or {}, (pixels.shape[1], pixels.shape[0]))
    if regions is not None:
        result["door"] = check_door(pixels, regions, kind == "from_biome")
        ok = ok and result["door"]["ok"]
    elif kind in DOOR_KINDS:
        # Sin polígonos, una vista de puerta solo pasaría la luminancia global: se da por fallida.
        result["door"] = {"ok": False, "error": "vista de puerta sin portalCorners, frameCorners o wallCorners"}
        ok = False
    result["ok"] = ok
    return result


def check_tour(tour_dir: Path) -> dict:
    index_path = tour_dir / "tour.json"
    if not index_path.exists():
        return {"ok": False, "error": f"no existe {index_path}", "shots": []}
    index = json.loads(index_path.read_text(encoding="utf-8"))
    if not index.get("complete"):
        return {"ok": False, "error": f"recorrido incompleto: {index.get('error', '')}", "shots": []}

    shots = []
    for shot in index.get("shots", []):
        path = tour_dir / shot["file"]
        if not path.exists():
            shots.append({"file": shot["file"], "ok": False, "error": "no existe"})
            continue
        shots.append(check_image(path, shot))
    ok = bool(shots) and all(shot["ok"] for shot in shots)
    return {"ok": ok, "error": "" if ok else "alguna captura es negra, quemada, uniforme, con el primer plano negro, sin portal distinto del marco o falta",
            "thresholds": {"meanLuma": [MIN_MEAN_LUMA, MAX_MEAN_LUMA], "lumaStd": MIN_LUMA_STD,
                           "portalStd": MIN_PORTAL_STD, "portalFrameDiff": MIN_PORTAL_FRAME_DIFF,
                           "litWallLuma": MIN_LIT_WALL_LUMA,
                           "darkForeground": [FOREGROUND_FRACTION, DARK_LUMA, MAX_DARK_FOREGROUND]},
            "shots": shots}


def describe(shot: dict) -> str:
    text = f"media {shot.get('meanLuma', '-')}, centro {shot.get('centerLuma', '-')}, desv. {shot.get('lumaStd', '-')}"
    if "darkForeground" in shot:
        text += f"; primer plano < {DARK_LUMA:g}: {100 * shot['darkForeground']:.1f} %"
    door = shot.get("door")
    if door:
        text += (f"; hueco desv. {door.get('portalStd', '-')}, hueco-marco {door.get('portalFrameDiff', '-')}, "
                 f"pared {door.get('wallLuma', '-')}")
    return text


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tour", required=True, type=Path)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()

    result = check_tour(args.tour)
    for shot in result["shots"]:
        mark = "OK  " if shot["ok"] else "FAIL"
        print(f"{mark} {shot['file']}: {describe(shot)}")
    print(f"Tour: {'OK' if result['ok'] else 'FALLO'} ({len(result['shots'])} capturas) {result['error']}")
    if args.out:
        args.out.write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding="utf-8")
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
