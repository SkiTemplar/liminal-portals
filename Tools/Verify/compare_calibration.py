"""Compara las pasadas de Liminal.Calibrate (etapa E2 de docs/PLAN-NOCHE.md).

Para cada ejecución (directorio con calibration.json, visible/NNN.png y hidden/NNN.png) calcula la
diferencia media absoluta entre la pasada con la superficie del portal visible y la pasada con ella
oculta, solo dentro de la máscara del portal en pantalla (polígono de calibration.json erosionado unos
píxeles para no medir el antialiasing del borde). La diferencia se da en % de la escala 0-255.

Aceptación: la ejecución de referencia (LagFrames 0) < --max-pct y la de control (LagFrames 1)
> --min-ratio veces la de referencia. Escribe el resultado en --out y sale con código 1 si no se cumple.

Uso:
    uv run --with numpy --with pillow python Tools/Verify/compare_calibration.py \
        --reference Saved/Calibration/lag0 --control Saved/Calibration/lag1 --out Saved/Calibration/result.json
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

PASSES = ("visible", "hidden")


def polygon_mask(size: tuple[int, int], corners: list[list[float]], erode_px: int) -> np.ndarray:
    """Máscara booleana (alto × ancho) del polígono, erosionada erode_px píxeles."""
    width, height = size
    image = Image.new("L", (width, height), 0)
    ImageDraw.Draw(image).polygon([(float(x), float(y)) for x, y in corners], fill=255)
    if erode_px > 0:
        image = image.filter(ImageFilter.MinFilter(2 * erode_px + 1))
    return np.asarray(image) > 127


def masked_abs_diff(a: np.ndarray, b: np.ndarray, mask: np.ndarray) -> tuple[float, int]:
    """Suma de |a - b| por canal dentro de la máscara y número de muestras (píxeles × canales)."""
    if a.shape != b.shape or a.shape[:2] != mask.shape:
        raise ValueError(f"Tamaños distintos: {a.shape}, {b.shape}, máscara {mask.shape}")
    diff = np.abs(a.astype(np.int32) - b.astype(np.int32))[mask]
    return float(diff.sum()), int(diff.size)


def to_percent(mean_level: float) -> float:
    return mean_level / 255.0 * 100.0


def load_rgb(path: Path) -> np.ndarray:
    with Image.open(path) as image:
        return np.asarray(image.convert("RGB"))


def scale_corners(corners, viewport, image_size):
    """Las esquinas vienen en píxeles del viewport; la captura de pantalla podría tener otro tamaño."""
    sx = image_size[0] / viewport[0] if viewport[0] else 1.0
    sy = image_size[1] / viewport[1] if viewport[1] else 1.0
    return [[x * sx, y * sy] for x, y in corners]


def diff_image(a: np.ndarray, b: np.ndarray, mask: np.ndarray, gain: float = 8.0) -> Image.Image:
    """Diferencia amplificada (gris) con la máscara en rojo tenue, para revisión visual."""
    diff = np.abs(a.astype(np.int32) - b.astype(np.int32)).mean(axis=2) * gain
    gray = np.clip(diff, 0, 255).astype(np.uint8)
    rgb = np.stack([gray, gray, gray], axis=2)
    rgb[..., 0] = np.where(mask, np.maximum(rgb[..., 0], 60), rgb[..., 0])
    return Image.fromarray(rgb, "RGB")


def analyse_run(run_dir: Path, erode_px: int, write_images: bool) -> dict:
    report_path = run_dir / "calibration.json"
    report = json.loads(report_path.read_text(encoding="utf-8"))
    if not report.get("complete"):
        raise RuntimeError(f"{report_path}: calibración incompleta ({report.get('error') or 'sin error'})")

    viewport = report["viewport"]
    frames = [f for f in report["frames"] if f["pass"] == "visible"]
    if not frames:
        raise RuntimeError(f"{report_path}: sin fotogramas")

    total_sum, total_count, per_frame = 0.0, 0, []
    for frame in frames:
        name = f"{frame['index']:03d}.png"
        visible = load_rgb(run_dir / "visible" / name)
        hidden = load_rgb(run_dir / "hidden" / name)
        size = (visible.shape[1], visible.shape[0])
        mask = polygon_mask(size, scale_corners(frame["corners"], viewport, size), erode_px)
        frame_sum, frame_count = masked_abs_diff(visible, hidden, mask)
        if frame_count == 0:
            raise RuntimeError(f"{run_dir}: máscara vacía en el fotograma {frame['index']}")
        total_sum += frame_sum
        total_count += frame_count
        per_frame.append({"index": frame["index"], "yawOffset": frame["yawOffset"],
                          "meanDiffPct": round(to_percent(frame_sum / frame_count), 4),
                          "maskPixels": int(mask.sum())})
        if write_images:
            for folder, image in (("mask", Image.fromarray((mask * 255).astype(np.uint8), "L")),
                                  ("diff", diff_image(visible, hidden, mask))):
                (run_dir / folder).mkdir(exist_ok=True)
                image.save(run_dir / folder / name)

    return {
        "dir": str(run_dir),
        "lagFrames": report.get("lagFrames"),
        "flipClipPlane": report.get("flipClipPlane"),
        "antiAliasingMethod": report.get("antiAliasingMethod"),
        "screenPercentage": report.get("screenPercentage"),
        "maxProjectionErrorPx": report.get("maxProjectionErrorPx"),
        "maxCameraDriftCm": report.get("maxCameraDriftCm"),
        "frames": len(per_frame),
        "meanDiffPct": round(to_percent(total_sum / total_count), 4),
        "maxFrameDiffPct": max(f["meanDiffPct"] for f in per_frame),
        "perFrame": per_frame,
    }


def evaluate(reference_pct: float, control_pct: float, max_pct: float, min_ratio: float) -> dict:
    """Criterio de aceptación: referencia < max_pct y control > min_ratio × referencia."""
    ratio = control_pct / reference_pct if reference_pct > 0 else float("inf")
    reference_ok = reference_pct < max_pct
    control_ok = control_pct > min_ratio * reference_pct
    return {"referenceOk": reference_ok, "controlOk": control_ok, "ratio": round(ratio, 3) if ratio != float("inf") else None,
            "accepted": reference_ok and control_ok}


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--reference", type=Path, required=True, help="Ejecución con LagFrames 0")
    parser.add_argument("--control", type=Path, required=True, help="Ejecución con LagFrames 1 (control negativo)")
    parser.add_argument("--extra", type=Path, nargs="*", default=[], help="Ejecuciones informativas (p. ej. clip plane invertido)")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--max-pct", type=float, default=2.0)
    parser.add_argument("--min-ratio", type=float, default=3.0)
    parser.add_argument("--erode", type=int, default=3, help="Píxeles de erosión de la máscara")
    parser.add_argument("--no-images", action="store_true", help="No escribir mask/ ni diff/")
    args = parser.parse_args(argv)

    try:
        reference = analyse_run(args.reference, args.erode, not args.no_images)
        control = analyse_run(args.control, args.erode, not args.no_images)
        extra = [analyse_run(path, args.erode, not args.no_images) for path in args.extra]
    except (OSError, ValueError, RuntimeError, KeyError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2

    verdict = evaluate(reference["meanDiffPct"], control["meanDiffPct"], args.max_pct, args.min_ratio)
    result = {"criteria": {"maxReferencePct": args.max_pct, "minControlRatio": args.min_ratio, "erodePx": args.erode},
              **verdict, "reference": reference, "control": control, "extra": extra}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding="utf-8")

    print(f"Referencia (LagFrames {reference['lagFrames']}): {reference['meanDiffPct']:.3f} % "
          f"(máx. por fotograma {reference['maxFrameDiffPct']:.3f} %)")
    print(f"Control (LagFrames {control['lagFrames']}): {control['meanDiffPct']:.3f} % · ratio {verdict['ratio']}")
    for run in extra:
        print(f"Extra {run['dir']}: {run['meanDiffPct']:.3f} %")
    print(f"Aceptación: {'OK' if verdict['accepted'] else 'NO'} -> {args.out}")
    return 0 if verdict["accepted"] else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
