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


def rare_i8(tex: bytes, w=8, h=8) -> bytes:
    # flags=0x01 non-zlib lod1; format=7 I8; method=0 uncompressed
    return bytes([0x01, 0x70, (w << 4) | ((h >> 4) & 0xF), (h << 4) & 0xF0]) + tex


def zlib_ci4(indices: bytes, w=8, h=8) -> bytes:
    return bytes([0x41, 0x0A, 0x01, 0xF8, 0x01, 0x07, 0xC1, w, h]) + wrap1172_deflate(indices)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("outdir", type=Path)
    args = ap.parse_args()
    args.outdir.mkdir(parents=True, exist_ok=True)
    (args.outdir / "checker.rare.bin").write_bytes(rare_i8(i8_checker()))
    (args.outdir / "checker.zbank.bin").write_bytes(zlib_ci4(ci4_checker()))


if __name__ == "__main__":
    main()
