#!/usr/bin/env python3
"""Fail CI if a ROM, retail dump hash, or unexpected large blob is in the tree.

SilverIris never ships GoldenEye assets or images. This scanner is the
merge gate from PR-01. It does not compile the game.
"""

from __future__ import annotations

import hashlib
import os
import stat
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

ROM_SUFFIXES = {".z64", ".n64", ".v64", ".rom"}
ROM_NAMES = {"baserom", "ge007"}

RETAIL_SHA1 = {
    "abe01e4aeb033b6c0836819f549c791b26cfde83": "NTSC-U ge007.u.z64",
    "2a5dade32f7fad6c73c659d2026994632c1b3174": "NTSC-J ge007.j.z64",
    "167c3c433dec1f1eb921736f7d53fac8cb45ee31": "PAL-E ge007.e.z64",
}

MAX_BLOB = 256 * 1024

SKIP_DIR_NAMES = {
    ".git",
    "node_modules",
    "CMakeFiles",
    "__pycache__",
    ".venv",
    "build",
    "dist",
}


def rel(path: Path, root: Path) -> str:
    try:
        return path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError:
        return str(path)


def is_under(path: Path, root: Path, *parts: str) -> bool:
    r = rel(path, root)
    prefix = "/".join(parts)
    return r == prefix or r.startswith(prefix + "/")


def size_allowlisted(path: Path, root: Path) -> bool:
    name = path.name
    if is_under(path, root, "third_party", "goldeneye_src"):
        return True
    if name.endswith(".wasm") or name.endswith(".js.map"):
        return True
    if is_under(path, root, "web", "shell", "dist"):
        return True
    return False


def looks_like_rom_name(path: Path) -> bool:
    """True only for dump images, not decomp sidecars like ge007.u.sha1."""
    suffix = path.suffix.lower()
    if suffix in ROM_SUFFIXES:
        return True
    return path.name.lower() in ROM_NAMES


def is_probably_binary(data: bytes) -> bool:
    if not data:
        return False
    if b"\x00" in data[:4096]:
        return True
    # High ratio of non-text bytes in the head.
    sample = data[:4096]
    textish = sum(1 for b in sample if 9 <= b <= 13 or 32 <= b <= 126)
    return textish / len(sample) < 0.75


def iter_files(root: Path):
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIR_NAMES]
        for name in filenames:
            p = Path(dirpath) / name
            try:
                st = p.lstat()
            except OSError:
                continue
            if stat.S_ISLNK(st.st_mode) or not stat.S_ISREG(st.st_mode):
                continue
            yield p, st.st_size


def scan(root: Path) -> list[str]:
    errors: list[str] = []
    for path, size in iter_files(root):
        r = rel(path, root)
        suffix = path.suffix.lower()

        if suffix in ROM_SUFFIXES or looks_like_rom_name(path):
            errors.append(f"ROM-like file forbidden: {r}")

        if is_under(path, root, "assets") and not is_under(path, root, "third_party"):
            try:
                head = path.read_bytes()[:8192]
            except OSError as exc:
                errors.append(f"unreadable {r}: {exc}")
                continue
            if is_probably_binary(head):
                errors.append(f"binary payload under assets/: {r}")

        if size > MAX_BLOB and not size_allowlisted(path, root):
            errors.append(
                f"blob larger than 256 KiB (not allowlisted): {r} ({size} bytes)"
            )

        # Hash ROM-like names always. Also hash 8–64 MiB blobs (retail GE is
        # ~12 MiB) even if renamed. Skip hashing huge toolchain binaries.
        should_hash = looks_like_rom_name(path) or (
            8 * 1024 * 1024 <= size <= 64 * 1024 * 1024
        )
        if not should_hash:
            continue
        h = hashlib.sha1()
        try:
            with path.open("rb") as fh:
                for chunk in iter(lambda: fh.read(1024 * 1024), b""):
                    h.update(chunk)
        except OSError as exc:
            errors.append(f"unreadable {r}: {exc}")
            continue
        digest = h.hexdigest()
        if digest in RETAIL_SHA1:
            errors.append(f"retail dump {RETAIL_SHA1[digest]} present as {r}")
    return errors


def self_test() -> int:
    failures = 0

    def expect(cond: bool, msg: str) -> None:
        nonlocal failures
        if not cond:
            print(f"FAIL: {msg}", file=sys.stderr)
            failures += 1

    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        # Synthetic z64 header — not a retail dump.
        z64 = tmp_path / "fake.z64"
        z64.write_bytes(b"\x80\x37\x12\x40" + b"\x00" * 64)
        errs = scan(tmp_path)
        expect(any("ROM-like" in e for e in errs), "must reject .z64 suffix")

    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        big = tmp_path / "oversized.bin"
        big.write_bytes(b"A" * (MAX_BLOB + 1))
        errs = scan(tmp_path)
        expect(any("256 KiB" in e for e in errs), "must reject oversized blob")

    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        assets = tmp_path / "assets"
        assets.mkdir()
        blob = assets / "texture.bin"
        blob.write_bytes(b"\x00\x01\x02\x03" * 32)
        errs = scan(tmp_path)
        expect(any("assets/" in e for e in errs), "must reject binary under assets/")

    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        expect(
            "abe01e4aeb033b6c0836819f549c791b26cfde83" in RETAIL_SHA1,
            "NTSC-U hash must be in the reject table",
        )
        expect(len(RETAIL_SHA1) == 3, "all three retail hashes must be rejected")

    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        planted = tmp_path / "planted.z64"
        payload = b"silveriris-guard-sentinel\n"
        planted.write_bytes(payload)
        digest = hashlib.sha1(payload).hexdigest()
        old = dict(RETAIL_SHA1)
        RETAIL_SHA1[digest] = "sentinel"
        try:
            errs = scan(tmp_path)
            expect(
                any("retail dump" in e for e in errs),
                "must reject a blob whose SHA-1 is in the retail table",
            )
            expect(
                any("ROM-like" in e for e in errs),
                "must still reject .z64 suffix",
            )
        finally:
            RETAIL_SHA1.clear()
            RETAIL_SHA1.update(old)

    if failures:
        print(f"{failures} self-test failure(s)", file=sys.stderr)
        return 1
    print("guard self-test ok")
    return 0


def main(argv: list[str]) -> int:
    if "--self-test" in argv:
        return self_test()
    errors = scan(ROOT)
    if errors:
        print("no-assets guard failed:", file=sys.stderr)
        for e in errors:
            print(f"  {e}", file=sys.stderr)
        return 1
    print("no-assets guard ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
