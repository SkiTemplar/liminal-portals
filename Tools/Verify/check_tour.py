"""Comprueba las capturas de Liminal.Tour (etapa E3): que existen todas las del índice y que no son
negras ni están quemadas, midiendo la luminancia media (Rec. 709 sobre los valores sRGB 0-255) de la
imagen completa y del recuadro central (donde queda la puerta en las vistas de puerta).

Uso: uv run --no-project --with numpy --with pillow python check_tour.py --tour Saved/Tour [--out result.json]
Sale con código 1 si alguna captura no cumple; escribe el resultado en JSON.
"""

import argparse
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image

LUMA = np.array([0.2126, 0.7152, 0.0722])
# Umbrales en niveles sRGB (0-255): por debajo, negra; por encima, quemada.
MIN_MEAN_LUMA = 15.0
MAX_MEAN_LUMA = 245.0
# Recuadro central: un 20 % del ancho y del alto.
CENTER_FRACTION = 0.2


def mean_luma(pixels: np.ndarray) -> float:
    return float((pixels[..., :3].astype(np.float64) @ LUMA).mean())


def center_crop(pixels: np.ndarray, fraction: float = CENTER_FRACTION) -> np.ndarray:
    height, width = pixels.shape[:2]
    half_w = max(1, int(width * fraction / 2))
    half_h = max(1, int(height * fraction / 2))
    cx, cy = width // 2, height // 2
    return pixels[cy - half_h:cy + half_h, cx - half_w:cx + half_w]


def check_image(path: Path) -> dict:
    pixels = np.asarray(Image.open(path).convert("RGB"))
    whole = mean_luma(pixels)
    center = mean_luma(center_crop(pixels))
    ok = all(MIN_MEAN_LUMA <= value <= MAX_MEAN_LUMA for value in (whole, center))
    return {"file": path.name, "size": [int(pixels.shape[1]), int(pixels.shape[0])],
            "meanLuma": round(whole, 2), "centerLuma": round(center, 2), "ok": ok}


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
        shots.append(check_image(path))
    ok = bool(shots) and all(shot["ok"] for shot in shots)
    return {"ok": ok, "error": "" if ok else "alguna captura es negra, está quemada o falta",
            "thresholds": [MIN_MEAN_LUMA, MAX_MEAN_LUMA], "shots": shots}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tour", required=True, type=Path)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()

    result = check_tour(args.tour)
    for shot in result["shots"]:
        mark = "OK  " if shot["ok"] else "FAIL"
        print(f"{mark} {shot['file']}: media {shot.get('meanLuma', '-')}, centro {shot.get('centerLuma', '-')}")
    print(f"Tour: {'OK' if result['ok'] else 'FALLO'} ({len(result['shots'])} capturas) {result['error']}")
    if args.out:
        args.out.write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding="utf-8")
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
