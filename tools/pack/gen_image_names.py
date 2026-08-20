#!/usr/bin/env python3
"""Emit a names-only C table from images.def. No texels, no ROM bytes."""
from __future__ import annotations

import sys
from pathlib import Path


def parse_names(text: str) -> list[str]:
    names: list[str] = []
    for line in text.splitlines():
        t = line.strip()
        if not t.startswith("IMAGE(") or not t.endswith(")"):
            continue
        name = t[6:-1].split(",")[0].strip()
        if name:
            names.append(name)
    return names


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: gen_image_names.py images.def out.c", file=sys.stderr)
        return 2
    src, dst = Path(sys.argv[1]), Path(sys.argv[2])
    names = parse_names(src.read_text())
    dst.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "/* Generated from images.def — names only, no texels. */",
        "#include <stddef.h>",
        "",
        "static const char *const g_image_names[] = {",
    ]
    for n in names:
        if any(c in n for c in '"\\'):
            raise SystemExit(f"unsafe image name {n!r}")
        lines.append(f'    "{n}",')
    lines.append("};")
    lines.append("")
    lines.append(f"unsigned g1_image_bank_count(void) {{ return {len(names)}u; }}")
    lines.append("const char *g1_image_bank_name(unsigned id)")
    lines.append("{")
    lines.append(f"    if (id >= {len(names)}u)")
    lines.append("        return 0;")
    lines.append("    return g_image_names[id];")
    lines.append("}")
    lines.append("")
    dst.write_text("\n".join(lines))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
