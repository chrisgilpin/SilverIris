#!/usr/bin/env python3
"""Parse a G0T1 record. Prints opcode histogram and SHA-256 of gfx words.

Developer dumps may contain assets — do not commit those. CI stores the hash
of the Gfx words, not the dump. Opcode histogram is a by-product.
"""

from __future__ import annotations

import argparse
import hashlib
import struct
import sys
from pathlib import Path

MAGIC = b"G0T1"
NSEG = 16

# Classic F3D (not F3DEX2) command bytes, as uint8 of w0>>24.
OPNAMES = {
    0x00: "G_SPNOOP",
    0x01: "G_MTX",
    0x03: "G_MOVEMEM",
    0x04: "G_VTX",
    0x06: "G_DL",
    0xB8: "G_ENDDL",
    0xB9: "G_SETGEOMETRYMODE",
    0xBA: "G_CLEARGEOMETRYMODE",
    0xBB: "G_RDPHALF_1",
    0xBC: "G_MOVEWORD",
    0xBD: "G_POPMTX",
    0xBE: "G_CULLDL",
    0xBF: "G_TRI1",
    0xC0: "G_NOOP",
    0xE4: "G_TEXRECT",
    0xE6: "G_RDPLOADSYNC",
    0xE7: "G_RDPPIPESYNC",
    0xE8: "G_RDPTILESYNC",
    0xE9: "G_RDPFULLSYNC",
    0xF2: "G_SETTILESIZE",
    0xF3: "G_LOADBLOCK",
    0xF5: "G_SETTILE",
    0xFA: "G_SETPRIMCOLOR",
    0xFB: "G_SETENVCOLOR",
    0xFC: "G_SETCOMBINE",
    0xFD: "G_SETTIMG",
    0xFE: "G_SETZIMG",
    0xFF: "G_SETCIMG",
}


def parse_g0(data: bytes) -> dict:
    if len(data) < 12 or data[:4] != MAGIC:
        raise ValueError("not a G0T1 record")
    n_gfx, n_seg = struct.unpack_from("<II", data, 4)
    if n_seg != NSEG:
        raise ValueError(f"n_seg {n_seg} != {NSEG}")
    off = 12
    segs = list(struct.unpack_from("<16I", data, off))
    off += 16 * 4
    mv = list(struct.unpack_from("<16I", data, off))
    off += 16 * 4
    proj = list(struct.unpack_from("<16I", data, off))
    off += 16 * 4
    need = off + n_gfx * 8
    if len(data) < need:
        raise ValueError(f"truncated G0 ({len(data)} < {need})")
    words = data[off:need]
    gfx = []
    hist: dict[int, int] = {}
    for i in range(n_gfx):
        w0, w1 = struct.unpack_from("<II", words, i * 8)
        gfx.append((w0, w1))
        op = (w0 >> 24) & 0xFF
        hist[op] = hist.get(op, 0) + 1
    digest = hashlib.sha256(words).hexdigest()
    return {
        "n_gfx": n_gfx,
        "n_seg": n_seg,
        "segment": segs,
        "mtx_modelview": mv,
        "mtx_projection": proj,
        "gfx": gfx,
        "hist": hist,
        "gfx_sha256": digest,
    }


def format_report(rec: dict) -> str:
    lines = [
        f"n_gfx={rec['n_gfx']} n_seg={rec['n_seg']}",
        f"gfx_sha256={rec['gfx_sha256']}",
    ]
    nz = [(i, v) for i, v in enumerate(rec["segment"]) if v]
    if nz:
        lines.append("segments: " + ", ".join(f"{i}=0x{v:08x}" for i, v in nz))
    else:
        lines.append("segments: (all zero)")
    lines.append("histogram:")
    for op in sorted(rec["hist"]):
        name = OPNAMES.get(op, "")
        extra = f" {name}" if name else ""
        lines.append(f"  0x{op:02x}{extra}: {rec['hist'][op]}")
    return "\n".join(lines) + "\n"


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("g0", type=Path, help="G0T1 file")
    p.add_argument("--hash", action="store_true", help="print gfx-word SHA-256 only")
    p.add_argument("--check-hash", type=Path, help="fail unless gfx SHA-256 matches this file")
    args = p.parse_args(argv)
    rec = parse_g0(args.g0.read_bytes())
    if args.hash:
        sys.stdout.write(rec["gfx_sha256"] + "\n")
    else:
        sys.stdout.write(format_report(rec))
    if args.check_hash:
        want = args.check_hash.read_text().strip().split()[0]
        if rec["gfx_sha256"] != want:
            sys.stderr.write(f"hash mismatch: got {rec['gfx_sha256']} want {want}\n")
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
