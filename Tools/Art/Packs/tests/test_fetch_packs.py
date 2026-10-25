"""Descarga reproducible: manifiesto, sha256, extracción segura y caché (sin red)."""

from __future__ import annotations

import hashlib
import io
import zipfile
from pathlib import Path

import pytest

import fetch_packs as fp


def make_zip(entries: dict[str, bytes], symlink: str | None = None) -> bytes:
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as z:
        for name, data in entries.items():
            z.writestr(name, data)
        if symlink:
            info = zipfile.ZipInfo(symlink)
            info.external_attr = (0o120777 << 16)
            z.writestr(info, "/etc/passwd")
    return buf.getvalue()


def test_manifest_is_complete():
    for p in fp.load_manifest()["packs"]:
        assert p["license"]["spdx"] == "CC0-1.0", p["id"]
        assert len(p["sha256"]) == 64, p["id"]
        assert p["source"]["kind"] in ("direct", "itch"), p["id"]
        assert p["page"].startswith("https://"), p["id"]


def test_fetch_uses_cached_zip_and_extracts(tmp_path: Path, monkeypatch):
    data = make_zip({"OBJ/a.obj": b"v 0 0 0\n"})
    (tmp_path / "x.zip").write_bytes(data)
    monkeypatch.setattr(fp, "download", lambda *a: pytest.fail("no debe descargar"))
    pack = {"id": "x", "sha256": hashlib.sha256(data).hexdigest()}
    out = fp.fetch(pack, tmp_path)
    assert (out / "OBJ" / "a.obj").read_bytes() == b"v 0 0 0\n"
    assert (out / ".sha256").read_text().strip() == pack["sha256"]


def test_fetch_rejects_changed_file(tmp_path: Path, monkeypatch):
    data = make_zip({"a.obj": b"x"})
    monkeypatch.setattr(fp, "download", lambda pack, dest: dest.write_bytes(data))
    with pytest.raises(RuntimeError, match="sha256"):
        fp.fetch({"id": "x", "sha256": "0" * 64}, tmp_path)
    assert not (tmp_path / "x.zip").exists()


def test_fetch_without_sha_needs_update_flag(tmp_path: Path, monkeypatch):
    data = make_zip({"a.obj": b"x"})
    monkeypatch.setattr(fp, "download", lambda pack, dest: dest.write_bytes(data))
    with pytest.raises(RuntimeError, match="update-sha"):
        fp.fetch({"id": "x"}, tmp_path)
    pack = {"id": "x"}
    fp.fetch(pack, tmp_path, update_sha=True)
    assert pack["sha256"] == hashlib.sha256(data).hexdigest()


@pytest.mark.parametrize("entries, symlink, fragment", [
    ({"../evil.obj": b"x"}, None, "fuera de la carpeta"),
    ({"a.obj": b"x"}, "link", "enlace simbólico"),
])
def test_fetch_rejects_unsafe_zip(tmp_path: Path, monkeypatch, entries, symlink, fragment):
    data = make_zip(entries, symlink)
    (tmp_path / "x.zip").write_bytes(data)
    with pytest.raises(RuntimeError, match=fragment):
        fp.fetch({"id": "x", "sha256": hashlib.sha256(data).hexdigest()}, tmp_path)
    assert not (tmp_path / "evil.obj").exists()


def test_check_members_rejects_bombs(tmp_path: Path, monkeypatch):
    monkeypatch.setattr(fp, "MAX_MEMBER_BYTES", 10)
    info = zipfile.ZipInfo("a.obj")
    info.file_size = 11
    with pytest.raises(RuntimeError, match="descomprimido"):
        fp.check_members([info], tmp_path / "x", "x")


def test_source_url_direct_and_unknown():
    assert fp.source_url(None, {"kind": "direct", "url": "https://k/a.zip"}) == "https://k/a.zip"  # type: ignore[arg-type]
    with pytest.raises(ValueError):
        fp.source_url(None, {"kind": "drive"})  # type: ignore[arg-type]


def test_cache_dir_env(monkeypatch, tmp_path: Path):
    monkeypatch.setenv("LIMINAL_PACKS_CACHE", str(tmp_path))
    assert fp.cache_dir() == tmp_path
    monkeypatch.delenv("LIMINAL_PACKS_CACHE")
    assert fp.cache_dir() == fp.REPO_ROOT / "Art" / "Packs"


def test_main_list_and_unknown(capsys):
    assert fp.main(["--list"]) == 0
    assert "quaternius_ultimate_nature" in capsys.readouterr().out
    assert fp.main(["no_existe"]) == 2
