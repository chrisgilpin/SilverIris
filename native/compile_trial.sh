#!/bin/bash
# Trial-compile decomp C files. Writes ok/fail lists under build/native/.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DECOMP="$ROOT/third_party/goldeneye_src"
BUILD="$ROOT/build/native/trial"
mkdir -p "$BUILD"
CFLAGS=(-std=gnu89 -fms-extensions -fno-builtin -ffp-contract=off -fno-strict-aliasing -fno-common
  -Wno-incompatible-pointer-types -Wno-int-conversion
  -Wno-implicit-int -Wno-return-type -Wno-unused-variable
  -Wno-unused-but-set-variable -Wno-parentheses -Wno-switch
  -Wno-missing-braces -Wno-overflow -Wno-pointer-sign
  -Wno-declaration-after-statement -Wno-unused-function
  -Werror=implicit-function-declaration
  -I"$ROOT/src/port/compat" -I"$ROOT/build/overlay" -I"$DECOMP/src" -I"$DECOMP/src/game"
  -idirafter "$DECOMP/include" -idirafter "$DECOMP" -idirafter "$DECOMP/include/PR"
  -include "$ROOT/src/port/compat/ido.h"
  -DVERSION_US -DLANG_US -DREFRESH_NTSC -DBUGFIX_R0 -D_LANGUAGE_C -DPORT)

python3 "$ROOT/native/gen_overlay.py"
: >"$BUILD/ok.txt"
: >"$BUILD/fail.txt"
ok=0
fail=0
while IFS= read -r -d '' f; do
  rel="${f#"$DECOMP"/}"
  out="$BUILD/${rel}.o"
  mkdir -p "$(dirname "$out")"
  if gcc -c "${CFLAGS[@]}" "$f" -o "$out" 2>"$out.err"; then
    echo "$rel" >>"$BUILD/ok.txt"
    ok=$((ok + 1))
    rm -f "$out.err"
  else
    echo "$rel" >>"$BUILD/fail.txt"
    fail=$((fail + 1))
  fi
done < <(find "$DECOMP/src" -name '*.c' \
  ! -path '*/libultra/*' ! -path '*/libultrare/*' -print0)

echo "OK=$ok FAIL=$fail"
echo "ok list: $BUILD/ok.txt"
echo "fail list: $BUILD/fail.txt"
# show unique first error lines
echo "--- sample failures ---"
i=0
while read -r rel; do
  echo "== $rel =="
  head -6 "$BUILD/${rel}.o.err" || true
  i=$((i + 1))
  [ "$i" -ge 12 ] && break
done <"$BUILD/fail.txt"
