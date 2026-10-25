"""Registro de kits: id de línea de comandos → módulo (cada uno expone KIT_NAME y build())."""
from __future__ import annotations

from pathlib import Path

import kit_crops
import kit_hub
import kit_ice
import kit_returnwall

KITS = {
    "hub": kit_hub,
    "returnwall": kit_returnwall,
    "ice": kit_ice,
    "crops": kit_crops,
}

REPO_ROOT = Path(__file__).resolve().parents[3]
EXPORT_ROOT = REPO_ROOT / "Art" / "Export"


def select(ids: str) -> list[tuple[str, object]]:
    """'all' o lista separada por comas; error explícito si un id no existe."""
    wanted = list(KITS) if ids in ("", "all") else [i.strip().lower() for i in ids.split(",") if i.strip()]
    unknown = [i for i in wanted if i not in KITS]
    if unknown:
        raise SystemExit(f"kits desconocidos: {unknown}; disponibles: {list(KITS)}")
    return [(i, KITS[i]) for i in wanted]


def fbx_path(kit_name: str, asset_name: str, root: Path = EXPORT_ROOT) -> Path:
    return root / kit_name / f"{asset_name}.fbx"
