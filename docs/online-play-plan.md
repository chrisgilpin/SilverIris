# SilverIris — milestone plan to public 2P

**Goal:** anyone opens https://007.goodhouseinc.com, supplies a legally obtained NTSC-U ROM, and plays Facility/Complex **2P delay lockstep** (create/join by room code) plus local split-screen.

Campaign is out of v1. Product name is SilverIris. Netplay is **on** at the public URL (`?ff_netplay=0` for solo).

Long-form design: `docs/SilverIris-browser-port-design.md`.

**Deploy rule:** every completed milestone is committed, pushed to GitHub `main`, pulled on the Hetzner box (`/home/grok/GoldenEye`), `make -C native wasm`, then `systemctl restart silveriris-vite silveriris-signal`. Do not touch other nginx vhosts.

---

## M0 — Foundation (done, on `main`)

ROM gate, extractor, c0pack, 20 Hz tick, wasm shell, audio, Facility/Complex G1 walk, guns/guards/score, local split-screen, TAPE1, signal lobby, WebRTC mesh, 2P lockstep transport (delay 1 LAN / 2 Internet, 350 ms stall, 10 s drop, checksum DESYNC), un-split Hor+ camera, 4P seats, coturn + wsRelay.

Live: https://007.goodhouseinc.com

## M1 — Lockstep can carry a match (done this push)

What was blocking a real 2P game, not the mesh:

- Mouse look on the lockstep pad (24-byte `InputBlock`, 0.1° int8).
- PvP hitscan (1 HP / shot, 8 HP). Score credits the shooting seat.
- Door open/frac in `crc_props`, compared on `ck`.
- Lobby `buildId` `siliris-inp-look-v1!` so mixed shells cannot join.
- Pointer-lock: A/D strafe, mouse turns.
- Guards: front cone + 0.5 s notice; HUD flash + UNDER FIRE.
- Walk path refuses the stall cubicle at −220, −2640.

Netplay still `?ff_netplay=1`. Hard-refresh after deploy (new wasm + `buildId`).

## M2 — Synthetic 2P harness (done this push)

Public-CI tape, no ROM: two seats on an on-tile corridor, walk, Z-unlatch a door, one PvP shot. Replay bit-identical. `make -C native 2p-corridor-test` writes `testdata/tapes/2p-corridor.tape`. Look stays 0 on disk pads.

Walk speed stays the pinned analog (~3 units/tick, dt=3). That is hardware-comparable, not a sprint. Facility is thousands of units across; stan clip vs G1 walls can make it feel slower. Do not invent a run multiplier here.

## M3 — On-tile 2P spawn (done this push)

`begin_match` re-applies the stage intro origin (solo pose was wiped before). Extra seats use dump intro pads when the setup lists more than one `INTROTYPE_SPAWN` (demo=0). Otherwise `k_spawn` if that xz is on-tile and not in a door slab; else the next walkable offset along look. No invented pads.

**Exit:** both seats on stan tiles; neither in a slab. Empty-stan tests keep P1 at (40, 20).

## M4 — Checksum the rest of the G1 sim (done this push)

`crc_props` now hashes doors, stan-guard xz/hit, setup-guard xz/alert/dead, pad-215 armour (pad/kind/hidden/xyz), and the KF7 death-drop (model/hidden/xyz). Tape replay compares `crc_props`. A divergent guard or pickup changes `ck`.

Lobby `buildId` `siliris-ck-props-v2!` so mixed shells cannot join.

## M5 — Green 2P tape (done this push)

Public CI: 2P corridor tape (walk, door, guard kill, PvP), plus synth TAPE1 replay. `make -C native replay-pack` is the private hook: `./build/native/replay_pack --pack <local.c0pack> --stage facility private.tape`. Never commit a pack or ROM. A minutes-long Facility recording is local-only until someone records one.

## M6 — Public 2P (P8) (done this push)

`ff_netplay` default **on** at the public URL. Remap UI in the shell. Privacy copy on the lobby + README. No server browser. No campaign.

**Exit:** two strangers can create/join by room code without a query flag. `?ff_netplay=0` is solo.

---

## Still true after M6

- G1 walls ≠ stan tiles (some visual clip-through remains).
- Combat AI is chase / LOS, not full GE.
- Full matching engine is not in wasm (PORT + G1 slices).
- Hosting `game.wasm` is still a compiled derivative. See `docs/legal-posture.md`.

## Hard rules (every milestone)

- Never commit a ROM, extracted assets, or ROM links. `tools/guard`.
- NTSC-U SHA-1 `abe01e4aeb033b6c0836819f549c791b26cfde83`.
- Dump-verified Facility data only. No invented pads, ammo crates, or lock/key.
- Port matching C through `src/port`. Do not emulate the N64 or rewrite it as a dedicated server.
- Do not default netplay on before M5 (M6 is that default).

## Local loop

```
make -C native lockstep-test gun-test score-test 2p-corridor-test replay-pack
cd web/shell && npm test
make -C native wasm   # emcc → web/shell/public/game.{js,wasm}
```
