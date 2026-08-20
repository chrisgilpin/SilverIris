# SilverIris

A browser-native port of GoldenEye 007 (Nintendo 64) that runs in a tab after
you supply a legally obtained NTSC-U ROM. Internet and LAN multiplayer are
new; the original game is local split-screen only.

**This is not an official Nintendo, Rare, Danjaq, Eon, or MGM product.**
SilverIris is a fan project. We do not provide a ROM, assets, or a link to
either. Dump your own cartridge.

## You must provide a ROM

Gameplay requires a matching NTSC-U dump:

| Region | SHA-1 |
| --- | --- |
| NTSC-U | `abe01e4aeb033b6c0836819f549c791b26cfde83` |

Unknown dumps are rejected. JP and EU matching hashes exist in the decomp; v1
does not accept them.

Never commit a ROM or extracted assets to this repository. CI fails the build
if they appear (`tools/guard`).

## Status

Web shell: ROM gate + in-tab extract + `.c0pack` + `game.wasm` `init(pack)`
(G1 picture, placeholder audio, Facility load, analog-walk slice at
`g_ClockTimer=3`). Native ge_sim through pack DMA. Full MoveBond still
needs `bondview2.c`.
Design:
[`docs/SilverIris-browser-port-design.md`](docs/SilverIris-browser-port-design.md).

## Layout

| Path | Purpose |
| --- | --- |
| `src/port/` | New platform layer (libultra replacement) |
| `src/glue/` | Emscripten exports |
| `src/overrides/` | Tiny, reviewed patches to decomp C |
| `third_party/goldeneye_src/` | Submodule: matching decomp, pinned in `NOTICE` |
| `web/shell/` | Browser UI (later) |
| `native/` | Developer/CI host only — not a public desktop player |
| `tools/guard/` | No-ROM / no-asset scanner |
| `services/` | Signaling + Caddy + coturn (later) |

Decomp C is compiled by path from the submodule. Do not copy `src/game` into
this tree.

## Legal

Original SilverIris code is All Rights Reserved (`LICENSE`). The vendored
decomp is **unlicensed-to-us** (`NOTICE`). Requiring a user ROM does not make
redistributing a compiled engine lawful. Read [`docs/legal-posture.md`](docs/legal-posture.md).

## Web shell

ROM gate + in-tab extract + `.c0pack` in IndexedDB, then `game.wasm` `init(pack)`
(hash check, 256 MB, G1 blit, placeholder AudioWorklet). `netplay` / `campaign`
stay off. Title music and gun are integer-phase stubs, not cartridge banks.

```bash
make -C native wasm                 # emcc → web/shell/public/game.{js,wasm}
cd web/shell
npm ci
npm test
npm run dev
```

`npm run dev` is localhost-only. To try it from another machine, point DNS at
this Hetzner box and put nginx in front — see [`docs/remote-dev.md`](docs/remote-dev.md)
(`007.goodhouseinc.com`).

Drop or pick an NTSC-U dump. Unknown files and JP/EU matching hashes are
rejected. The file stays in the tab. After verify, assets are extracted
here, stored as a `.c0pack`, and the engine draws the G1 picture. Title
boot is not in this build.

## Native ge_sim (developer / CI only)

```bash
make -C native
./build/native/port_stub
make -C native rng-test
make -C native tick-test
make -C native joy-playback
make -C native rom-dma
make -C native product-no-rom-dma
make -C native g0-test
make -C native g1-test
make -C native pack-dma
make -C native port-api
make -C native audio-test
make -C native stage-test
make -C native wasm
./build/native/silveriris          # 20 Hz SDL window; no --rom
./build/native/silveriris --headless --ticks=8
./build/native/silveriris --pack testdata/pack/synthetic.c0pack --headless
```

`--rom` is rejected on `silveriris`. Product DMA is `--pack`. The K18 exception
is a separate binary:

```bash
make -C native silveriris_bringup
./build/native/silveriris_bringup --rom /path/to/your/ge007.u.z64 --headless
```

That fopen is compiled in only with `PORT_BRINGUP_ROM_DMA`. Do not commit a
dump. Host gcc compiles a subset of decomp C. Full title boot is not there yet.
See [`native/README.md`](native/README.md).

## Guard

```bash
python3 tools/guard/no_assets.py
python3 tools/guard/no_assets.py --self-test
```

## Pin bumps

Changing `third_party/goldeneye_src` is a dedicated PR: update `.gitmodules`
and `NOTICE`, re-extract a private US ROM, re-run RNG vectors, replay a tape.
No drive-by pin bumps.
