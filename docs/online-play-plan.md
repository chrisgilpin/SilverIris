# SilverIris — milestone plan to public 2P

**Goal:** anyone opens https://007.goodhouseinc.com, supplies a legally obtained NTSC-U ROM, and plays Facility/Complex **2P delay lockstep** (create/join by room code) plus local split-screen.

Campaign is out of v1. Product name is SilverIris. Netplay stays **opt-in** (`?ff_netplay=1`) until M5 is green, then default on (M6).

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

## M3 — On-tile 2P spawn

`k_spawn_*` offsets from the intro pad can put P2 in a wall. Bind dump-verified Facility MP pads, or the next on-tile offset. No invented pads.

**Exit:** both seats spawn on stan tiles in Facility; neither starts inside a slab.

## M4 — Checksum the rest of the G1 sim

`crc_chrs` is still the patrol dummy. Include setup-guard xz / alert / dead, pad-215 armour, and KF7 death-drop in `crc_props` (or an extended chr CRC).

**Exit:** a divergent guard or pickup changes `ck` and halts DESYNC.

## M5 — Green 2P Facility tape

Private pack tape: minutes of walk / door / shoot, 0 DESYNC native↔wasm. That is the P5 ship bar. Do not assume it exists until it does.

**Exit:** recorded tape replays; two browser tabs at `?ff_netplay=1` stay in sync for that duration.

## M6 — Public 2P (P8)

`ff_netplay` default **on** at the public URL. PR-19 polish: modern remap UI, privacy copy. No server browser. No campaign.

**Exit:** README + ROM-gate copy + privacy note; two strangers can create/join by room code without a query flag.

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
- Do not default netplay on before M5.

## Local loop

```
make -C native lockstep-test gun-test score-test
cd web/shell && npm test
make -C native wasm   # emcc → web/shell/public/game.{js,wasm}
```
