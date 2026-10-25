"""Resume Saved/BenchProfile.json (Liminal.Bench en modo perfil, D-36).

Por caso separa la vista principal ("Frame/SceneRender - ViewFamilies") de cada captura
("Frame/WorldTick/LiminalCapture <portal>") y reparte el tiempo exclusivo de cada miga en categorías
(Lumen, sombras, base pass, niebla, TSR...) según la miga más profunda de su ruta que case con una regla.
Las colas se suman: con cómputo asíncrono (r.RDG.AsyncCompute 1) la suma pasa del tiempo del fotograma
porque las colas se solapan; con r.RDG.AsyncCompute 0 el desglose es aditivo.

Uso: uv run --no-project python summarize_profile.py --profile Saved/BenchProfile.json --out resumen.json
"""

from __future__ import annotations

import argparse
import json
import sys
from collections import defaultdict
from pathlib import Path

# (categoría, subcadenas del nombre de la miga). Gana la miga más profunda que case; en la misma miga,
# la primera regla de la lista.
RULES: list[tuple[str, tuple[str, ...]]] = [
    ("lumenScreenProbeGather", ("LumenScreenProbeGather",)),
    ("lumenReflections", ("LumenReflections", "ReflectionsAndRefraction")),
    ("lumenSceneLighting", ("LumenSceneLighting",)),
    ("lumenSceneUpdate", ("LumenSceneUpdate", "UpdateLumenScene", "LumenCardScene", "LumenSurfaceCache",
                          "UpdateLumenSurfaceCache", "LumenScene")),
    ("lumenTranslucencyVolume", ("TranslucencyVolumeLighting", "LumenTranslucency")),
    ("lumenOther", ("Lumen",)),
    ("shadowsVsm", ("VirtualShadowMap", "ShadowDepths", "ShadowProjection", "Shadow")),
    ("tsr", ("TemporalSuperResolution", "TAA")),
    ("postProcess", ("PostProcessing", "Tonemap", "Bloom")),
    ("fog", ("Fog",)),
    ("skyAtmosphere", ("SkyAtmosphere", "Sky")),
    ("basePass", ("BasePass",)),
    ("prePass", ("PrePass", "DepthPass")),
    ("naniteInstances", ("Nanite", "InstanceCulling", "GPUScene", "BuildRenderingCommands", "CullInstances")),
    ("distanceFields", ("DistanceField", "GlobalDistanceField", "MeshSDF")),
    ("translucency", ("Translucency",)),
    ("deferredLights", ("Lights", "RenderDeferredLighting")),
    ("hzbVelocity", ("HZB", "Velocity")),
]
CATEGORIES = [name for name, _ in RULES] + ["other"]

MAIN_PREFIX = "Frame/SceneRender - ViewFamilies"
CAPTURE_MARK = "/LiminalCapture "


def classify_name(name: str) -> str | None:
    for category, needles in RULES:
        if any(needle in name for needle in needles):
            return category
    return None


def classify_path(levels: list[str]) -> str:
    for name in reversed(levels):
        category = classify_name(name)
        if category:
            return category
    return "other"


def view_of(path_without_queue: str) -> str | None:
    """'main', 'capture:<portal>' o None (resto del fotograma)."""
    if path_without_queue.startswith(MAIN_PREFIX):
        return "main"
    index = path_without_queue.find(CAPTURE_MARK)
    if index >= 0:
        rest = path_without_queue[index + len(CAPTURE_MARK):]
        return "capture:" + rest.split("/", 1)[0]
    return None


def exclusive_times(paths: list[dict]) -> dict[str, float]:
    """Tiempo exclusivo por ruta: inclusivo menos el de sus hijas directas presentes en el informe."""
    inclusive = {p["path"]: float(p["ms"]) for p in paths}
    children_sum: dict[str, float] = defaultdict(float)
    for path, ms in inclusive.items():
        parent = path.rsplit("/", 1)[0] if "/" in path else None
        if parent in inclusive:
            children_sum[parent] += ms
    return {path: max(ms - children_sum.get(path, 0.0), 0.0) for path, ms in inclusive.items()}


def summarize_case(case: dict) -> dict:
    exclusive = exclusive_times(case["paths"])
    views: dict[str, dict[str, dict[str, float]]] = defaultdict(lambda: defaultdict(lambda: defaultdict(float)))
    for path, ms in exclusive.items():
        queue, _, rest = path.partition("/")
        view = view_of(rest)
        if view is None:
            continue
        category = classify_path(rest.split("/"))
        views[view][queue][category] += ms

    def finish(per_queue: dict[str, dict[str, float]], divisor: float = 1.0) -> dict:
        queues = {q: {c: v / divisor for c, v in sorted(cats.items())} for q, cats in per_queue.items()}
        total = defaultdict(float)
        for cats in queues.values():
            for category, value in cats.items():
                total[category] += value
        return {
            "totalMs": sum(total.values()),
            "byCategory": {c: total.get(c, 0.0) for c in CATEGORIES},
            "byQueue": {q: sum(cats.values()) for q, cats in queues.items()},
            "byQueueCategory": queues,
        }

    captures = sorted(v for v in views if v.startswith("capture:"))
    capture_sum: dict[str, dict[str, float]] = defaultdict(lambda: defaultdict(float))
    for view in captures:
        for queue, cats in views[view].items():
            for category, value in cats.items():
                capture_sum[queue][category] += value

    return {
        "name": case["name"],
        "captures": case.get("captures"),
        "frames": case.get("frames"),
        "gpuMsMean": case.get("gpuMsMean"),
        "fpsMean": case.get("fpsMean"),
        "gameThreadMsMean": case.get("gameThreadMsMean"),
        "renderThreadMsMean": case.get("renderThreadMsMean"),
        "rhiThreadMsMean": case.get("rhiThreadMsMean"),
        "queueBusyMsPerFrame": case.get("queueBusyMsPerFrame"),
        "main": finish(views["main"]) if "main" in views else None,
        "captureViews": {view.split(":", 1)[1]: finish(views[view]) for view in captures},
        "capturePerView": finish(capture_sum, float(len(captures))) if captures else None,
    }


def summarize_crossings(crossings: dict) -> dict:
    """Picos al cruzar: por puerta, mediana antes del cruce y máximo desde el cruce (sin las series)."""
    keep = ("portal", "pass", "crossed", "frameMsMedianBefore", "frameMsMaxFromCross", "spikeRatio", "gpuMsMax",
            "renderThreadMsMax", "gameThreadMsMax")
    summary = {k: v for k, v in crossings.items() if k != "doors"}
    summary["doors"] = [{k: door.get(k) for k in keep} for door in crossings.get("doors", [])]
    return summary


def summarize(profile: dict) -> dict:
    return {
        "complete": profile.get("complete"),
        "error": profile.get("error"),
        "gpu": profile.get("gpu"),
        "viewport": profile.get("viewport"),
        "renderTarget": profile.get("renderTarget"),
        "cvars": profile.get("cvars"),
        "cases": [summarize_case(case) for case in profile.get("cases", [])],
        "walk": {k: v for k, v in profile.get("walk", {}).items() if k != "samples"},
        "crossings": summarize_crossings(profile.get("crossings", {})),
    }


def print_table(summary: dict) -> None:
    for case in summary["cases"]:
        main = case["main"] or {"totalMs": 0.0, "byCategory": {}}
        capture = case["capturePerView"]
        print(f"{case['name']:<18} GPU {case['gpuMsMean']:.2f} ms, {case['fpsMean']:.1f} fps; principal {main['totalMs']:.2f} ms"
              + (f"; captura media {capture['totalMs']:.2f} ms" if capture else ""))
        for category in CATEGORIES:
            m = main["byCategory"].get(category, 0.0)
            c = capture["byCategory"].get(category, 0.0) if capture else 0.0
            if m >= 0.05 or c >= 0.05:
                print(f"    {category:<24} {m:6.2f} {c:6.2f}")
    for door in summary.get("crossings", {}).get("doors", []):
        print(f"cruce {door['portal']} (pasada {door.get('pass', 1)}): mediana antes {door['frameMsMedianBefore']:.2f} ms, máximo desde el cruce "
              f"{door['frameMsMaxFromCross']:.2f} ms (x{door['spikeRatio']:.2f}), GPU máx. {door['gpuMsMax']:.2f} ms")
    walk = summary["walk"]
    if walk:
        print(f"recorrido: {walk.get('poses')} poses, capturas {walk.get('capturesMean', 0):.2f}/fotograma, "
              f"superficie dibujada {walk.get('renderedMean', 0):.2f}, área por portal {walk.get('areaPerCapturedPortalMean', 0):.3f}, "
              f"GPU {walk.get('gpuMs', {}).get('mean', 0):.2f} ms")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--profile", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()
    profile = json.loads(args.profile.read_text(encoding="utf-8"))
    summary = summarize(profile)
    args.out.write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")
    if not args.quiet:
        print_table(summary)
    if not profile.get("complete"):
        print(f"Perfil incompleto: {profile.get('error')}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
