"""Compone la lámina de Liminal.Tour (etapa E4): una fila por bioma con la puerta vista desde el hub,
la puerta de vuelta vista desde el bioma y el bioma visto desde dentro (de espaldas a la puerta).

Uso: uv run --no-project --with pillow python make_lamina.py --tour Saved/Tour [--out Saved/Tour/lamina.png]
Lee tour.json (campos kind y biome de cada captura). Sale con código 1 si falta alguna captura.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

COLUMNS = (("from_hub", "Desde el hub"), ("from_biome", "Puerta de vuelta"), ("inside", "Desde dentro"))
THUMB_SIZE = (640, 360)
MARGIN = 16
HEADER = 44
ROW_LABEL = 170
BACKGROUND = (24, 24, 24)
TEXT = (235, 235, 235)


def load_font(size: int):
    try:
        return ImageFont.truetype("arial.ttf", size)
    except OSError:
        return ImageFont.load_default()


def collect_rows(index: dict) -> list[tuple[str, dict]]:
    """Filas en el orden del recorrido: biome -> {kind: fichero}."""
    rows: dict[str, dict] = {}
    for shot in index.get("shots", []):
        biome = shot.get("biome", "")
        kind = shot.get("kind", "")
        if biome and kind in dict(COLUMNS):
            rows.setdefault(biome, {})[kind] = shot["file"]
    return list(rows.items())


def compose(tour_dir: Path, rows: list[tuple[str, dict]]) -> Image.Image:
    width = ROW_LABEL + len(COLUMNS) * (THUMB_SIZE[0] + MARGIN) + MARGIN
    height = HEADER + len(rows) * (THUMB_SIZE[1] + MARGIN) + MARGIN
    sheet = Image.new("RGB", (width, height), BACKGROUND)
    draw = ImageDraw.Draw(sheet)
    font = load_font(24)
    for column, (_kind, title) in enumerate(COLUMNS):
        draw.text((ROW_LABEL + column * (THUMB_SIZE[0] + MARGIN) + 8, 10), title, fill=TEXT, font=font)
    for row, (biome, files) in enumerate(rows):
        top = HEADER + row * (THUMB_SIZE[1] + MARGIN)
        label = biome.split("_", 1)[-1]
        draw.text((MARGIN, top + THUMB_SIZE[1] // 2 - 12), label, fill=TEXT, font=font)
        for column, (kind, _title) in enumerate(COLUMNS):
            with Image.open(tour_dir / files[kind]) as image:
                thumb = image.convert("RGB").resize(THUMB_SIZE, Image.LANCZOS)
            sheet.paste(thumb, (ROW_LABEL + column * (THUMB_SIZE[0] + MARGIN), top))
    return sheet


def make_lamina(tour_dir: Path, out: Path) -> tuple[bool, str]:
    index_path = tour_dir / "tour.json"
    if not index_path.exists():
        return False, f"no existe {index_path}"
    rows = collect_rows(json.loads(index_path.read_text(encoding="utf-8")))
    if not rows:
        return False, "el índice no tiene vistas de bioma"
    missing = [f"{biome}/{kind}" for biome, files in rows for kind, _ in COLUMNS
               if kind not in files or not (tour_dir / files[kind]).exists()]
    if missing:
        return False, "faltan capturas: " + ", ".join(missing)
    compose(tour_dir, rows).save(out)
    return True, f"{len(rows)} biomas"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tour", required=True, type=Path)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    out = args.out or args.tour / "lamina.png"
    ok, message = make_lamina(args.tour, out)
    print(f"Lámina: {'OK' if ok else 'FALLO'} ({message}) -> {out}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
