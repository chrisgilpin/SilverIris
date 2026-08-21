#!/bin/sh
# Dump Facility spawn / hallway / stairs PNG + HUD from the gitignored pack.
# Mihok: run this after a slice. Do not commit .local/.
set -e
ROOT=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
make -C "$ROOT/tools/shots"
mkdir -p "$ROOT/.local/shots"
exec "$ROOT/build/native/shot" \
  --pack "$ROOT/.local/pack/ge.u.c0pack" \
  --out "$ROOT/.local/shots"
