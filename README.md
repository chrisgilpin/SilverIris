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
faces -Z; phi pitch ±70° via mouse-look or I/K) so world-space `G_TRI4`s can land on the G1 FB as untextured
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
and the gun. G1 now keeps a per-triangle tile cache (a SETTEX in the same display list must not evict a slot already recorded on a tri — that punched Facility walls to the cleared FB after setup guards/doors added more unique ids), samples
IA/RGBA32, inflates the secondary GDL, and clips homogeneous
`w`/`±x`/`±y`/`±z` so a near-camera floor cannot fill the screen
and a door/portal closer than the projection near plane cannot
stamp a center-covering black rectangle over the gun.
Transparent IA (alpha 0) does not stamp black. A SETTEX miss still
paints vertex grey. Sampled texels are SHADE*TEXEL (Rare Vtx.cn already in the GDL); cn=0 skips modulate so the G1 greyscale hash and SETTEX checkers stay bit-identical (g1_set_shade_modulate(0) is the explicit no-light path). G1 now walks the current room plus Rare portal
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
them at the pad with look-yaw through the same clip/SETTEX path. Facility start has no PROPDEF_DOOR (pads sit ~9000u away in the gas-plant cluster); door-sized portals and the start alcove get the retail Pgas_plant_met1_do1 96-vert mesh (SETTEX 685-688,706) when the pack has that P*Z — G1 binds the node vertex bank (file+0xC0) as seg 4 so G_VTX 0x04 resolves, leaves seg 3 unbound (retail G_MTX would LOAD over the camera), and replaces the gas-plant GROUP origin with the fitted portal pose. Missing / non-seg4 models keep the fitted G1DL metal quad (SETTEX 685). Walked-path lab portals r71-r7 / r7-r8 / r8-r20 / r20-r19 / r19-r18 / r3-r18 / r19-r21 / r8-r5 / r8-r10 / r1-r3 are door-sized Rare quads with no setup pad; Z/Space within 200 facing unlatches the fitted slab (collision off, swing 90). Catwalk r13-r15, stacked r8-r6/r8-r9/r6-r71, spawn-side r11-r71, r6 island, and gas-plant GROUP origins stay unbound.
Rooms without a setup stay bit-identical on the greyscale G1 hash.
Native tests prove a magenta door GDL at a known pad, a Rare node
tree, far-pad cull, and empty-setup hash stability. The player now
spawns at the setup intro pad (first INTROTYPE_SPAWN with demo=0;
10000+ remaps onto boundpads) so the first frame is that corridor,
not room-1 origin. Proof: player x/z/θ match the pad (0.01 / 0.1°)
and a nearby door still draws. G1 look-at Y is Rare eye height (185*perspective-10 = 175) above the stan floor tile under the player (Facility pack file `Tbg_ark_all_p_stanZ.bin`, inflated 1172). WASD dest must stay inside a stan tile and must not cross a closed PROPDEF_DOOR pad slab, so a corridor wall or a shut door stops the step. Empty synthetic stan keeps pad Y so G1 greyscale / intro tests stay bit-identical. Z/Space (Z_TRIG) facing a closed door within 200 units unlatches it (collision off; the closed GDL stays and is swung 90° around the pad + look-tangent hinge, away from the player, or slid along that tangent if setup doorType is SLIDING). Facility start doors are SWINGING / maxFrac=90. A second use closes. The same Z press does not fire the PP7; Z with no facing door in range still shoots. PP7 hitscan is a ray from the eye along look (including pitch): first hit among a closed door slab, an exterior stan-tile edge (leave walkable), or a standing-guard / patrol cylinder (radius 30, height 185 at the pad, or the walking test-mover's current xz; idle pads stay put). Door slabs are 250 tall; a high/low shot can miss a standing cylinder or door. Tile-exit walls stay full-height. Pack GwppkZ draws as a camera-space PP7 viewmodel (hold +11, −24, −60 after 180° Y; Rare wppk_stats Pos is 11/−20.8/−33.5 — G1 near=10 so Z stays farther). It pitches with look φ. Two SKEL_FLASH cards stay hidden except for three ticks after a spent shot. The overlay PP7 trapezoid is only the no-pack placeholder. Open doors do not block. The fake PORT wall at z=-50 is only the no_assets / empty-synthetic fallback — a corridor shot no longer always reports z=-50. A hit increments the HUD hits counter. A setup-guard hit hides the standing body (skip G1 draw; no ragdoll), stops blocking later rays, and increments HUD kills once. Patrol still kill+score. No combat AI, blood, or damage model. Instant pose, no Rare door-anim player, no lock puzzles, no volume radius. Setup PROPDEF_GUARD now loads C*Z body (and a HeadID>=42 C*Z if the pack has it) at the pad through the same G1/SETTEX path, SKELETON(guard) rest-pose joint matrices (ANIM_idle frame 0 from the pack, or RST1 on a GROUP; identity if missing), 4000-unit room filter. One Facility test mover (next-closest setup guard after the start-corner idle) loops pack ANIM_walking (37 frames, 12-bit) while ping-ponging a 2–4 tile open-floor strip in room 71 (north of the hallway turn, not the stall cubicle, out of the spawn 270 cone). Y is the stan floor; stand is 185u; the figure faces the step. No AI, return fire, chase, or door use. A blocked or NaN step stops rather than clipping a G1 wall. Fill emits that mover first, then remaining props nearest-first. PORT_PROP_MAX_DRAW is 512 so idle guards in the next walked rooms are not starved by the prop-pass cap. The in-tab extractor copies `assets/animationtable_entries.bin` and `assets/animationtable_data.bin` from NTSC-U ROM DMA (entries 1198784/1482432, data 2681216/59360; skip if missing). An IndexedDB pack built before this must be rebuilt — re-drop the ROM. Missing body is skipped — no capsule. No AI, pathing, combat, or weapons-as-pickups. No retail texels in git. Campaign
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
