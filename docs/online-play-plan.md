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

Walk speed stays the pinned analog (~3 units/tick, dt=3). That is hardware-comparable. Hold-Shift (lockstep `PORT_RUN` / CONT_R) is ~1.9× analog — not a default sprint and not a client-only multiplier.

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

## M7 — MP respawn

Dead seats stayed dead (hp 0, no move). That is not a match.

**Does:** after 20 ticks (1 s) a rising Z respawns; at 40 ticks (2 s) auto-respawn. Seat returns to its intro / on-tile pad with full HP, 0 armour, PP7 mag/reserve. Other seats, score, and chrs stay. Match-over freezes respawn.

**Exit:** kill P1, wait 40 ticks, P1 hp is 8 and on-tile. Checksum includes `dead_ticks`.

## M8 — Match length + scoreboard

`MatchConfig.gameLength` was packed and ignored. HUD showed a single kill counter.

**Does:** GAMELENGTH 0–6 (unlimited / 5–20 min / 5–10–20 pts). Clock ticks at 20 Hz. Point limit ends the match on the killing shot. HUD shows per-seat kills and remaining time. Overlay MATCH OVER.

**Exit:** 5-pt configure + 5 kills → over; 10-min default stays under 12000 ticks.

## M9 — Facility / Complex select

Lobby cfg hard-coded stage 34. Complex (31) is already dump-verified in the pack.

**Does:** host picks Facility or Complex; `MatchConfig.stage` is packed; every peer `loadStage`s that id before lockstep. Solo can switch from the same control. Failed load keeps the current stage.

**Exit:** packed cfg with stage 31 round-trips; `loadStage(31)` is called from Start.

Lobby `buildId` `siliris-respawn-v3!!`.

---

## Remaining until this project is “done”

**v1 ship bar is already live** (M6 / P8): ROM gate + Facility/Complex 2P lockstep + local split-screen on https://007.goodhouseinc.com.

What is left is **playability** of that match, then items that are not v1.

### Playability (this project)

| # | Status | What | Blocker |
| --- | --- | --- | --- |
| M14 | this push | Click-to-fire when pointer-locked; readable center sight | none |
| M15 | this push | PP7 viewmodel recedes (hold Z −110). Rare pos is −33.5; G1 near=10 filled the FB at −60 | none |
| M16 | off by default | Overlay hittable 30u guard cylinder — live canvas off; `?ff_debug=1` restores it | none |
| M17 | this push | Step clip: unlinked-edge skin + `stan_ray_block` so the centre does not walk through a tile-exit or closed door. Interior G1 walls inside a tile still clip | none |
| M18 | this push | Guard GROUP uses RST1 only — decoded 16-joint Euler exploded the mesh. Fit clamps exploded AABBs. Lime SETTEX miss remains | none |
| M19 | Chris | Private Facility 2P tape, minutes, 0 DESYNC native↔wasm | local pack, not git |
| M20 | Chris | Two-box live netplay look | you |

### Not v1 — follow-on, not a gate for “public 2P”

- Campaign (Dam Agent, PR-11f / P9)
- Full matching engine in wasm (legal + huge)
- Full `chrai` combat AI
- Aim-pose bind (`skip=pose` until a ROM still of standing aim)
- 60 Hz lockstep, GGPO, JP/EU WASMs, WebGPU
- Invented default sprint (walk stays ~3 u/tick; hold-Shift is the 1.9× lockstep run)

### Blocked on Chris (do not fake)

- Aim decode `have≠0`: 16-joint Euler explodes the mesh
- G1 walls ≠ stan tiles (true visual clip): needs a Facility pack look after M17
- KF7 near-white after collect (header radius 941 vs PP7 294)
- Two-box live netplay

Lobby `buildId` after this push: `siliris-run-v8!!!!!!`.

---

## M10+ (after M9)

Smallest first. Each exit is a native harness — no Chrome, no Chris.
Default walk stays the pinned analog (~3 u/tick). Hold-Shift is the 1.9× lockstep run. Campaign is out of v1.

### M10 — KF7 fire/mag is rifle, not leftover PP7 9mm (this push)

Collecting chrkalash 184 equips KF7: `ak47_stats` `AMMO_RIFLE`, MagSize 30.
Fire spends that mag. Reload takes from `ammo[RIFLE]`. Pickup +7 goes to
rifle, not 9mm. Leftover PP7 mag unloads back to 9mm (not converted).
Spawn/respawn still PP7 7/21 9mm. Hitscan damage stays 1.

Lobby `buildId` `siliris-kf7-mag-v4!!` (M12 bumps this).

**Exit:** `gun-test` collect-184 then fire: weapon=KF7, mag moves on rifle,
9mm reserve unchanged. Empty-PP7 path unchanged.

### M11 — KF7 viewmodel hold is Rare `ak47_stats` Pos (this push)

Gak47Z uses Rare (11, −19, −16), not the PP7 hold (11, −24, −60). PP7 hold
unchanged so the G1 greyscale hash stays. Dump `BoundingVolumeRadius` is
941.9339 (ak47 header) vs PP7 293.60767 — that is the file, not a drawn AABB.

**Exit:** `gun-test` KF7 hold xyz is 11/−19/−16; PP7 still 11/−24/−60.

### M12 — MP5K death-drop is a hold (this push)

chrmp5k 189 binds Gmp5kZ (`mp5k_stats` `AMMO_9MM`, MagSize 30, Pos 11/−26.4/−35).
Collecting switches hold + mag size. +7 is still the dump amount, into the
MP5K mag, not leftover PP7 mag 7.

Lobby `buildId` `siliris-mp5k-v5!!!!!`.

**Exit:** `gun-test` collect-189: weapon=MP5K, mag size 30, ammo type 9mm.

### M13 — Every death-drop is tracked (this push)

`crc_props` hashes all on-floor assigned drops, not `g_drop_prop` last-wins.
Two corpses both stay collectable. n≤1 keeps the last-wins checksum bytes.

**Exit:** two drops in `gun-test`; hide one; crc changes; the other remains.

### Blocked on Chris (do not start here)

- Aim decode `have≠0`: header already decodes; 16-joint Euler explodes the
  mesh (`skip=pose`). Needs ROM visual of a standing aim. Do not fake an arm.
- G1 walls ≠ stan tiles: visual clip-through. Needs a Facility pack look.
- Combat AI beyond chase/LOS: full GE is `chrai`. Needs ROM AI lists.
- Full matching engine in wasm: legal + huge. See `docs/legal-posture.md`.
- KF7 near-white after collect: if Rare pos still fills the FB, needs a ROM
  look (G1 near=10 vs header radius 941).
- Two-box live netplay look.

---

## Still true after M9

- G1 walls ≠ stan tiles (some visual clip-through remains).
- Combat AI is chase / LOS, not full GE.
- Full matching engine is not in wasm (PORT + G1 slices).
- Hosting `game.wasm` is still a compiled derivative. See `docs/legal-posture.md`.
- Default walk stays the pinned analog (~3 units/tick). Hold-Shift is 1.9× analog (`PORT_RUN` on the lockstep pad).

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

---

## STATUS (2026-08-30)

Playtest Facility on https://007.goodhouseinc.com (hard-refresh). Spawn stays closed stall room 71. Netplay on. Lobby `siliris-run-v9!!!!!!` (walk/run pad from `a81d7db`; this session did not change the lockstep pad).

**Shipped on `origin/main` (this session)**

| Item | SHA | What |
| --- | --- | --- |
| Walk / gun / wall skin | `a81d7db` | Keep. Rare 0.1 viewgun scale, authentic PP7/KF7 PosXYZ, `PORT_WALK_MUL` 4.5, collision skin 30. Not reverted. |
| Standing bodies | `15def0a` | `emit_parts` bakes pad yaw into the part matrix (`T * R_yaw * R_pose`). Old G1 order `T * R_pose * R_yaw` smeared idle/walk limbs into wall blobs while GROUP AABB still reported `fit=0.123 h=1510 rest=skel`. Doors / identity G1DL unchanged. Facility harness `idle_look` from ~220u at the extra idle (`-420,-2480`). Spawn first frame still stall. Aim stays `skip=pose`. |
| Draw-only wall slack | `4d2bcf8` | `port_stan_visual_xz` pulls the G1 camera off unlinked edges to 46u. `clip_step` / `PORT_WALL_SKIN` stay 30. Walk 4.5× unchanged. |

G1 greyscale `643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477` (unchanged).

**Remaining holes**

- Heads often missing: body `have_head` (opcode 23) may not fire, so `head_off` stays 0 and Chead*Z sits at the feet. Neck attach still dump-verified, not invented.
- Aim decode `have≠0`: 16-joint Euler still explodes (`skip=pose`). Needs a ROM still of standing aim. Do not fake an arm.
- G1 walls still sit inside some stan tiles; visual slack 46 reduces smear, does not match meshes to tiles.
- Chr SETTEX can stay grey/flat vs brick walls.
- KF7 near-white after collect (header radius 941 vs PP7 294) — hold Z is Rare `-16` after 0.1 scale.
- Combat AI is chase / LOS, not full `chrai`.
- Two-box live netplay look. M19 private Facility tape (Chris).

Walk is `PORT_WALK_MUL` 4.5 × analog×dt (~13.5 u/tick). Hold-Shift is 1.9× that. Campaign is out of v1.
