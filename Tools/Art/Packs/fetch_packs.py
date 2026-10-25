"""Descarga y verifica los packs CC0 de ``packs.json`` en una caché fuera de git.

Uso (desde ``Tools/Art/Packs``):
    uv run python fetch_packs.py                 # todos los packs
    uv run python fetch_packs.py kenney_nature_kit
    uv run python fetch_packs.py --list          # packs, versión y licencia
    uv run python fetch_packs.py --update-sha    # rellena sha256 vacíos (solo al añadir un pack)

Caché: ``Art/Packs/`` en la raíz del proyecto (en ``.gitignore``) o ``$LIMINAL_PACKS_CACHE``.
Cada pack queda como ``<caché>/<id>.zip`` (el original, verificado con sha256) y
``<caché>/<id>/`` (extraído). Lo descargado es dato no confiable: solo se extrae (con
comprobación de rutas y tamaños), nunca se ejecuta nada de dentro.

Fuentes admitidas (adaptado de Explored, ``Tools/Packs/fetch_packs.py``):
- ``direct``: URL fija al zip (Kenney publica cada versión con su propio hash en la ruta).
- ``itch``: página de itch.io de precio libre. Mismo flujo que el botón «No thanks, just take
  me to the downloads»: POST a ``<página>/file/<upload_id>`` devuelve una URL firmada de
  corta duración. ``upload_id`` fija el fichero; si el autor lo cambia, falla el sha256.
"""

from __future__ import annotations

import argparse
import hashlib
import http.cookiejar
import json
import os
import re
import shutil
import sys
import urllib.parse
import urllib.request
import zipfile
from pathlib import Path
from typing import Any

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[2]
MANIFEST = HERE / "packs.json"
USER_AGENT = "Mozilla/5.0 (LiminalPortals fetch_packs.py)"
MAX_MEMBER_BYTES = 512 * 1024 * 1024
MAX_TOTAL_BYTES = 4 * 1024 * 1024 * 1024

CSRF_RE = re.compile(r'name="csrf_token" (?:value|content)="([^"]+)"')


def cache_dir() -> Path:
    env = os.environ.get("LIMINAL_PACKS_CACHE")
    return Path(env) if env else REPO_ROOT / "Art" / "Packs"


def load_manifest(path: Path = MANIFEST) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def sha256_of(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def _opener() -> urllib.request.OpenerDirector:
    jar = http.cookiejar.CookieJar()
    op = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(jar))
    op.addheaders = [("User-Agent", USER_AGENT)]
    return op


def _read(op: urllib.request.OpenerDirector, url: str, data: dict[str, str] | None = None) -> bytes:
    body = urllib.parse.urlencode(data).encode() if data is not None else None
    with op.open(urllib.request.Request(url, data=body), timeout=60) as r:
        return r.read()


def itch_file_url(op: urllib.request.OpenerDirector, page: str, upload_id: int) -> str:
    """URL firmada del upload ``upload_id`` de una página itch.io de precio libre."""
    html = _read(op, page).decode("utf-8", "replace")
    m = CSRF_RE.search(html)
    token = m.group(1) if m else ""
    raw = _read(op, f"{page}/file/{upload_id}?source=game_download", {"csrf_token": token})
    reply = json.loads(raw)
    if "url" not in reply:
        raise RuntimeError(f"itch.io no dio URL para {page} upload {upload_id}: {reply}")
    url = reply["url"]
    if not url.startswith("https://"):
        raise RuntimeError(f"itch.io devolvió una URL no https: {url}")
    return url


def source_url(op: urllib.request.OpenerDirector, src: dict[str, Any]) -> str:
    if src["kind"] == "direct":
        return src["url"]
    if src["kind"] == "itch":
        return itch_file_url(op, src["page"], src["uploadId"])
    raise ValueError(f"fuente desconocida: {src['kind']}")


def download(pack: dict[str, Any], dest: Path) -> None:
    op = _opener()
    url = source_url(op, pack["source"])
    tmp = dest.with_suffix(".part")
    with op.open(urllib.request.Request(url), timeout=300) as r, tmp.open("wb") as f:
        shutil.copyfileobj(r, f)
    tmp.replace(dest)


def check_members(infos: list[zipfile.ZipInfo], out: Path, pack_id: str) -> None:
    """Rechaza rutas fuera de ``out``, enlaces simbólicos y tamaños desmesurados (zip bomb)."""
    root = out.resolve()
    total = 0
    for info in infos:
        target = (out / info.filename).resolve()
        if not target.is_relative_to(root):
            raise RuntimeError(f"{pack_id}: ruta fuera de la carpeta en el zip: {info.filename}")
        if (info.external_attr >> 16) & 0o170000 == 0o120000:
            raise RuntimeError(f"{pack_id}: enlace simbólico en el zip: {info.filename}")
        if info.file_size > MAX_MEMBER_BYTES:
            raise RuntimeError(f"{pack_id}: {info.filename} ocupa {info.file_size} bytes descomprimido")
        total += info.file_size
    if total > MAX_TOTAL_BYTES:
        raise RuntimeError(f"{pack_id}: el zip descomprimido ocupa {total} bytes")


def fetch(pack: dict[str, Any], root: Path, *, update_sha: bool = False) -> Path:
    """Descarga (si falta), verifica sha256 y extrae. Devuelve la carpeta extraída."""
    root.mkdir(parents=True, exist_ok=True)
    archive = root / f"{pack['id']}.zip"
    out = root / pack["id"]
    expected = pack.get("sha256") or ""
    if archive.exists() and expected and sha256_of(archive) != expected:
        archive.unlink()
    if not archive.exists():
        download(pack, archive)
    got = sha256_of(archive)
    if not expected:
        if not update_sha:
            raise RuntimeError(f"{pack['id']}: sin sha256 en packs.json (usa --update-sha al añadirlo)")
        pack["sha256"] = got
    elif got != expected:
        archive.unlink()
        raise RuntimeError(f"{pack['id']}: sha256 {got} != {expected} (el autor cambió el fichero)")
    stamp = out / ".sha256"
    if not (stamp.exists() and stamp.read_text().strip() == got):
        if out.exists():
            shutil.rmtree(out)
        with zipfile.ZipFile(archive) as z:
            check_members(z.infolist(), out, pack["id"])
            z.extractall(out)
        stamp.write_text(got + "\n")
    return out


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("ids", nargs="*", help="ids de packs.json (por defecto, todos)")
    parser.add_argument("--list", action="store_true", help="lista los packs sin descargar")
    parser.add_argument("--update-sha", action="store_true", help="escribe en packs.json los sha256 vacíos")
    args = parser.parse_args(argv)

    manifest = load_manifest(MANIFEST)
    packs = {p["id"]: p for p in manifest["packs"]}
    if args.list:
        for p in packs.values():
            print(f"{p['id']:36} {p['author']:10} {p['version']:8} {p['license']['spdx']}")
        return 0
    unknown = [i for i in args.ids if i not in packs]
    if unknown:
        print(f"Packs desconocidos: {', '.join(unknown)}", file=sys.stderr)
        return 2
    root = cache_dir()
    failed = 0
    for pid in args.ids or list(packs):
        try:
            out = fetch(packs[pid], root, update_sha=args.update_sha)
            print(f"[packs] {pid}: {out}")
        except Exception as e:  # noqa: BLE001 - se informa y se sigue con el resto
            failed += 1
            print(f"[packs] {pid}: ERROR {e}", file=sys.stderr)
    if args.update_sha:
        MANIFEST.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
