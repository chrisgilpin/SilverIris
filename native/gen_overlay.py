#!/usr/bin/env python3
"""Rewrite bondtypes.h so ItemModelFileRecord is complete before chrobjdata.h."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
src = root / "third_party/goldeneye_src/src/bondtypes.h"
dst = root / "build/overlay/bondtypes.h"
text = src.read_text()
needle = """#ifndef AIPARSE
    #include "game/chrobjdata.h"
#endif
"""
if needle not in text:
    raise SystemExit("bondtypes.h include block not found")
text = text.replace(needle, "/* chrobjdata.h moved below ItemModelFileRecord (PORT/GCC) */\n", 1)
text = text.replace("#define inherits struct", "#define inherits /* PORT: empty; use -fms-extensions */", 1)
anchor = """        } ChrModelFileRecord;
"""
if anchor not in text:
    raise SystemExit("ChrModelFileRecord typedef not found")
text = text.replace(
    anchor,
    anchor
    + """
#ifndef AIPARSE
    #include \"game/chrobjdata.h\"
#endif
""",
    1,
)
dst.parent.mkdir(parents=True, exist_ok=True)
dst.write_text(text)
print(f"wrote {dst}")
