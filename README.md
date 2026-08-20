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

Public URL: [https://007.goodhouseinc.com](https://007.goodhouseinc.com)
(no access secret). The shell lobby, 2-4P lockstep, and coturn TURN exist.
Netplay is **opt-in** (`?ff_netplay=1`) and is **not** default-on. The live
canvas calls `port_api_draw` and blits the stage G1 framebuffer when the
user pack produced a drawable room GDL (synthetic Fast3D or inflated 1172
C0/`G_TRI4`). Otherwise it keeps the PORT mesh so a non-drawable pack does
not black-screen. A retail-shaped C0 room now also inflates `pPointTableBin`
(the vertex table), binds RSP segment 14 (`SPSEGMENT_BG_VTX`), skips
unknown opcodes / unresolved `G_DL`, and applies a player look-at (theta=0
faces -Z) so world-space `G_TRI4`s can land on the G1 FB as untextured
grey. G1 decodes Rare `G_SETTEX` (0xC0): `texture_id = w1 & 0xfff`
looks up `assets/images/split/<images.def name>.bin` (or `imageN.bin`)
in the user pack. After 0b83df6 some retail wall tiles already bind
(Facility hallway showed repeating beige brick). Floors and ceilings
in that view stayed black because later SETTEX ids did not stay bound
(last-wins TMEM at raster time), IA4/IA8/IA16 tiles were rejected, the
secondary room GDL (`pSecMappingBin`) was not walked, and large floor
tris that crossed the near plane were discarded whole. After 3550dc9
those floors sampled, but a `w>0.01` clip (no x/y frustum) projected
near-camera slivers across the whole FB as black, overwriting brick
and the gun. G1 now keeps a per-triangle tile cache, samples
IA/RGBA32, inflates the secondary GDL, and clips homogeneous
`w`/`±x`/`±y`/`±z` so a near-camera floor cannot fill the screen
and a door/portal closer than the projection near plane cannot
stamp a center-covering black rectangle over the gun.
Transparent IA (alpha 0) does not stamp black. A SETTEX miss still
paints vertex grey. G1 now walks the current room plus Rare portal
neighbors (depth 2, cap 12) so a doorway shows the next room's GDL
instead of a hole. SETTEX now binds 64x64 RGBA16/RGB15 (the 4KB TMEM
cap used to reject them as misses) and inflates Rare lookup /
Huffman / Huffman-lookup / RLE-lookup / blur methods plus RGB15.
Huffman uses Rare's tree and accepts niter up to 16KB. Facility
room GDLs are MIPMAP/TILE_PRESWAPPED only (no DETAIL second id).
HUD walked/cur report that walk; texMiss splits abs vs dec. After the
SETTEX/clip/portal walk, remaining grey in a doorway is missing
**props** (doors), not another wall format. G1 now also inflates the
stage setup (`UsetuparkZ`), walks PROPDEF_DOOR / PROP / glass, loads
`P*Z` models (G1DL magic or Rare node GDL on segment 5), and rasters
them at the pad with look-yaw through the same clip/SETTEX path.
Rooms without a setup stay bit-identical on the greyscale G1 hash.
Native tests prove a magenta door GDL at a known pad, a Rare node
tree, far-pad cull, and empty-setup hash stability. The player now
spawns at the setup intro pad (first INTROTYPE_SPAWN with demo=0;
10000+ remaps onto boundpads) so the first frame is that corridor,
not room-1 origin. Proof: player x/z/θ match the pad (0.01 / 0.1°)
and a nearby door still draws. No door
open/close, no guards, no weapons. No retail texels in git. Campaign
is not v1. Title boot is not in this build.

Design:
[`docs/SilverIris-browser-port-design.md`](docs/SilverIris-browser-port-design.md).
This box: [`docs/remote-dev.md`](docs/remote-dev.md).

## Privacy

- The ROM is chosen in *your* browser. It is never uploaded through nginx.
  Extracted assets stay in this tab (IndexedDB). We do not provide a ROM.
- WebRTC can reveal your IP to peers in the room (host / srflx candidates).
  Default ICE is `all` (direct or STUN first). This box runs coturn TURN
  with ephemeral room-scoped credentials -- **no open TURN**.
  `?ff_turnForce=1` forces the TURN relay path. `?ff_wsRelay=1` (or ICE
  fail after Start) carries `inp`/`ctl` on same-origin `/ws` instead of
  DataChannels.
- **Keep the game tab visible.** Browsers throttle hidden timers to about
  1 Hz. The shell sends STALL and overlays "tab must stay visible."
  Background-tab lockstep is unsupported.
- No analytics SDK. Debug report (`silveriris-report/1`) is clipboard JSON
  with no ROM bytes.

## QoL flags

Query `?ff_name=0|1` overrides `localStorage` `ff_name`. Do not default
netplay on.

| Flag | Default | Effect |
| --- | --- | --- |
| `ff_netplay` | off | Show the lobby and start a 2-4P lockstep mesh |
| `ff_lan` | off | Delay 1 tick (LAN) instead of 2 (Internet) |
| `ff_turnForce` | off | ICE `relay` only (TURN path; hides host IP) |
| `ff_wsRelay` | off | Force `/ws` relay of `inp`/`ctl` (also auto-on after ICE fail) |
| `ff_widescreen` | on | Hor+ camera on remote seats |
| `ff_campaign` | off | Not v1. Leave it off. |


## Layout

| Path | Purpose |
| --- | --- |
| `src/port/` | New platform layer (libultra replacement) |
| `src/glue/` | Emscripten exports |
| `src/overrides/` | Tiny, reviewed patches to decomp C |
| `third_party/goldeneye_src/` | Submodule: matching decomp, pinned in `NOTICE` |
| `web/shell/` | Browser shell (ROM gate, lobby, lockstep) |
| `native/` | Developer/CI host only — not a public desktop player |
| `tools/guard/` | No-ROM / no-asset scanner |
| `services/` | Signaling + nginx template + coturn |
| `deploy/systemd/` | `silveriris-vite` / `silveriris-signal` unit copies |

Decomp C is compiled by path from the submodule. Do not copy `src/game` into
this tree.

## Legal

Original SilverIris code is All Rights Reserved (`LICENSE`). The vendored
decomp is **unlicensed-to-us** (`NOTICE`). Requiring a user ROM does not make
redistributing a compiled engine lawful. Read [`docs/legal-posture.md`](docs/legal-posture.md).

## Web shell

ROM gate + in-tab extract + `.c0pack` in IndexedDB, then `game.wasm` `init(pack)`
(hash check, 256 MB, G1 blit when the pack's room GDL is drawable, else the
PORT mesh, placeholder AudioWorklet). Title music and gun are integer-phase
stubs, not cartridge banks. Netplay is opt-in (`?ff_netplay=1`); campaign is
not v1.

```bash
make -C native wasm                 # emcc → web/shell/public/game.{js,wasm}
cd web/shell
npm ci
npm test
npm run dev
```

`npm run dev` is localhost-only. The public instance is already live. See [`docs/remote-dev.md`](docs/remote-dev.md).

### Netplay (opt-in, `?ff_netplay=1`)

Netplay stays **off** unless the query flag is set. Host creates a room; 2-4
players join the same code, Ready, then host Start. Transport is a full-mesh
of `inp` + `ctl` DataChannels (6 channels per client at 4P). Delay is 2
ticks on Internet STUN, or 1 with `?ff_lan=1`.

Remote clients render a **full-frame Hor+** camera for their seat via
`port_set_view_seat` / `currentPlayerSetScreenSize` — not leftover split-screen
viewports. Local couch split-screen (keys 1-4 on one machine) is unchanged.

If the host disconnects, the match ends. A guest that goes silent after the
mesh (or the signal WebSocket relay) has carried input stalls for 350 ms,
then the match ends after 10 s (v1 does not drop to 3 mid-sim). If WebRTC
never opens, the overlay says the peers could not connect — not that someone
left. This box runs coturn (STUN/TURN on 3478) with ephemeral room-scoped creds — no open TURN. Same-origin `/ws` relays `inp`/`ctl` after Start when ICE fails so Chrome+Safari or
two tabs on one Mac still lockstep when ICE is blocked (Safari private,
no TURN). Two Chrome tabs is the reliable same-machine test; Safari private
often shows an Apple privacy banner and gathers only `.local` host
candidates that Chrome will not resolve.

**Keep the game tab visible.** Browsers throttle hidden timers to about 1 Hz.
The shell sends STALL and overlays "tab must stay visible." A silent
AudioWorklet and navigator.wakeLock reduce throttle when they work; there
is no good fix. Background-tab lockstep is unsupported.

How to try 3-4P: open four tabs at
`https://007.goodhouseinc.com/?ff_netplay=1` (or add `&ff_lan=1` on a LAN).
Each tab loads the same NTSC-U ROM. Host Create room, others Join the code,
Ready, host Start. Each tab is one seat with its own full-frame view.

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
