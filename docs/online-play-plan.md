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

---

## STATUS (2026-08-31 playtest)

Chris poses on live `95fc845`: corner `117.6,-2447` θ244; stairs `-530.7,-2580.3` y=86.8 θ80; wall `-687,-2713.9` θ271. Spawn stall room 71. Netplay on. Lobby still `siliris-run-v9!!!!!!` (no lockstep pad change).

**Shipped this slice (`04b3db7`)**

| Item | What |
| --- | --- |
| Start-stair climb | `enter_rise_tile`: dest xz that **enters** a higher overlapping tile (r12 landing 2391 over r71 152, +319 avgY, centroid <300u) hops onto that landing. Origin already inside the high polygon (bathroom `-491.9,-2238.5`) stays low. Rise cap 400u avgY so r13 catwalk (+650) is not a hop. `clip_step` / `PORT_WALL_SKIN` 30, walk 4.5×, visual xz 46 kept. |
| Wall “cannot get closer” | `4d2bcf8` visual xz is **not** the cause at this pose (`visual_d=0`, `ray_t=29.5`). Collision already sits on Rare skin 30 from the tile-exit. Camera = body. Did not shrink radius 30. |
| Corner void | visual xz pulls 8.8u off the unlinked edge; G1 wall still sits inside the stan tile. Remaining G1≠stan clip. Did not raise `PORT_DRAW_SKIN` (that would widen camera/body gap at stan edges). |

Harness: `shot --playtest` — stairs_end eye=405.9 room=12; bathroom clip_y=86.8; spawn first frame stall. Native `player-test` `lockstep-test` `2p-corridor-test` `g1-test` green. Greyscale `643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- G1 walls still inside some stan tiles (corner black void).
- Chr SETTEX grey. KF7 near-white after collect.
- Aim `skip=pose`. Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-08-31 HEADS `ad755e6`)

Jim/Sally attach on `hasHead==0` guards (dump `chrModelFileRecord` + `random_male_heads[0]=HEAD_Male_Jim`). Neck is opcode 23 / `Switches[4]`. Body `fit_scale` copied onto Chead*Z so the placeholder is not unscaled at the feet. Synthetic HeadID=0 does not attach a body as a head.

Harness: Facility `idle_look` `have=1 off=5.8,520.1,-36.7` — standing body with a head on the neck. Spawn first frame closed stall room 71. Aim `skip=pose`.

**Remaining holes**

- G1 walls still inside some stan tiles (corner black void).
- Chr SETTEX grey vs brick.
- KF7 near-white after collect.
- Aim `skip=pose`. Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-08-31 SETTEX + KF7 `624ee87`)

SETTEX bind: pack files are `assets/images/split/<TextureID>.bin` (and images.def names). `g1_tex_settex` now tries the numeric id path after the name; F3D `G_NOOP` (`0xC0000000/0`) is not COPYICON. Oliveguard SETTEX 1916 decodes CI8 5551 mean rgb 87,109,51 (camo), texOk=1306/1306. Remaining flat olive is SHADE on greyscale Vtx.cn, not a missing palette — no invented TLUT.

KF7 viewmodel: `MODELFILEHEADER` radius 941.9339 vs PP7 293.60767. G1 near=10 cannot host 0.1×941 at Rare hold Z −16. Mesh scale is `0.1 * (294/941)` for Gak47Z only. Hold XYZ stays Rare `11/−19/−16`. PP7 stays 0.1 at `11/−20.8/−33.5`.

Spawn first frame stall room 71. Aim `skip=pose`. Native player/gun/lockstep/2p-corridor/g1 green. Greyscale `643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- Chr camo still SHADE-flattened vs brick (bind hits; no invented palette).
- Aim `skip=pose`. Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-08-31 G1≠stan `e21097d`)

Rare `bgroomtrans` scales room verts and `room.pos` by `room_data_float2 = 1/levelscale` (Facility 1/1.20648). Stan s16 already used `inv`. Port G1 was unscaled `vtx+(pos-r1)`, so walls sat inside tiles: playtest-corner camera 37u past the G1 wall while stan tile 162's unlinked edge is z=-358.9 — dump-equal to G1 rareAABB z max.

Retail C0 (78 rooms, >100 tiles): pads `*= inv` (Rare prop.c), origin `r1*inv`, G1 `scale=inv` / `ox=pos*inv-r1*inv`. Synthetic 1-room C0 / G1DL keep inv=1 (greyscale). Pad 167 *inv sits on r13; spawn snaps to the low hall (eye 29.1 local = floor+175 in scaled space). `PORT_WALL_SKIN` 30, `PORT_DRAW_SKIN` 46, walk 4.5× kept.

Harness: spawn `rooms=21/71` fb=76800 closed stall; corner same stan tile, ceiling/walls fill the frame (no black void). Stairs still hop to r12. Bathroom stays low. Aim `skip=pose`. Native player/gun/lockstep/2p-corridor/g1 green. Greyscale `643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- Chr camo still SHADE-flattened vs brick (bind hits; no invented palette).
- Aim `skip=pose`. Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-08-31 hall hop `124b354`)

Chris live `00430a0`: hall A `-233.7,-2312.1` y=29.1 r71 launched to B `-246.6,-2347.8` y=409.6 r12. Dump: r71 147 overlaps r12 2378 (+380 avgY, eye 409.6) and 2393 (+319). `enter_rise_tile` hopped because dest entered the high polygon (same path as the start-stair). Rare does not hop in that hall.

Cap `PORT_RISE_MAX` 350 (foot 2391 is +319; hall 2378 is +380). Skip rise when dest is still inside the low from-tile (147 overlapping 2393). Stair still leaves 152 onto 2391 (eye 348.2). Did not revert `e21097d` inv, walk 4.5×, or skin 30.

Harness: spawn stall r71 y=29.1 c0=1 vtx=1 walked=21; WASD around spawn stays y=29.1; A→B clip stays r71; 40 steps along A look stay y=29.1; stairs from converted foot still hop r12. Bathroom low. G1 never walked=400+/vtx=0 on that path. Aim `skip=pose`. Native player/gun/lockstep/2p-corridor/g1 green. Greyscale `643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

Shots (not committed): `.local/shots/play_spawn.png`, `play_hall_a.png`, `play_hall_walk.png`, `play_stairs.png`, `play_stairs_end.png`.

**Remaining holes**

- Chr camo still SHADE-flattened vs brick (bind hits; no invented palette).
- Aim `skip=pose`. Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-08-31 stair landing `46c48d9`)

`play_stairs_end` after the 2391 overlap hop was 88% black (mean luma 19): dest sat 1.5u from 2391's unlinked wall (world x=-717), camera inside G1. Dump: 2391 has two wall edges; Rare 2390 same-floor link is 2367 (all edges linked, landing corridor). After `enter_rise`, pick a same-room same-floor tile off unlinked edges and snap to its centroid. Hall no-hop (`PORT_RISE_MAX` 350 + from-tile skip) kept. `e21097d` inv / walk 4.5× / skin 30 kept.

Harness: spawn/WASD/A→B stay y=29.1 r71 c0=1; stairs still climb; `play_stairs_end` mean luma 60 (dark 5244, was 52821) — tiled landing, door, ceiling, not a wall interior. Aim `skip=pose`. Native player/gun/lockstep/2p-corridor/g1 green. Greyscale `643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- Chr camo still SHADE-flattened vs brick (bind hits; no invented palette).
- Aim `skip=pose`. Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-08-31 landing void `ca673cf`)

`play_stairs_end` after 2364 snap still had a black left slab + clipped guard limb. Dump: 2364 is the stair-well lip (south edge z=-452); 1e30 unlinked-edge ties picked it over 2367. Looking −Z was the well. Rare 2390 same-floor link is 2367. Score now skips tiles closer to the ground from-tile than the rise tile, so 2367 wins. `select_rooms` depth 5 when current is r12 so r71/r11 walk (portal r6-r71 at d4). Hall no-hop / `PORT_RISE_MAX` 350 / `e21097d` inv kept.

Harness: spawn/WASD/A→B y=29.1 r71; stairs climb; `play_stairs_end` mean 65 dark16=4116 (5.4%, was a third-frame void). Left side is a real wall, not a black rectangle. Aim `skip=pose`. Native player/gun/lockstep/2p-corridor/g1 green. Greyscale `643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- Chr camo still SHADE-flattened vs brick (bind hits; no invented palette).
- Aim `skip=pose`. Combat AI / matching engine later. Campaign out of v1.
- Walk clip/teleport past the hall (r11 dump at -651,-1311). Guards through closed doors.

---

## STATUS (2026-08-31 shoot `b1a9201`)

Live `playtest-shoot.png` after `ca673cf`: PP7 2/21 hp 8 hits 5 los 1 shots 4, crosshair on spawn-hall extra idle, body stayed up. Dump at x=-219 z=-2364 θ=264: pad cylinder perp=104u (skip=pose mesh vs 30u pad), tile-exit t=386, `guard=0`. Hitscan now uses the posed G1 viscyl (floor Y, r≥115 so that body registers) and marks the pad dead. Aim `skip=pose`.

Harness: `play_shoot_before` standing torso; 7 Z-fires; extra idle dead; `play_shoot_after` pack death rest (not standing). Native player/gun/lockstep/2p-corridor/g1 green. Greyscale `643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- Walk clip/teleport past the hall (Chris r11 -651,-1311 y=29.1).
- Guards through closed G1 doors / occlusion.
- Chr camo still SHADE-flattened vs brick. Aim `skip=pose`. Campaign out of v1.

---

## STATUS (2026-08-31 clip `1601098`)

Hall r12 tiles at +319 (under `PORT_RISE_MAX` 350) still launched a 12u step to eye 348 then snapped 300-500u onto the landing. Dump: 36 headings from spawn; th=110 from=-72,-2311 → 400,-2250 y=348 j=477, then drop through the stack to y=29. Retail r71→r12 now climbs only at the stair foot (`PLAY_STAIR` ~-572,-2229). Snaps >120u from the requested dest are refused except there. `try_snap_local` caps at 120u (was 800). `rising_landing` stays same-floor.

Harness: hunt teleports=0; spawn/WASD/A→B y=29.1 r71; real stair still `-244,-2098` eye 348.2 mean 65. Aim `skip=pose`. Native player/gun/lockstep/2p-corridor/g1 green. Greyscale `643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- Guards through closed G1 doors (`play_clip_door` / Chris r11). Dead skip=pose body through the hall wall (`play_shoot_after`).
- Chr camo still SHADE-flattened vs brick. Aim `skip=pose`. Campaign out of v1.

---

## STATUS (2026-08-31 death floor `57e8429`)

`play_shoot_after` after `b1a9201` was a skip=pose death rest jammed through the hall G1 wall. Die-model part origins now clamp to the pad (±45 xz, 0..40 y) so the body lies on the tile. No invented ragdoll. Living chr / door ghosts unchanged.

Harness: `play_shoot_before` standing extra idle; `play_shoot_after` torso on the floor. Clip hunt still 0. Native player/gun/lockstep/2p-corridor/g1 green. Greyscale `643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- Guards through closed G1 doors (`play_clip_door` / Chris r11 — skip=pose mesh vs pad).
- Chr camo still SHADE-flattened vs brick. Aim `skip=pose`. Campaign out of v1.

---

## STATUS (2026-08-31 door clip `c012341`)

Chris `play_clip_door` at x=-651.1 z=-1311.4 y=29.12 θ=24° cur=11: a skip=pose
guard mesh (pad -360.6,-1680.1, 469u along the look) painted through the closed
brown ribbed leaf. Vis AABB was compact on that pad; G1 applied skip=pose Euler
plus in-DL G_MTX and drew the body on the door. Extra idle in the spawn hall
was not behind that leaf (cam-pad ray miss).

Living chr DLs now ignore G_MTX/G_POPMTX/G_DL (pose is already T*R_yaw) and
drop extracted Euler so draw stays on the pad. Pads behind a closed G1 door
rectangle are not drawn (extra idle is not). Living chrs farther than 400u are
not drawn (spawn-hall extra idle is <300u). Die rest clamp unchanged.

Harness: `play_clip_door` door leaf solid, no limb through the ribbing;
`play_hall_walk` no arm through the stall door; extra idle still dies
(kills=1); hunt teleports=0. Native player/gun/lockstep/2p-corridor/g1 green.
Greyscale `643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- Chr camo still SHADE-flattened vs brick. Aim `skip=pose`. Campaign out of v1.

---

## STATUS (2026-08-31 extra idle `e3a2eff`)

`c012341` made the r11 ribbed door solid but emptied the spawn hall: living
G_DL/Euler containment plus a 400u range cap hid extra idle (`drawn=70`).
Extra idle is never skip-drawn. It now sits at `-350,-2320` (past Z_TRIG,
inside spawn 270 / PLAY_SHOOT FOV). Closed-leaf skip and a 380u cap apply
only to other living pads, so guard 36 (469u) still cannot paint through
the r11 leaf.

Harness: `play_spawn` / `play_hall_a` / `play_shoot_before` standing extra
idle (`rest=skel`); `play_clip_door` leaf solid; extra idle still dies
(kills=1). Native player/gun/lockstep/2p-corridor/g1 green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- Chr camo still SHADE-flattened vs brick. Aim `skip=pose`. Campaign out of v1.

---

## STATUS (2026-08-31 extra-idle mesh `fc5214a`)

shots2 extra idle was a vertical green slab off the head (vertex card to the
ceiling). Dump vs `goldeneye_src`: rest=skel `T*R_yaw*R_pose` and Chead*Z on
neck (opcode 23 / Switches[4]) were already right; G_VTX banks are compact
(vmax ~200–500). Rare `modelApplyHeadRelations` replaces HeadPlaceholder.Child
with Chead*Z RootNode. The body's default-head GDL at that neck origin was
still drawn under Jim/Sally. That card is skipped. Opcode 4 binds Vertices
(not BaseAddr/COL1). skip=pose DLs ignore G_MTX and do not keep a previous
room's clip verts. Door-leaf skip / idle exempt / death clamp / shooting
unchanged.

Harness: `play_spawn` / `play_hall_a` / `play_shoot_before` standing camo
body+head, no ceiling plane; `play_clip_door` leaf solid (md5 unchanged);
`play_shoot_after` kills=1, body on the pad floor. Native player/gun/lockstep/
2p-corridor/g1 green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- Remaining G1 clip while walking. Chr camo still SHADE-flattened vs brick.
  Aim `skip=pose`. Campaign out of v1.

---

## STATUS (2026-08-31 hall floor + camo `9bb17ae`)

Chris live A y=29.1 → B y=409.6 r12: overlapping r12 under the spawn hall
could still win if the tile cache held the high polygon. Ground-floor
clip now prefers the lowest tile and refuses a hall-band launch except
at the dump-verified stair foot. Analog-sized A→B steps stay y=29.1 r71.

skip=pose Vtx.cn is baked greyscale, not RSP lighting. SHADE*TEXEL
flattened oliveguard SETTEX 1916 (CI8 camo) to a slab. Identity SHADE on
no_mtx parts keeps texel albedo. Rooms still modulate. skip=pose standing
body+head unchanged (no green slab). Door-leaf skip / idle exempt / death
clamp / shooting unchanged.

Harness: `play_spawn` / `play_hall_a` / `play_hall_walk` / `play_shoot_before`
standing camo (olive uniq≥21, var≥93); A→B 4u/12u y=29.1 r71 hopped=0;
`play_clip_door` leaf solid; `play_shoot_after` kills=1; hunt teleports=0;
real stair still `-244,-2098` eye 348.2. Native player/gun/lockstep/
2p-corridor/g1 green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- Aim `skip=pose`. Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-08-31 FPS `e01e97f`)

Chris live `007.goodhouseinc.com` felt 2–3 FPS after skip=pose / door-leaf
ships. Cause: `emit_guard_body` walked G1 door-leaf triangles (vis AABB +
4-iter `chr_push` + `leaf_blocks`) for every `near_room` living pad
*before* the 380u skip-draw cap. Spawn considered 25 of 65 guards × ~8
rooms × 5 scans.

`e01e97f` range-culls first (extra idle still drawn; guard 36 at 469u still
skip-draw). Door-like G1 planes are cached per room. `walk_step` stderr is
once. KEEP gameplay SHAs unchanged.

Mac harness spawn: **1316ms/frame (0.8 fps, unopt HEAD)** → **35ms (28 fps
unopt) / 27.6ms (36 fps -O2)**. `drawn=69` unchanged.

Harness: `play_spawn` / `play_hall_a` / `play_hall_walk` / `play_shoot_*`
y=29.12; camo olive (uniq≥21, var≥93); `play_clip_door` leaf solid, hunt
teleports=0; `play_shoot_after` kills=1 mag 7/14; real stair still
`-244,-2098` eye 348.2. Native player/gun/lockstep/2p-corridor/g1 green.
Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- Aim `skip=pose`. Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-08-31 FPS `e01e97f`)

Chris live `007.goodhouseinc.com` felt 2–3 FPS after skip=pose / door-leaf
ships. Cause: `emit_guard_body` walked G1 door-leaf triangles (vis AABB +
4-iter `chr_push` + `leaf_blocks`) for every `near_room` living pad
*before* the 380u skip-draw cap. Spawn considered 25 of 65 guards × ~8
rooms × 5 scans.

`e01e97f` range-culls first (extra idle still drawn; guard 36 at 469u still
skip-draw). Door-like G1 planes are cached per room. `walk_step` stderr is
once. KEEP gameplay SHAs unchanged.

Mac harness spawn: **1316ms/frame (0.8 fps, unopt HEAD)** → **35ms (28 fps
unopt) / 27.6ms (36 fps -O2)**. `drawn=69` unchanged.

Harness: `play_spawn` / `play_hall_a` / `play_hall_walk` / `play_shoot_*`
y=29.12; camo olive (uniq≥21, var≥93); `play_clip_door` leaf solid, hunt
teleports=0; `play_shoot_after` kills=1 mag 7/14; real stair still
`-244,-2098` eye 348.2. Native player/gun/lockstep/2p-corridor/g1 green.
Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- Aim `skip=pose`. Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-08-31 playtest A/B/C `6ec243b`)

Chris live after `e01e97f`: (A) brief freeze every fire, (B) dead extra idle a
bright-green floor pancake with a detached brown hat, (C) walking the Facility
hall visually jumped to other rooms. Idle-arm / joint-matrix WIP is stashed
(`idle-arm-wip-deferred`); not this slice.

**A — shoot hitch.** `port_prop_chr_ray_hit` walked every living pad's GDL
verts + G1 leaf push on each Z. Cheap pad cylinder (r=150) first; posed viscyl
only for those hits, no leaf push on the fire path. `tile_exit_hit` followed
Rare `point.link` instead of 24×2599 tile scans. Door/guard hitscan Y no longer
calls `tile_at_world` per pad. KEEP `b1a9201` viscyl kills.

Harness: `fire_hitch miss_ms=3.38 hit_ms=3.70` (was ~13ms before the stan ray
cut); `play_shoot_after` kills=2 mag 7/14; extra idle dies.

**B — death mesh splat.** `57e8429` ±45/0..40 origin clamp stacked every limb
onto the pad and left Chead unclamped (floating hat). Dead emit now skips
exploded parts, drops death Euler on skip=pose (bind-pose chunks), and pins
body+head origins on the pad floor (`ly=14`). KEEP death-on-floor, not ragdoll.

Harness: `play_shoot_before` standing camo body+head; `play_shoot_after` compact
camo corpse on the tile with the hat on it (no floor pancake, no floating head).

**C — visual map warps.** skip=pose chr `G_DL` resolved leftover seg 14/15 (BG
/ last room Vtx) and walked another room's GDL. Isolate those segments for
`no_mtx` parts; only follow in-file children. Ground-floor
`port_stan_tile_room_at_eye` prefers the lowest tile inside eye slack so
overlapping r12 cannot win the blit. KEEP `1601098` hall teleport refusal.

Harness: `play_hall_walk` / A→B y=29.12 r71 hopped=0; `clipdoor hunt
teleports=0`; real stair still `-244,-2098` eye 348.2 mean 65. Spawn `drawn=69`
frame_ms=35.2 (28.4 fps unopt) — `e01e97f` kept.

Native player/gun/lockstep/2p-corridor/g1 green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- Aim `skip=pose`. Combat AI / matching engine later. Campaign out of v1.
- Death rest is a compact on-floor pile (skip=pose), not a full N64 lie-down.

---

## STATUS (2026-08-31 death rest + idle arms `c0277f9`)

N64-feel slice on top of A/B/C `6ec243b` (KEEP). SHA `c0277f9`. Die anim last frame is a
hierarchical 4x4 joint table (SKELETON(guard) MatrixID0/1, 20 slots on
oliveguard), not skip=pose Euler. A ~90° lie-down gimbal-locked T*R and
became the compact camo pile. Seg-3 G_MTX is `view * T(pad)*R_yaw*S *
joint` so a LOAD cannot replace look-at (no ceiling slab). Standing scale
kept; `fit_ymin` is the posed min Y so the body sits on the pad
(`PORT_DIE_FLOOR_LIFT` 12). Hat uses the neck 4x4. Fallback skip=pose
floor clamp only when joints are missing. Living idle uses the same table
so palette-skinned arms are not a T-pose.

Idle-arm WIP stays in `stash@{0}` (`idle-arm-wip-deferred`); this is a
clean redo (45-slot table, die enabled). KEEP `57e8429` floor pin,
`e01e97f` spawn FPS, `fc5214a` no green slab.

**1 — death rest.** `play_shoot_after_down` (pitch −40): face-up lie-down
on the tile, arms out, hat on the body; second corpse also down the hall.
No green splat, no floating hat, not a compact pile.
`die=1 … f=88 j=20 ymin=-174`.

**2 — idle arms.** Spawn-hall extra idle is a standing camo body+head with
a hanging arm (not a ceiling slab, not a T-pose cross). `rest=skel`
`fit=0.123 h=1510`.

**3 — hitch / jump.** Re-measured, no extra touch: `fire_hitch
miss_ms=3.41 hit_ms=3.83`; `play_shoot_after` kills=2 mag 7/14;
A→B / hall walk y=29.12 r71 hopped=0; `clipdoor hunt teleports=0`; real
stair still `-244,-2098` eye 348.2 mean 65.

Spawn `drawn=69` frame_ms=35.29 (28.3 fps unopt) — `e01e97f` kept.

Native player/gun/lockstep/2p-corridor/g1 green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- Aim `skip=pose`. Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-08-31 aim pose `999e0fc`)

N64-feel slice on top of death/idle joints `c0277f9` (KEEP). SHA `999e0fc`. In-box living
shooters bind pack `PTR_ANIM_fire_standing` frame 53 through the same
hierarchical 4x4 joint table as idle/die. Frame 0 of that clip hangs like
idle; mid-cycle is the rifle-fire still. skip=pose Euler is not used
(that exploded the mesh). Do not fake an arm. Unique model id
(`PORT_AIM_ID_BASE+body`) so idle clones stay hanging. Unalerted drop
back to idle when they leave LOS. Living aim copies the neck 4x4 so the
hat follows. Reject a non-standing span or a bind without joints.
Preload per body at setup so the first LOS is not an inflate hitch.

KEEP `c0277f9` idle/death joints, `fc5214a` no ceiling slab, `e01e97f`
spawn FPS, `57e8429` floor pin.

**1 — aim pose.** `play_shoot_before` still the hanging-arm extra idle
(back). After hear + fire tick: `aim_look have=1 bound=1` `aim=1 … f=53
h=1409 j=20 rest=skel`. `play_aim_look` from the shoot pad: guard turned
to face the camera, arms in the fire_standing still (not a T-pose, not a
ceiling slab). Camo `olive=11947 uniq=27 luma=79.1 var=120.1`.

**2 — hitch / jump / death.** Re-measured: `fire_hitch miss_ms=3.39
hit_ms=3.71`; `play_shoot_after` kills=2 mag 7/14; `play_shoot_after_down`
face-up lie-down, hat on the body; A→B hopped=0 y=29.12 r71; `clipdoor
hunt teleports=0`; real stair still `-244,-2098` eye 348.2 mean 65.

Spawn `drawn=69` frame_ms=34.85 (28.7 fps unopt) — `e01e97f` kept.

Native player/gun/lockstep/2p-corridor/g1 green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-08-31 held KF7 `1312936`)

N64-feel slice on top of aim joints `999e0fc` (KEEP). SHA `1312936`. In-box fire_standing
guards parent dump `PchrkalashZ` (ASSIGNEDTOCHR model 184) to Rare
`Switches[3]` (right-wrist GROUP MatrixID0=15) via the same hierarchical
4x4 path as the hat. Native `process_15` attach * T(GROUPSIMPLE Origin).
Idle hang stays empty. Dead bodies drop the floor KF7 (not a held emit).
Preload the prop at setup so the first LOS is not an inflate hitch.
Mutate/restore the shared drop model so catalog 0.1 floor KF7 is unchanged.

KEEP `999e0fc` aim joints, `c0277f9` idle/death, `fc5214a` no ceiling slab,
`e01e97f` spawn FPS, `57e8429` floor pin.

**1 — KF7 in hands.** `play_shoot_before` `held=0` hanging-arm extra idle.
After hear + fire tick: `aim_look have=1 bound=1` `aim=1 … f=53 h=1409 j=20
rest=skel` `held=1 drawn=70`. `play_aim_look` is down the barrel (muzzle).
`play_aim_grip` 3/4 from ~200u: tan third-person KF7 in the fire_standing
right-hand grip (not a T-pose, not a ceiling slab). Camo `olive=6292 uniq=26
luma=80.4 var=141.3`. `held_gun n=50 mtx=65`.

**2 — hitch / jump / death.** Re-measured: `fire_hitch miss_ms=3.37
hit_ms=3.71`; `play_shoot_after` kills=2 mag 7/14; `play_shoot_after_down`
face-up lie-down, hat on the body, floor KF7 beside the corpse; A→B hopped=0
y=29.12 r71; `clipdoor hunt teleports=0`; real stair still `-244,-2098` eye
348.2 mean 65.

Spawn `drawn=69 held=0` frame_ms=35.83 (27.9 fps unopt) — `e01e97f` kept
(35ms class; spawn does not emit the held mesh).

Native player/gun/lockstep/2p-corridor/g1 green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-01 death fetal rest `1207531`)

N64-feel slice on top of held KF7 `1312936` (KEEP). SHA `1207531`.
`play_shoot_after_down` after `1312936` was a face-up lie-down whose last
frame of `PTR_ANIM_death_forward_face_down` is ~1500u along model Z with
BASE at the origin, so the shoot-pad −40° look saw soles in the near
plane (legs-up). Bind pack `PTR_ANIM_death_fetal_position_right` (86
frames, last 85) instead — a tucked on-tile rest, not an invented curl.
`pin_die_floor` also XZ-centers posed joint translations on the pad (same
Y floor pin, not a ragdoll). Hierarchical 4x4 joint table unchanged.

KEEP `1312936` held KF7, `999e0fc` aim joints, `c0277f9` idle/death,
`fc5214a` no ceiling slab, `e01e97f` spawn FPS, `57e8429` floor pin.

**1 — death rest.** `play_shoot_after_down` (pitch −40): tucked camo
corpse on the tile, hat on the body, no two vertical legs into the
camera; second corpse also down the hall. Floor KF7 beside the corpse.
`die=1 addr=0x4c59c off=312732 fr=86 w=12 f=85 j=20 ymin=-245`.

**2 — hitch / jump / aim.** Re-measured: `fire_hitch miss_ms=3.37
hit_ms=3.74`; `play_shoot_after` kills=2 mag 7/14; `play_shoot_before`
`held=0`; `aim_look have=1 bound=1` `held=1 drawn=70`; `play_aim_grip`
tan KF7 in the fire_standing grip. A→B hopped=0 y=29.12 r71; `clipdoor
hunt teleports=0`; real stair still `-244,-2098` eye 348.2 mean 65.

Spawn `drawn=69 held=0` frame_ms=35.06 (28.5 fps unopt) — `e01e97f` kept.

Native player/gun/lockstep/2p-corridor/g1 green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-01 Hor+ G1 `7f974af`)

N64-feel slice on top of death fetal `1207531` (KEEP). SHA `7f974af`.
Live widescreen (`ff_widescreen` default on, lockstep 640×360) was
blitting a 4:3 G1 320×240 buffer — anamorphic stretch, not Hor+. G1
look-at now takes `port_persp` fovy/aspect (default 60° / 4:3 so
greyscale and the 320×240 harness stay bit-identical). Vertical FOV
stays native; 16:9 widens hfov 75.2° → 91.5°. Presenter-only; near
stays G1 10; checksum unchanged.

KEEP `1207531` fetal death, `1312936` held KF7, `999e0fc` aim joints,
`c0277f9` idle/death, `fc5214a` no ceiling slab, `e01e97f` spawn FPS,
`57e8429` floor pin.

**1 — Hor+.** `play_spawn` aspect=1.333 hfov=75.2 drawn=69 held=0.
`play_spawn_wide` aspect=1.778 hfov=91.5; extra-idle olive_cx 91 → 108
(left body packed toward center; more stall on the left). g1-test
`hor+ mean_x 279 → 249`. player-test hfov 4:3=75.18 16:9=91.49.

**2 — hitch / jump / aim / death.** Re-measured: `fire_hitch
miss_ms=3.76 hit_ms=3.91`; `play_shoot_after` kills=2 mag 7/14;
`play_shoot_before` `held=0`; `aim_look have=1 bound=1` `held=1
drawn=70`; `play_aim_grip` tan KF7 in the fire_standing grip.
`play_shoot_after_down` fetal ymin=-245, floor KF7, hat on the body.
A→B hopped=0 y=29.12 r71; `clipdoor hunt teleports=0`; real stair
still `-244,-2098` eye 348.2 mean 65.

Spawn `drawn=69 held=0` frame_ms=36.27 (27.6 fps unopt) — `e01e97f`
35ms class kept.

Native player/gun/lockstep/2p-corridor/g1 green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-01 Chead neck 4x4 `b56a698`)

N64-feel slice on top of Hor+ `7f974af` (KEEP). SHA `b56a698`.
Idle/walk Chead used skip=pose Euler on a shared `Chead*Z` joint[0], so
the last visible guard's neck won (aim face on an idle body, photo tilt).
Rare `modelApplyHeadRelations` parents Chead RootNode to HeadPlaceholder;
Cheadjim has no GROUP (SWITCH → DL, G_MTX slot 0). Snapshot that pose's
neck 4x4 into the draw record (`joint_one`) so idle, aim, and death heads
do not clobber each other. Same njoint=1 snapshot as the held KF7 (wrist
was restored before interpret).

KEEP `7f974af` Hor+, `1207531` fetal death, `1312936` held KF7, `999e0fc`
aim joints, `e01e97f` spawn FPS, `57e8429` floor pin.

**1 — heads.** `head_joint chr=0 T=5.8,520.1,-36.7 idle` (dump neck).
`play_spawn` `headj=1` standing camo + 3D Chead on the neck. `aim_look`
`T=-163.1,478.6,108.2 aim` `headj=1 held=1`. `play_aim_grip` Jim on the
fire_standing neck, tan KF7 in the right-hand grip. `play_shoot_after_down`
fetal `headj=2` hat on the tucked corpse, floor KF7.

**2 — hitch / jump / Hor+.** Re-measured: `fire_hitch miss_ms=3.39
hit_ms=3.73`; `play_shoot_after` kills=2 mag 7/14; `play_shoot_before`
`held=0`; `aim_look have=1 bound=1` `held=1 drawn=70`. A→B hopped=0
y=29.12 r71; `clipdoor hunt teleports=0`; real stair still `-244,-2098`
eye 348.2 mean 65. `play_spawn` aspect=1.333 hfov=75.2;
`play_spawn_wide` aspect=1.778 hfov=91.5.

Spawn `drawn=69 held=0 headj=1` frame_ms=34.77 (28.8 fps unopt) —
`e01e97f` 35ms class kept.

Native player/gun/lockstep/2p-corridor/g1 green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-01 camera-space PP7 `42ba170`)

N64-feel slice on top of Chead neck `b56a698` (KEEP). SHA `42ba170`.
Rare PosXYZ + bind-pose Euler hangs GwppkZ below the G1 near plane at
phi=0 (muzzle sliver). `.view` applied look pitch, so looking down filled
the FB and looking level hid the gun. Lock the camera-space product that
used to appear at phi=-35 (`View(-35°)` ≡ `Rx(+35°)` around the eye).
Hold XYZ stays Rare. Look pitch is not applied to `.view`.

KEEP `b56a698` per-pose Chead neck, `7f974af` Hor+, `1207531` fetal
death, `1312936` held KF7, `999e0fc` aim joints, `e01e97f` spawn FPS.

**1 — viewgun.** `play_spawn` `viewgun_lr n=12771` PP7+hand on-screen at
phi=0 (was a sliver). `play_hall_a` / `play_aim_grip` / `play_stairs`
classic lower-right PP7. `play_shoot_after_down` phi=-40 `viewgun_lr
n=5053 top_r=1290` — gun stays put, fetal corpse visible, not a
soles-to-camera fill. `g1 viewgun mean_y 213 -> 213` (pitch does not
swing).

**2 — hitch / jump / Hor+ / heads.** Re-measured: `fire_hitch
miss_ms=3.38 hit_ms=3.71`; `play_shoot_after` kills=2 mag 7/14
`held=0`; `aim_look have=1 bound=1` `held=1 drawn=70`; `play_aim_grip`
Jim + tan KF7 grip `headj=1`. A→B hopped=0 y=29.12 r71; `clipdoor hunt
teleports=0`; real stair still `-244,-2098` eye 348.2 mean 65.
`play_spawn` aspect=1.333 hfov=75.2; `play_spawn_wide` aspect=1.778
hfov=91.5. `play_spawn` `drawn=69 held=0 headj=1`.

Spawn `drawn=69 held=0 headj=1` frame_ms=36.10 (27.7 fps unopt) —
`e01e97f` 35ms class kept.

Native player/gun/lockstep/2p-corridor/g1 green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-01 pad-cylinder hitscan `a756d97`)

N64-feel slice on top of PP7 rest Rx `7886c41` (KEEP). SHA `a756d97`.
Chris live: crosshair on a living guard + fire, hits stayed 0 while ammo
drained (PP7 0/0 after 28 misses). Idle/aim/die 4x4 sits the drawn body
on the pad; bind-pose viscyl can sit ~100u off, so requiring pad AND
viscyl rejected the pad hit. r=150 already covers that skip=pose offset
(`b1a9201`). Fire uses the pad cylinder only — no GDL walk, hitch stays
in the `6ec243b` 3–4ms band.

KEEP `7886c41` forward PP7, `a106ce5` clip-door fill, `fecd44a` sealed
faces, `e9247e9` closed-door portal cull, `42ba170` camera-space lock,
`b56a698` per-pose Chead neck, `7f974af` Hor+, `1207531` fetal death,
`1312936` held KF7, `999e0fc` aim joints, `e01e97f` spawn FPS,
`b1a9201` setup-chr hitscan.

**1 — hits.** `shoot chr_ray hit=1 t=281.8`; `fire_hitch miss_ms=3.85
hit_ms=3.58 hits=1`; `play_shoot_after` hits=0→6 kills=2 dead=0→2 mag=7
`die=1`. Native `hitscan_guard` / pitch0 still green.

**2 — hitch / jump / Hor+ / heads / gun / doors.** Re-measured:
`viewgun_lr play_spawn n=13525`; `play_shoot_after_down` n=3179
top_r=1290 (no look-swing). `clipdoor_olive n=34` `walked n=1 11`
`clipdoor_fill dark=0 metal=3546 area=5120`; hunt teleports=0.
Spawn `drawn=68 held=0 headj=1` frame_ms=24.61 (40.6 fps unopt) /
`--bench` frame_ms=25.10 (39.8 fps) — `e01e97f` 35ms class kept.

Native player/gun/g1 green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- Brown door fills still read as black/strange panels (P0-C).
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-01 PP7 rest Rx `7886c41`)

N64-feel slice on top of clip-door G1 hole fill `a106ce5` (KEEP). SHA `7886c41`.
Chris live after `a106ce5` hard-refresh: PP7 pointed nearly vertical at
the ceiling at φ≈0. `42ba170` locked camera-space by composing Rx(+35°)
(the old look-down product) around the eye. That lifted GwppkZ on-screen
but tilted the barrel up. Rest Rx is +15°: enough to lift Rare PosXYZ
out of the frustum floor and keep `mtx_euler` off the R180 gimbal, with
the PP7 in the lower-right pointing forward. Hold XYZ stays Rare. Look
pitch is still not applied to `.view`.

KEEP `a106ce5` clip-door fill, `fecd44a` sealed opening faces,
`e9247e9` closed-door portal cull, `42ba170` camera-space lock,
`b56a698` per-pose Chead neck, `7f974af` Hor+, `1207531` fetal death,
`1312936` held KF7, `999e0fc` aim joints, `e01e97f` spawn FPS,
`b1a9201` setup-chr hitscan.

**1 — viewgun rest.** `viewgun_lr play_spawn n=13525` `play_spawn_wide
n=13153`. `play_shoot_before` / `play_hall_a` show lower-right forward
PP7 (not at the ceiling). `play_shoot_after_down` phi=-40
`viewgun_lr n=3179 top_r=1290` (no look-swing).

**2 — hitch / jump / Hor+ / heads / gun / doors.** Re-measured:
`fire_hitch miss_ms=3.47 hit_ms=3.92 hits=1`; `play_shoot_after`
kills=2 mag 7/14 `held=0`; `clipdoor_olive n=34` `walked n=1 11`
`clipdoor_fill dark=0 metal=3546 area=5120`; hunt teleports=0.
A→B hopped=0 y=29.12 r71. `play_spawn` aspect=1.333 hfov=75.2;
`play_spawn_wide` aspect=1.778 hfov=91.5. Spawn `drawn=68 held=0
headj=1` camo olive=4172 uniq=22.

Spawn `drawn=68 held=0 headj=1` frame_ms=25.23 (39.6 fps unopt) /
`--bench` frame_ms=25.34 (39.5 fps) — `e01e97f` 35ms class kept.

Native player/gun/g1 green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- Live hits-on-crosshair (P0-A) and brown door fills looking like black
  slabs (P0-C) still open; this ship is P0-B viewgun rest only.
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-01 clip-door G1 hole fill `a106ce5`)

N64-feel slice on top of sealed opening fill `fecd44a` (KEEP). SHA `a106ce5`.
The `play_clip_door` black opening is a G1 wall-mesh cutout, not a Rare
path portal (r11-r71 / r10-r11 / r73-r11 sit elsewhere). After closed-door
portal cull that hole read as a black void (`clipdoor_fill dark=2525/7488`).
Door-sized gaps in the current room's vertical G1 triangles get a solid
SETTEX 685 quad sized to the actual sill/lintel (clip-door cutout
local=-611,-1993 w=213 h=240). Same-room mesh holes fill; through-points
into a different already-walked room do not (open archways stay open).

KEEP `fecd44a` sealed opening faces, `e9247e9` closed-door portal cull,
`42ba170` PP7 camera-space, `b56a698` per-pose Chead neck, `7f974af` Hor+,
`1207531` fetal death, `1312936` held KF7, `999e0fc` aim joints,
`e01e97f` spawn FPS.

**1 — clip-door hole.** `play_clip_door` `clipdoor_fill dark=0 metal=3546
area=5120` (was 2525/7488). `clipdoor_olive n=34` `walked n=1 11`.
`play_spawn` `spawn_fill dark=1222 metal=3619 area=13376`. `play_hall_a`
`dark=712 metal=3936`.

**2 — hitch / jump / Hor+ / heads / gun.** Re-measured: `fire_hitch
miss_ms=3.40 hit_ms=3.82`; `play_shoot_after` kills=2 mag 7/14
`held=0`; `aim_look` `held=1` `headj=1`; `play_aim_grip` held=1
`headj=1`. A→B hopped=0 y=29.12 r71; `clipdoor hunt teleports=0`; real
stair still `-244,-2098` eye 348.2. `play_spawn` aspect=1.333 hfov=75.2;
`play_spawn_wide` aspect=1.778 hfov=91.5. `play_spawn` `drawn=68 held=0
headj=1` camo olive=4172 uniq=22. `viewgun_lr play_spawn n=12771`;
`play_shoot_after_down` phi=-40 `viewgun_lr n=5053 top_r=1290` (no
look-swing). Fetal ymin=-245.

Spawn `drawn=68 held=0 headj=1` frame_ms=25.13 (39.8 fps unopt) /
`--bench` frame_ms=25.22 (39.6 fps) — `e01e97f` 35ms class kept.

Native player/gun/lockstep/2p-corridor/g1 green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- Look-down death still awkward; idle KF7 empty / death floor KF7 polish;
  camo SHADE-flattening / G1≠stan leftovers.
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-01 sealed opening fill `fecd44a`)

N64-feel slice on top of closed-door portal cull `e9247e9` (KEEP). SHA `fecd44a`.
Retail `Pgas_plant_met1_do1` is a 96-vert FRAME with a hole, so after
portal cull a sealed opening read as a black void. Fitted path faces are
a solid G1DL quad (SETTEX 685) sized to the Rare portal horiz × tall.
Facility spawn r71 left alcove covers the G1 mesh that ends short of the
stan tile (door-sized stamp left a hole).

KEEP `e9247e9` closed-door portal cull, `42ba170` PP7 camera-space,
`b56a698` per-pose Chead neck, `7f974af` Hor+, `1207531` fetal death,
`1312936` held KF7, `999e0fc` aim joints, `e01e97f` spawn FPS.

**1 — sealed face.** `play_spawn` left void gone: `spawn_fill dark=1222
metal=3619 area=13376` (was a black rectangle). `play_hall_a`
`dark=712 metal=3936`. `play_clip_door` `clipdoor_olive n=34`
`clipdoor_fill dark=2525 metal=2570 area=7488` `walked n=1 11`.

**2 — hitch / jump / Hor+ / heads / gun.** Re-measured: `fire_hitch
miss_ms=3.42 hit_ms=3.77`; `play_shoot_after` kills=2 mag 7/14
`held=0`; `aim_look have=1 bound=1` `held=1` `headj=1`; `play_aim_grip`
held=1 `headj=1`. A→B hopped=0 y=29.12 r71; `clipdoor hunt
teleports=0`; real stair still `-244,-2098` eye 348.2. `play_spawn`
aspect=1.333 hfov=75.2; `play_spawn_wide` aspect=1.778 hfov=91.5.
`play_spawn` `drawn=68 held=0 headj=1` camo olive=4172 uniq=22.
`viewgun_lr play_spawn n=12771`; `play_shoot_after_down` phi=-40
`viewgun_lr n=5053 top_r=1290` (no look-swing). Fetal ymin=-245.

Spawn `drawn=68 held=0 headj=1` frame_ms=25.43 (39.3 fps unopt) —
`e01e97f` 35ms class kept.

Native player/gun/lockstep/2p-corridor/g1 green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- Some G1 door-leaf cutouts (clip-door interior) still rim-black vs a
  full N64 door panel.
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-01 closed-door portal vis `e9247e9`)

N64-feel slice on top of camera-space PP7 `42ba170` (KEEP). SHA `e9247e9`.
Draw walked portal neighbors even when the connecting door was shut, so
`play_clip_door` showed the next hall, EXIT, and a camo guard through a
closed opening. Rare `doorActivatePortal` clears `PORTALFLAG_DISABLED`
only while opening. Doorlike portals with a closed bound slab no longer
enqueue the far room. Open / frac>0 and non-doorlike archways still
traverse. Pads whose stan room was not walked skip draw; spawn-hall
extra idle stays exempt.

KEEP `42ba170` PP7 camera-space, `b56a698` per-pose Chead neck,
`7f974af` Hor+, `1207531` fetal death, `1312936` held KF7, `999e0fc`
aim joints, `e01e97f` spawn FPS.

**1 — closed door.** `play_clip_door` `walked n=1 11` `clipdoor_olive
n=34` (was next-room camo + EXIT through the leaf). Doorway is the
current-room frame, not the far hall. Synthetic `closed-portal vis
closed_walked=1 open_walked=2`.

**2 — hitch / jump / Hor+ / heads / gun.** Re-measured: `fire_hitch
miss_ms=3.42 hit_ms=3.78`; `play_shoot_after` kills=2 mag 7/14
`held=0`; `aim_look have=1 bound=1` `held=1` `headj=1`; `play_aim_grip`
Jim + tan KF7 grip `headj=1`. A→B hopped=0 y=29.12 r71; `clipdoor hunt
teleports=0`; real stair still `-244,-2098` eye 348.2. `play_spawn`
aspect=1.333 hfov=75.2; `play_spawn_wide` aspect=1.778 hfov=91.5.
`play_spawn` `drawn=68 held=0 headj=1` camo olive=4172 uniq=22.
`viewgun_lr play_spawn n=12771`; `play_shoot_after_down` phi=-40
`viewgun_lr n=5053 top_r=1290` (no look-swing). Fetal ymin=-245.

Spawn `drawn=68 held=0 headj=1` frame_ms=25.41 (39.4 fps unopt) —
`e01e97f` 35ms class kept.

Native player/gun/lockstep/2p-corridor/g1 green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- Fitted slabs may not fill every sealed portal (black vs door mesh).
- Combat AI / matching engine later. Campaign out of v1.

