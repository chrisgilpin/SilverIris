#!/usr/bin/env python3
"""Emit synthetic Rare image-bank blobs. No ROM bytes."""
import argparse
import zlib
from pathlib import Path


def wrap1172_deflate(data: bytes) -> bytes:
    c = zlib.compressobj(9, zlib.DEFLATED, -15)
    return bytes([0x11, 0x72]) + c.compress(data) + c.flush()


def i8_checker(w=8, h=8, a=0x20, b=0xE0) -> bytes:
    out = bytearray(w * h)
    for y in range(h):
        for x in range(w):
            out[y * w + x] = b if ((x ^ y) & 1) else a
    return bytes(out)


def ia8_checker(w=8, h=8, a=0x2F, b=0xEF) -> bytes:
    return i8_checker(w, h, a, b)


def ia4_checker(w=8, h=8, a=0x5, b=0xD) -> bytes:
    n = (w * h + 1) // 2
    out = bytearray(n)
    i = 0
    for y in range(h):
        for x in range(w):
            v = b if ((x ^ y) & 1) else a
            if (i & 1) == 0:
                out[i >> 1] = (v << 4) & 0xFF
            else:
                out[i >> 1] |= v & 0x0F
            i += 1
    return bytes(out)


def ci4_checker(w=8, h=8) -> bytes:
    n = (w * h + 1) // 2
    out = bytearray(n)
    i = 0
    for y in range(h):
        for x in range(w):
            v = 1 if ((x ^ y) & 1) else 0
            if (i & 1) == 0:
                out[i >> 1] = (v << 4) & 0xFF
            else:
                out[i >> 1] |= v & 0x0F
            i += 1
    return bytes(out)


def rare_fmt(fmt: int, tex: bytes, w=8, h=8) -> bytes:
    # flags=0x01 non-zlib lod1; format4 / w8 / h8 / method4 uncompressed
    return bytes([0x01, (fmt << 4) | ((w >> 4) & 0xF), ((w << 4) | ((h >> 4) & 0xF)) & 0xFF, (h << 4) & 0xF0]) + tex


def zlib_ci4(indices: bytes, w=8, h=8) -> bytes:
    return bytes([0x41, 0x0A, 0x01, 0xF8, 0x01, 0x07, 0xC1, w, h]) + wrap1172_deflate(indices)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("outdir", type=Path)
    args = ap.parse_args()
    args.outdir.mkdir(parents=True, exist_ok=True)
    (args.outdir / "checker.rare.bin").write_bytes(rare_fmt(7, i8_checker()))
    (args.outdir / "checker.zbank.bin").write_bytes(zlib_ci4(ci4_checker()))
    (args.outdir / "floor.ia8.bin").write_bytes(rare_fmt(5, ia8_checker()))
    (args.outdir / "floor.ia4.bin").write_bytes(rare_fmt(6, ia4_checker()))


if __name__ == "__main__":
    main()
