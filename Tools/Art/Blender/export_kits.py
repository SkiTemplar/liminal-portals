"""Exporta los kits a Art/Export/<Kit>/SM_<Kit>_<Cosa>_<NN>.fbx (Blender en segundo plano).

    blender -b --factory-startup --python Tools/Art/Blender/export_kits.py -- [--kits hub,ice|all]
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import lp_blender  # noqa: E402
from kits import EXPORT_ROOT, fbx_path, select  # noqa: E402


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--kits", default="all")
    parser.add_argument("--out", default=str(EXPORT_ROOT))
    args = parser.parse_args(argv)
    out_root = Path(args.out)
    for kit_id, module in select(args.kits):
        lp_blender.reset_scene()
        assets = module.build()
        target_dir = out_root / module.KIT_NAME
        target_dir.mkdir(parents=True, exist_ok=True)
        # Solo se borra lo propio (prefijo del kit) que ya no se genera: la carpeta puede ser compartida.
        current = {asset.spec.name for asset in assets}
        for stale in target_dir.glob(f"SM_{module.KIT_NAME}_*.fbx"):
            if stale.stem not in current:
                stale.unlink()
        for asset in assets:
            obj = lp_blender.mesh_to_object(asset.spec.name, asset.mesh, asset.gradient)
            lp_blender.export_fbx(obj, str(fbx_path(module.KIT_NAME, asset.spec.name, out_root)))
        print(f"[export] {kit_id}: {len(assets)} mallas en {target_dir}")
    return 0


if __name__ == "__main__":
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    sys.exit(main(argv))
