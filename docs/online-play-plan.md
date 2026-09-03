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

## STATUS (2026-09-01 pack rifle-cock reload SFX `4f05c3f`)

SFX follow-up on pack armour-collect `0e86529` (KEEP). SHA `4f05c3f`.
Empty mag with reserve queues pack `GUN_RIFLECOCK` 50 instead of silence.
Dry click stays `EMPTY_GUN_FIRE` 89. Same runtime ALBankFile VADPCM path.
Placeholders remain without a pack. Fire≠use last_sfx contract unchanged
(PP7 gun=1, dry=2, door open=3).

KEEP `0e86529` armour collect, `3856b45` ammo crate, `c1b5747` pack
ricochet, `34e7bb9` leftover camo albedo, `0659cba` door-close,
`50b07c2` KF7/pickup, `102237e` door-jump, `e01e97f` spawn FPS.

**1 — reload cock.** `sfx_bank ready=1 gun_n=12336 dry_n=4752
door_n=8448 fall_n=3904 hit_n=1376 kf7_n=5840 pickup_n=3824
close_n=7104 rico_n=11936 ammo_n=3824 armour_n=4096 reload_n=10320`.
`sfx_reload last=12 mix_diff=512`. `reload_cock mag=0→7 res=14→7
flash=0 act=3 sfx=12 n=6`. `pad_fire_no_unlatch mag=7→6 flash=3
open=0→0 act=1 sfx=1`. `pad_use_no_fire mag=6→6 flash=0 open=1
sfx=3`. `pad_close_no_fire mag=6→6 flash=0 open=0 sfx=8`.
`dry_fire mag=0→0 flash=0 act=2 sfx=2 n=15`. `kf7_pickup weapon=1
mag=7 sfx=7`. `kf7_fire mag=7→6 act=1 sfx=6`.

**2 — hang / hitch / doors.** Re-measured: `play_spawn` `held=1`
`idle_hang tan=4252`. `play_spawn` spawn_fill dark=56 metal=10501
frame_ms=27.70 (36.1 fps). `door_jump` th=249 alcove=-219.0,-2248.3
yaw=180 d=0.0,116.0 slabs=3 frame_ms=34.01. `long_walk
fb=76037→76025 dark=1447`. `clipdoor_fill dark=53 metal=4446`
`clipdoor_olive n=34`. y=29.12. `camo play_spawn olive=4083 uniq=18
var=108.2`. `fire_hitch miss_ms=3.41 hit_ms=3.16 hits=1`. HUD
`play_spawn` mag=7/21 hp=8 sfx=0.

Native player/gun green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- Full ASP HLE still out (music, spatial, footsteps / other SFX IDs).
  Music is still a placeholder loop.
- Camo SHADE-flattening (identity vs N64 lighting) / G1≠stan leftovers.
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-01 pack armour-collect SFX `0e86529`)

SFX follow-up on pack ammo-crate `3856b45` (KEEP). SHA `0e86529`.
Armour collect queues pack `ARMOUR_COLLECT` 81 instead of `PICKUP_GUN`
232. Ammo crates stay `PICKUP_AMMO` 234; gun death-drops stay pickup.
Same runtime ALBankFile VADPCM path. Placeholders remain without a pack.
Fire≠use last_sfx contract unchanged (PP7 gun=1, dry=2, door open=3).

KEEP `3856b45` ammo crate, `c1b5747` pack ricochet, `34e7bb9` leftover
camo albedo, `0659cba` door-close, `50b07c2` KF7/pickup, `102237e`
door-jump, `e01e97f` spawn FPS.

**1 — armour collect.** `sfx_bank ready=1 gun_n=12336 dry_n=4752
door_n=8448 fall_n=3904 hit_n=1376 kf7_n=5840 pickup_n=3824
close_n=7104 rico_n=11936 ammo_n=3824 armour_n=4096`. `sfx_armour
last=11 mix_diff=512`. `pickup_proof` pad=204 model=115 kind=2
arm=0→8 sfx=11 PRESENT GONE HUD. `pad_fire_no_unlatch mag=7→6
flash=3 open=0→0 act=1 sfx=1`. `pad_use_no_fire mag=6→6 flash=0
open=1 sfx=3`. `pad_close_no_fire mag=6→6 flash=0 open=0 sfx=8`.
`dry_fire mag=0→0 flash=0 act=2 sfx=2 n=22`. `kf7_pickup weapon=1
mag=7 sfx=7`. `kf7_fire mag=7→6 act=1 sfx=6`.

**2 — hang / hitch / doors.** Re-measured: `play_spawn` `held=1`
`idle_hang tan=4252`. `play_spawn` spawn_fill dark=56 metal=10501
frame_ms=27.66 (36.1 fps). `door_jump` th=249 alcove=-219.0,-2248.3
yaw=180 d=0.0,116.0 slabs=3 frame_ms=33.84. `long_walk
fb=76037→76025 dark=1447`. `clipdoor_fill dark=53 metal=4446`
`clipdoor_olive n=34`. y=29.12. `camo play_spawn olive=4083 uniq=18
var=108.2`. `fire_hitch miss_ms=3.40 hit_ms=3.19 hits=1`.

Native player/gun/g1 green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- Full ASP HLE still out (music, spatial, footsteps / other SFX IDs).
  Music is still a placeholder loop.
- Camo SHADE-flattening (identity vs N64 lighting) / G1≠stan leftovers.
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-01 pack ammo-crate SFX `3856b45`)

SFX follow-up on pack ricochet `c1b5747` (KEEP). SHA `3856b45`.
Ammo crate / magazine collect queues pack `PICKUP_AMMO` 234 instead of
`PICKUP_GUN` 232. Gun death-drops stay pickup. Same runtime ALBankFile
VADPCM path. Placeholders remain without a pack. Fire≠use last_sfx
contract unchanged (PP7 gun=1, dry=2, door open=3).

KEEP `c1b5747` pack ricochet, `34e7bb9` leftover camo albedo, `0659cba`
door-close, `50b07c2` KF7/pickup, `102237e` door-jump, `7cd1121` idle KF7
hang, `e01e97f` spawn FPS.

**1 — ammo crate.** `sfx_bank ready=1 gun_n=12336 dry_n=4752
door_n=8448 fall_n=3904 hit_n=1376 kf7_n=5840 pickup_n=3824
close_n=7104 rico_n=11936 ammo_n=3824`. `sfx_ammo last=10
mix_diff=512`. `sfx_rico_wall e=113351172710 last=1`.
`pad_fire_no_unlatch mag=7→6 flash=3 open=0→0 act=1 sfx=1`.
`pad_use_no_fire mag=6→6 flash=0 open=1 sfx=3`.
`pad_close_no_fire mag=6→6 flash=0 open=0 sfx=8`.
`dry_fire mag=0→0 flash=0 act=2 sfx=2 n=22`. `kf7_pickup weapon=1
mag=7 sfx=7`. `kf7_fire mag=7→6 act=1 sfx=6`. `path_unlatch r8-r7
local=650.7,-1753.0` OK.

**2 — hang / hitch / doors.** Re-measured: `play_spawn` `held=1`
`idle_hang tan=4252`. `play_spawn` spawn_fill dark=56 metal=10501
frame_ms=27.82 (35.9 fps). `door_jump` th=249 alcove=-219.0,-2248.3
yaw=180 d=0.0,116.0 slabs=3 frame_ms=33.80. `long_walk
fb=76037→76025 dark=1447`. `clipdoor_fill dark=53 metal=4446`
`clipdoor_olive n=34`. `play_hall_a` dark=0 metal=12866. y=29.12.
`camo play_spawn olive=4083 uniq=18 var=108.2`. `fire_hitch
miss_ms=3.37 hit_ms=3.21 hits=1`.

Native player/gun/g1 green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- Full ASP HLE still out (music, spatial, footsteps / other SFX IDs).
  Music is still a placeholder loop.
- Camo SHADE-flattening (identity vs N64 lighting) / G1≠stan leftovers.
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-01 pack ricochet SFX `c1b5747`)

SFX follow-up on leftover camo SETTEX albedo `34e7bb9` (KEEP). SHA `c1b5747`.
Wall / floor / fake-wall hits overlay pack `RICO_8_AFDM_A` 27 on the
flesh-hit mixer channel so a miss still plays PP7. Body hits stay
`HIT_BULLET_FLESH` 69. Same runtime ALBankFile VADPCM path. Placeholders
remain without a pack. Fire≠use last_sfx contract unchanged (PP7 gun=1,
dry=2, door open=3).

KEEP `34e7bb9` leftover camo albedo, `0659cba` door-close, `50b07c2`
KF7/pickup, `102237e` door-jump, `7cd1121` idle KF7 hang, `6ad59c5`
flesh-hit, `4883173` pack gun/dry/door, `e01e97f` spawn FPS.

**1 — wall ricochet.** `sfx_bank ready=1 gun_n=12336 dry_n=4752
door_n=8448 fall_n=3904 hit_n=1376 kf7_n=5840 pickup_n=3824
close_n=7104 rico_n=11936`. `sfx_rico last=9 mix_diff=512`.
`sfx_rico_wall e=113351172710 last=1`. `pad_fire_no_unlatch mag=7→6
flash=3 open=0→0 act=1 sfx=1`. `pad_use_no_fire mag=6→6 flash=0
open=1 sfx=3`. `pad_close_no_fire mag=6→6 flash=0 open=0 sfx=8`.
`dry_fire mag=0→0 flash=0 act=2 sfx=2 n=22`. `kf7_fire mag=7→6
act=1 sfx=6`. `path_unlatch r8-r7 local=650.7,-1753.0` OK.

**2 — hang / hitch / doors.** Re-measured: `play_spawn` `held=1`
`idle_hang tan=4252`. `play_spawn` spawn_fill dark=56 metal=10501
frame_ms=27.65 (36.2 fps). `door_jump` th=249 alcove=-219.0,-2248.3
yaw=180 d=0.0,116.0 slabs=3 frame_ms=33.82. `long_walk
fb=76037→76025 dark=1447`. `clipdoor_fill dark=53 metal=4446`
`clipdoor_olive n=34`. `play_hall_a` dark=0 metal=12866. y=29.12.
`camo play_spawn olive=4083 uniq=18 var=108.2`. `fire_hitch
miss_ms=3.46 hit_ms=3.19 hits=1`.

Native player/gun/g1 green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- Full ASP HLE still out (music, spatial, footsteps / other SFX IDs).
  Music is still a placeholder loop.
- Camo SHADE-flattening (identity vs N64 lighting) / G1≠stan leftovers.
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-01 leftover camo SETTEX albedo `34e7bb9`)

N64-feel slice on pack door-close SFX `0659cba` (KEEP). SHA `34e7bb9`.
skip=pose identity SHADE covers no_mtx chr. Baked-grey cn still crushed
dump ColiveguardZ leftovers 1966/1967/1995/1609–1622/1912/1980 and
Cheadjim 1814–1816/1996 off no_mtx (same SHADE*TEXEL path as door 685).
`g1_tex_slot_keep_albedo` now includes those pack SETTEX ids. No
invented palettes. Standing Facility camo stays identity-SHADE
(`play_spawn` uniq=18).

KEEP `0659cba` door-close, `50b07c2` KF7/pickup, `102237e` door-jump,
`7cd1121` idle KF7 hang, `4883173` pack gun/dry/door, `30db967`
door/camo albedo, `e01e97f` spawn FPS.

**1 — leftover albedo.** `olive tex albedo full=38400` (SETTEX 1967
cn80 off no_mtx). `door tex albedo full=38400` (685 kept).
`skip=pose camo albedo full=38400`. `camo play_spawn olive=4083
uniq=18 var=108.2`. `camo play_hall_a olive=5029 uniq=30`.
`camo play_shoot_before olive=9025 uniq=27`.

**2 — hang / hitch / doors.** Re-measured: `play_spawn` `held=1`
`idle_hang tan=4252`. `play_spawn` spawn_fill dark=56 metal=10501
frame_ms=28.49 (35.1 fps). `door_jump` th=249 alcove=-219.0,-2248.3
yaw=180 d=0.0,116.0 slabs=3 frame_ms=35.60. `long_walk
fb=76037→76025 dark=1447`. `clipdoor_fill dark=53 metal=4446`
`clipdoor_olive n=34`. `play_hall_a` dark=0 metal=12866. y=29.12.
`fire_hitch miss_ms=3.49 hit_ms=3.39 hits=1`.

Native player/gun/g1/2p-corridor green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- Full ASP HLE still out (music, spatial, footsteps / other SFX IDs).
  Music is still a placeholder loop.
- Camo SHADE-flattening (identity vs N64 lighting) / G1≠stan leftovers.
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-01 pack door-close SFX `0659cba`)

SFX follow-up on pack KF7 bolt + pickup `50b07c2` (KEEP). SHA `0659cba`.
Second A on an open door queues pack `DOOR_METAL_CLOSE` 197 instead of
replaying open. Open stays `DOOR_METAL_OPEN` 196. Same runtime ALBankFile
VADPCM path. Placeholders remain without a pack. Fire≠use last_sfx
contract unchanged (PP7 gun=1, dry=2, door open=3).

KEEP `50b07c2` KF7/pickup, `102237e` door-jump, `6ad59c5` flesh-hit,
`7cd1121` idle KF7 hang, `4883173` pack gun/dry/door, `e01e97f` spawn
FPS.

**1 — door close.** `sfx_bank ready=1 gun_n=12336 dry_n=4752
door_n=8448 fall_n=3904 hit_n=1376 kf7_n=5840 pickup_n=3824
close_n=7104`. `sfx_door_close last=8 mix_diff=512`.
`pad_use_no_fire mag=6→6 flash=0 open=1 sfx=3`.
`pad_close_no_fire mag=6→6 flash=0 open=0 sfx=8`.
`pad_fire_no_unlatch mag=7→6 flash=3 open=0→0 act=1 sfx=1`.
`dry_fire mag=0→0 flash=0 act=2 sfx=2 n=22`. `kf7_pickup weapon=1
mag=7 sfx=7`. `kf7_fire mag=7→6 act=1 sfx=6`. `sfx_hit last=5
mix_diff=512`. `sfx_fall last=4 mix_diff=512`. `path_unlatch r8-r7
local=650.7,-1753.0` OK.

**2 — hang / hitch / doors.** Re-measured: `play_spawn` spawn_fill
dark=56 metal=10501 frame_ms=27.94 (35.8 fps). `door_jump` th=249
alcove=-219.0,-2248.3 yaw=180 d=0.0,116.0 slabs=3 frame_ms=34.27.
`clipdoor_fill dark=53 metal=4446`. `play_hall_a` dark=0 metal=12866.
y=29.12.

Native player/gun/g1/2p-corridor/audio green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- Full ASP HLE still out (music, spatial, footsteps / other SFX IDs).
  Music is still a placeholder loop.
- Camo SHADE-flattening / G1≠stan leftovers.
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-01 pack KF7 bolt + pickup SFX `50b07c2`)

SFX follow-up on door jump on rotate `102237e` (KEEP). SHA `50b07c2`.
KF7 fire queues pack `GUN_B4_BOLTACTION` 109 instead of the PP7
`GUN_B2_HEAVY` 107 shot. Collecting a death-drop (or armour / reserve)
queues pack `PICKUP_GUN` 232. Same runtime ALBankFile VADPCM path.
Placeholders remain without a pack. Fire≠use last_sfx contract
unchanged (PP7 gun=1, dry=2, door=3).

KEEP `102237e` door-jump, `6ad59c5` flesh-hit overlay, `7cd1121` idle
KF7 hang, `c92f7bb` pack body-fall, `4883173` pack gun/dry/door,
`9b7b6e6` P0 ammo/fire≠use/walk FB, `e01e97f` spawn FPS.

**1 — KF7 / pickup.** `sfx_bank ready=1 gun_n=12336 dry_n=4752
door_n=8448 fall_n=3904 hit_n=1376 kf7_n=5840 pickup_n=3824`.
`sfx_kf7 last=6 mix_diff=494`. `sfx_pickup last=7 mix_diff=494`.
`kf7_pickup weapon=1 mag=7 sfx=7`. `kf7_fire mag=7→6 act=1 sfx=6`.
`pad_fire_no_unlatch mag=7→6 flash=3 open=0→0 act=1 sfx=1`.
`pad_use_no_fire mag=6→6 flash=0 open=1 sfx=3`. `dry_fire mag=0→0
flash=0 act=2 sfx=2 n=22`. `sfx_hit last=5 mix_diff=512`. `sfx_fall
last=4 mix_diff=512`.

**2 — hang / hitch / doors.** Re-measured: `play_spawn` `held=1`
`idle_hang tan=4252`. `play_shoot_before` held=1 tan=5048.
`play_spawn` spawn_fill dark=56 metal=10501 frame_ms=27.76 (36.0 fps).
`door_jump` th=249 alcove=-219.0,-2248.3 yaw=180 d=0.0,116.0 slabs=3
frame_ms=34.30. `long_walk fb=76037→76025 dark=1447`. `clipdoor_fill
dark=53 metal=4446`. `play_hall_a` dark=0 metal=12866. y=29.12.
`fire_hitch miss_ms=3.41 hit_ms=3.17 hits=1`.

Native player/gun/g1/2p-corridor/audio green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- Full ASP HLE still out (music, spatial, footsteps / other SFX IDs).
  Music is still a placeholder loop.
- Camo SHADE-flattening / G1≠stan leftovers.
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-01 door jump on rotate `102237e`)

N64-feel slice on top of pack flesh-hit overlay `6ad59c5` (KEEP). SHA `102237e`.
Chris live: same xz, slight yaw → brown door beside the player (HUD
x -219.0 z -2364.3 y 29.1 θ 249° φ -1° stan 2599+ rm 78). Cause: r71
spawn-alcove 640-wide stamp used current look-left, so the leaf swung
into camera space. Hall-left is intro look (Facility spawn 270 → +Z).
Path/cutout fitted faces no longer skip on a look-along 40u threshold
(yaw pop out of the hole).

KEEP `6ad59c5` flesh-hit overlay, `7cd1121` idle KF7 hang, `fedf44f`
door-leaf interiors, `21cbd0a` portal scale, `a32ed6a` brown fills,
`30db967` door/camo albedo, `e01e97f` spawn FPS.

**1 — door jump.** Chris pad θ=219/234/249/264/279: alcove local
-219.0,-2248.3 yaw=180 d=0.0,116.0 slabs=3 (identical). HUD + PNG
`door_jump_249` / `door_jump_234` / `door_jump_264`. Door stays in the
north-wall hole, not beside the camera.

**2 — hang / hitch / doors / SFX.** Re-measured: `play_spawn` `held=1`
`idle_hang tan=4252`. `play_shoot_before` held=1 tan=5048.
`play_shoot_after` held=0 drop=1. `fire_hitch miss_ms=3.60 hit_ms=3.15
hits=1`. `play_spawn` spawn_fill dark=56 metal=10501 frame_ms=28.19
(35.5 fps). `door_jump` frame_ms=35.59 (28.1 fps) — `e01e97f` 35ms
class kept. `long_walk fb=76037→76025 dark=1447`. `clipdoor_fill
dark=53 metal=4446`. `play_hall_a` dark=0 metal=12866. y=29.12.
`die_across add=90 olive=6159`. `sfx_hit last=5 mix_diff=512`.
`pad_fire_no_unlatch` mag=7→6 act=1. `pad_use_no_fire` sfx=3.

Native player/gun/g1/2p-corridor/audio green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- Full ASP HLE still out (music, spatial, footsteps / other SFX IDs).
  Music is still a placeholder loop.
- Camo SHADE-flattening / G1≠stan leftovers.
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-01 pack flesh-hit overlay `6ad59c5`)

SFX follow-up on idle KF7 hang `7cd1121` (KEEP). SHA `6ad59c5`.
Body hits queue pack `HIT_BULLET_FLESH` 69 on a second mixer overlay so
the PP7 shot and pack BODY_FALL_C1 are not cut. Same runtime ALBankFile
VADPCM path. Placeholders remain without a pack. Fire≠use last_sfx
contract unchanged (dry=2, door=3).

KEEP `7cd1121` idle KF7 hang, `c92f7bb` pack body-fall, `4883173` pack
gun/dry/door, `9b7b6e6` P0 ammo/fire≠use/walk FB, `e01e97f` spawn FPS.

**1 — hit overlay.** `sfx_bank ready=1 gun_n=12336 dry_n=4752 door_n=8448
fall_n=3904 hit_n=1376`. `sfx_hit last=5 mix_diff=512`. `sfx_fall last=4
mix_diff=512`. `pad_use_no_fire` sfx=3. `dry_fire` mag=0→0 flash=0 act=2
sfx=2 n=22. `pad_fire_no_unlatch` mag=7→6 flash=3 open=0→0 act=1.

**2 — hang / hitch / doors.** Re-measured: `play_spawn` `held=1`
`idle_hang tan=4252`. `play_shoot_before` held=1 tan=5037. `play_shoot_after`
held=0 drop=1. `fire_hitch miss_ms=3.38 hit_ms=3.15 hits=1`. `play_spawn`
spawn_fill dark=56 metal=10501 frame_ms=27.59 (36.2 fps). `long_walk
fb=76037→76025 dark=1447`. `clipdoor_fill dark=53 metal=4446`.
`play_hall_a` dark=0 metal=12898. y=29.12. `die_across add=90 olive=6159`.

Native player/gun/g1/2p-corridor/audio green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- Full ASP HLE still out (music, spatial, footsteps / other SFX IDs).
  Music is still a placeholder loop.
- Camo SHADE-flattening / G1≠stan leftovers.
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-01 idle KF7 hang `7cd1121`)

N64-feel slice on top of pack body-fall overlay `c92f7bb` (KEEP). SHA `7cd1121`.
Living idle/walk/aim with joints parent dump `PchrkalashZ` to Rare
`Switches[3]` (right-wrist GROUP MatrixID0=15). Same hierarchical 4x4
as the fire_standing grip — idle hang was empty. Dead bodies still drop
the floor KF7 (not a held emit). Mutate/restore of the shared drop model
unchanged.

KEEP `c92f7bb` pack body-fall, `4883173` pack gun/dry/door, `9b7b6e6` P0
ammo/fire≠use/walk FB, `1312936` held KF7, `f3414ae` floor KF7,
`e01e97f` spawn FPS.

**1 — idle hang.** `held_emit chr=0 model=184 slot=15`. `play_spawn`
`held=1` `idle_hang tan=4252` (was held=0 empty hands). `play_spawn_wide`
held=1 tan=7572. `play_shoot_before` held=1 tan=5037. `play_aim_look`
held=1; `play_aim_grip` held=1 (fire_standing grip kept). `play_shoot_after`
held=0 drop=1 tan=3275 (dead still drops). `play_hall_a` held=1.

**2 — hitch / doors / walk / ammo.** Re-measured: `fire_hitch miss_ms=3.44
hit_ms=3.14 hits=1`. `play_shoot_after` kills=2 mag 7/14. `pad_fire_no_unlatch`
mag=7→6 flash=3 open=0→0 act=1. `pad_use_no_fire` mag=6→6 flash=0 open=1
sfx=3. `dry_fire` mag=0→0 flash=0 act=2 sfx=2 n=22. `sfx_bank ready=1
gun_n=12336 dry_n=4752 door_n=8448 fall_n=3904`. `play_spawn` spawn_fill
dark=56 metal=10501. `play_hall_a` dark=0 metal=12898. `clipdoor_fill
dark=53 metal=4446` `clipdoor_olive n=34`. `long_walk fb=76037→76025
dark=1447`. A→B hopped=0 y=29.12 r71. `die_across add=90 olive=6159`.
Camo `play_spawn` olive=4084 uniq=18 var=108.2.

Spawn `drawn=69 held=1 headj=1` frame_ms=28.30 (35.3 fps) — `e01e97f`
35ms class kept. y=29.12.

Native player/gun/g1/2p-corridor/audio green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- Full ASP HLE still out (music, spatial, other SFX IDs). Music is
  still a placeholder loop.
- Camo SHADE-flattening / G1≠stan leftovers.
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-01 pack body-fall overlay `c92f7bb`)

SFX follow-up on pack VADPCM gun/dry/door `4883173` (KEEP). SHA `c92f7bb`.
Kill queues
pack `BODY_FALL_C1` 123 on a second mixer voice so the PP7 shot is not
cut. Same runtime ALBankFile VADPCM path. Placeholders remain without a
pack. Fire≠use last_sfx contract unchanged (dry=2, door=3).

KEEP `4883173` pack gun/dry/door, `9b7b6e6` P0 ammo/fire≠use/walk FB,
`fedf44f` door-leaf interiors, `e01e97f` spawn FPS.

**1 — fall overlay.** `sfx_bank ready=1 gun_n=12336 dry_n=4752 door_n=8448
fall_n=3904`. `sfx_fall last=4 mix_diff=512`. `pad_use_no_fire` sfx=3.
`dry_fire` mag=0→0 flash=0 act=2 sfx=2 n=22. `pad_fire_no_unlatch`
mag=7→6 flash=3 open=0→0 act=1.

**2 — hitch / doors / walk.** Re-measured: `fire_hitch miss_ms=3.39
hit_ms=3.23 hits=1`. `play_spawn` spawn_fill dark=38 metal=10782
frame_ms=27.33 (36.6 fps). `long_walk fb=76079→76084 dark=1385`.
`clipdoor_fill dark=53 metal=4446`. y=29.12.

Native player/gun/g1/2p-corridor/audio green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- Full ASP HLE still out (music, spatial, other SFX IDs). Music is
  still a placeholder loop.
- Camo SHADE-flattening / G1≠stan leftovers; idle KF7 empty.
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-01 pack VADPCM gun/dry/door SFX `4883173`)

SFX slice on top of P0 empty-mag / fire≠use / walk FB `9b7b6e6` (KEEP).
SHA `4883173`.
Pack `sfx.ctl`/`sfx.tbl` are still N64 banks (no ASP HLE). Runtime decode
of ALBankFile VADPCM one-shots into host PCM: PP7 `GUN_B2_HEAVY` 107,
empty-mag `EMPTY_GUN_FIRE` 89, door `DOOR_METAL_OPEN` 196. Mixer
placeholders remain when the pack has no banks (CI hashes unchanged).
Not ROM PCM in the tree.

KEEP `9b7b6e6` P0 ammo/fire≠use/walk FB, `fedf44f` door-leaf interiors,
`21cbd0a` portal scale, `30db967` door/camo albedo, `f3414ae` floor KF7,
`6666c32` death-across, `a756d97` pad hits, `7886c41` PP7 rest Rx,
`e01e97f` spawn FPS.

**1 — pack SFX.** `sfx_bank ready=1 gun_n=12336 dry_n=4752 door_n=8448`.
`sfx_pcm gun_e≠dry_e≠door_e` last=3. `pad_use_no_fire` sfx=3.
`dry_fire` mag=0→0 flash=0 act=2 sfx=2 n=22. `pad_fire_no_unlatch`
mag=7→6 flash=3 open=0→0 act=1.

**2 — hitch / doors / walk / gun.** Re-measured: `fire_hitch miss_ms=3.40
hit_ms=3.18 hits=1`. `play_spawn` spawn_fill dark=38 metal=10782.
`play_hall_a` dark=0 metal=12898. `play_clip_door` clipdoor_fill dark=53
metal=4446 `clipdoor_olive n=34`. `long_walk fb=76079→76084 dark=1385`.
`play_shoot_after` kills=2 mag 7/14. `die_across add=90 olive=6159`.
`path_unlatch r8-r7 local=650.7,-1753.0` OK.

Spawn `drawn=68 held=0 headj=1` frame_ms=27.32 (36.6 fps) — `e01e97f`
35ms class kept. y=29.12.

Native player/gun/g1/2p-corridor/audio green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- Full ASP HLE still out (music, spatial, other SFX IDs). Music is
  still a placeholder loop.
- Camo SHADE-flattening / G1≠stan leftovers; idle KF7 empty.
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-01 P0 empty-mag / fire≠use / walk FB / SFX `9b7b6e6`)

P0 playtest slice on top of door-leaf interiors `fedf44f` (KEEP). SHA `9b7b6e6`.
Chris live: HUD PP7 0/0 with click still flashing white + fire beep; Z/Space
both fired and opened doors; walking could leave a permanent black frame
while CSS pew still flashed.

**A — empty mag / starting ammo.** Facility start is PP7 mag 7 / reserve 21.
Rising fire (Z-trig / B) with mag==0 reloads if reserve remains, else dry
click: no mag drain, no SKEL_FLASH, no hitscan. Shell no longer `bang()`s
a fire SFX + CSS pew on every click/Z; pew only when C flash_frames rises.

**B — fire ≠ use.** N64 Facility 1P map:
- click / gamepad B or LT/RT → CONT_G 0x2000 fire (B / Z-trig)
- Z / Space / gamepad A → CONT_A 0x8000 use door (A)
- WASD → stick, mouse → look, Shift → run
Use does not fire. Fire does not unlatch. 2P corridor tape tick 9 is A.

**C — walk black frame.** wasm `ALLOW_MEMORY_GROWTH` detached HEAPU8, so
HUD i32 and G1 blit read zeros (PP7 0/0 + black canvas) while C still
ticked. `liveHeapU8` rebinds from `wasmMemory`. Stage draw keeps the last
good room if the eye leaves the walked set.

**D — SFX.** Pack `sfx.ctl`/`sfx.tbl` are N64 banks (no ASP HLE yet). Mixer
placeholders: shot = noise crack + 140 Hz thud; dry = 2.1 kHz click; door
= 78 Hz thud. Queued from C on shot / dry / successful A-use. Not ROM PCM.

KEEP `fedf44f` door-leaf interiors, `21cbd0a` portal scale, `30db967`
door/camo albedo, `f3414ae` floor KF7, `6666c32` death-across, `a756d97`
pad hits, `7886c41` PP7 rest Rx, `e01e97f` spawn FPS.

**1 — ammo / fire / use.** `play_spawn` mag=7/21. `pad_fire_no_unlatch`
mag=7→6 flash=3 open=0→0 act=1. `pad_use_no_fire` mag=6→6 flash=0 open=1
sfx=3. `dry_fire` mag=0→0 flash=0 act=2 sfx=2 n=22. `play_shoot_after`
kills=2 mag 7/14.

**2 — walk / hitch / doors.** `long_walk fb=76079→76084 dark=1385` (not a
full-FB black). `fire_hitch miss_ms=3.53 hit_ms=3.29 hits=1`. `play_spawn`
spawn_fill dark=38 metal=10782. `play_hall_a` dark=0 metal=12898.
`play_clip_door` clipdoor_fill dark=53 metal=4446 `clipdoor_olive n=34`.
`path_unlatch r8-r7 local=650.7,-1753.0` OK.

Spawn `drawn=68 held=0 headj=1` frame_ms=27.33 (36.6 fps) — `e01e97f`
35ms class kept. y=29.12.

Native player/gun/g1/2p-corridor/audio green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- Pack SFX banks not HLE'd (placeholders only).
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-01 door-leaf interiors `fedf44f`)

N64-feel slice on top of portal geom in G1/stan space `21cbd0a` (KEEP). SHA `fedf44f`.
Fitted path/alcove/cutout faces were a single SETTEX 685 quad, so sealed
openings read as a stretched metal smear. Keep the 685 backing (fills
the FRAME hole, dark≈0) and add 686/687 recessed panels plus 706 handles;
wide openings (r71 alcove 640) get two leaves. Jamb still belongs to G1.

KEEP `21cbd0a` G1/stan portal scale, `30db967` door/camo albedo,
`f3414ae` floor KF7, `6666c32` death-rest across look, `a32ed6a`
brown door fills, `a106ce5` clip-door fill, `fecd44a` sealed faces,
`e9247e9` portal cull, `e01e97f` spawn FPS.

**1 — door-leaf interiors.** `play_spawn` spawn_fill dark=38 metal=10782
area=13376. `play_hall_a` dark=0 metal=12898. `play_clip_door`
clipdoor_fill dark=53 metal=4446 area=5120 `clipdoor_olive n=34`
`walked n=1 11`. Path openings still `r8-r7 local=650.7,-1753.0 w=265.2`.
`path_unlatch` / `wide_door_side` / `hinge_park` wide+narrow /
`path_close_swing` OK.

**2 — hitch / jump / Hor+ / heads / gun / death.** Re-measured:
`fire_hitch miss_ms=3.42 hit_ms=3.18 hits=1`; `play_shoot_after`
kills=2 mag 7/14 `held=0` `drop=1`. `die_across add=90 olive=6159`
`viewgun_lr play_spawn n=13525` `play_shoot_after_down n=2634
top_r=1238`. A→B hopped=0 y=29.12 r71; `clipdoor hunt teleports=0`.
Camo `play_spawn` olive=4084 uniq=18 var=108.2; `play_hall_a`
olive=5037 uniq=30 var=95.6. Spawn `drawn=68 held=0 headj=1`
frame_ms=27.51 (36.3 fps unopt) / `--bench` frame_ms=27.52 (36.3 fps)
— `e01e97f` 35ms class kept.

Native player/gun/g1 green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-01 portal geom in G1/stan space `21cbd0a`)

N64-feel slice on top of door/camo SETTEX albedo `30db967` (KEEP). SHA `21cbd0a`.
Rare portal quads lived in unscaled room.pos while G1 draw, stan tiles,
and pad props already `*inv` (Facility 1/1.20648). Fitted door faces
sat ~20% long of the G1 hole. Stored portal pos/width/tall now scale
once after inv is known; `bind_path_openings` / vis-cull use that world
(no second `*inv`). doorlike stays the unscaled 80-450 band. r71
alcove stamp 640 and G1 cutouts already scaled — unchanged.

KEEP `30db967` door/camo albedo, `f3414ae` floor KF7, `6666c32`
death-rest across look, `a32ed6a` brown door fills, `a106ce5`
clip-door fill, `fecd44a` sealed faces, `e9247e9` portal cull,
`e01e97f` spawn FPS.

**1 — G1≠stan door faces.** Path openings now in player-local:
`r8-r7 local=650.7,-1753.0 w=265.2` (was unscaled ~744/-2133 w=320).
`path_unlatch` / `wide_door_side` / `hinge_park` wide+narrow /
`path_close_swing` OK. `play_spawn` spawn_fill dark=5 metal=11143
area=13376. `play_hall_a` dark=0 metal=13376. `play_clip_door`
clipdoor_fill dark=45 metal=4568 area=5120 `clipdoor_olive n=34`
`walked n=1 11`.

**2 — hitch / jump / Hor+ / heads / gun / death.** Re-measured:
`fire_hitch miss_ms=3.39 hit_ms=3.17 hits=1`; `play_shoot_after`
kills=2 mag 7/14 `held=0` `drop=1`. `die_across add=90 olive=6159`
`viewgun_lr play_spawn n=13525` `play_shoot_after_down n=2634
top_r=1238`. A→B hopped=0 y=29.12 r71; `clipdoor hunt teleports=0`.
Camo `play_spawn` olive=4084 uniq=18 var=108.2; `play_hall_a`
olive=5037 uniq=30 var=95.6. Spawn `drawn=68 held=0 headj=1`
frame_ms=25.81 (38.7 fps unopt) / `--bench` frame_ms=25.81 (38.7 fps)
— `e01e97f` 35ms class kept.

Native player/gun/g1 green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- Some door-leaf interiors still a fill quad vs a full N64 FRAME with
  handles.
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-01 door/camo SETTEX albedo `30db967`)

N64-feel slice on top of floor KF7 `f3414ae` (KEEP). SHA `30db967`.
skip=pose identity SHADE only covered no_mtx chr verts. Door SETTEX 685
and oliveguard camo still SHADE*TEXEL crushed when Vtx.cn was baked grey
(room leftover / door props). Raster now skips modulate for those ids
(rooms still modulate other textures; greyscale cn=0 path unchanged).
Fills use a fixed +8u rim pad and a 4u toward-camera z-push instead of
1.02×/1.15× oversize. r71 alcove stamp is actually 640 wide (slab_sized
was capped at 450, so the G1≠stan left void stayed rim-black).

KEEP `f3414ae` floor KF7, `6666c32` death-rest across look, `a32ed6a`
brown door fills, `a106ce5` clip-door fill, `fecd44a` sealed faces,
`e9247e9` portal cull, `e01e97f` spawn FPS.

**1 — door faces / camo.** `play_spawn` spawn_fill dark=5 metal=11143
area=13376 (was 1205/7811). `play_hall_a` dark=0 metal=13376 (was 712).
`play_clip_door` clipdoor_fill dark=0 metal=4737 area=5120
`clipdoor_olive n=34` `walked n=1 11`. Camo `play_spawn` olive=4084
uniq=18 var=108.2; `play_hall_a` olive=5037 uniq=30 var=95.6 (not flat).
`door tex albedo full=38400` (SETTEX 685 cn80 off no_mtx).

**2 — hitch / jump / Hor+ / heads / gun / death.** Re-measured:
`fire_hitch miss_ms=3.45 hit_ms=3.21 hits=1`; `play_shoot_after`
kills=2 mag 7/14 `held=0` `drop=1`. `die_across add=90 olive=6159`
`viewgun_lr play_spawn n=13525` `play_shoot_after_down n=2634
top_r=1238`. A→B hopped=0 y=29.12 r71; `clipdoor hunt teleports=0`.
Spawn `drawn=68 held=0 headj=1` frame_ms=26.51 (37.7 fps unopt) /
`--bench` frame_ms=25.80 (38.8 fps) — `e01e97f` 35ms class kept.

Native player/gun/g1 green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- G1≠stan leftovers; some door-leaf interiors still a fill quad vs a
  full N64 FRAME with handles.
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-01 floor KF7 beside corpse `f3414ae`)

N64-feel slice on top of death-rest across look `6666c32` (KEEP). SHA `f3414ae`.
Hall deaths never emitted PchrkalashZ: the spawn-look cone treated the
extra idle as the stall slab (`drop_skip slab`). Stall skip is 80u from
spawn only. Drop sits 48u toward the shooter (inside pickup radius),
skip=pose catalog 0.1 with baked GROUP origin, identity SHADE so the
rifle is a floor pickup beside the fetal rest, not a pad-centered smear
inside the body. Idle hang stays empty; in-box aim still parents the
held KF7.

KEEP `6666c32` death-rest across look, `a32ed6a` brown door fills,
`a756d97` pad hitscan, `7886c41` forward PP7, `1312936` held KF7,
`1207531` fetal death, `e01e97f` spawn FPS.

**1 — floor KF7.** `drop_spawn chr=0 model=184 local=-304.5,-2335.4
pad=-350.0,-2320.0`. `play_shoot_after` `drop=1 drawn=97 held=0`
`drop_floor tan=2614`. `play_shoot_after_down` phi=-40 `drop=1 tan=405`
`die_across add=90 olive=6278 bbox=59,0-204,204` `viewgun_lr n=2634
top_r=1228`. `play_shoot_before` `held=0`; `aim_look have=1 bound=1`
`held=1`; `play_aim_grip` held=1.

**2 — hitch / jump / Hor+ / heads / gun / doors.** Re-measured:
`fire_hitch miss_ms=3.63 hit_ms=3.37 hits=1`; `play_shoot_after`
kills=2 mag 7/14. A→B hopped=0 y=29.12 r71; `clipdoor hunt
teleports=0`; `clipdoor_olive n=34` `walked n=1 11` `clipdoor_fill
dark=0 metal=4737 area=5120`. `play_spawn` spawn_fill dark=1205
metal=7811; `play_hall_a` dark=712 metal=11390. `viewgun_lr play_spawn
n=13525`. Spawn `drawn=68 held=0 headj=1` camo olive=4172 uniq=22
frame_ms=25.07 (39.9 fps unopt) / `--bench` frame_ms=24.68 (40.5 fps)
— `e01e97f` 35ms class kept.

Native player/gun/lockstep/2p-corridor/g1 green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- Camo SHADE-flattening / G1≠stan leftovers; door-leaf rim leftovers.
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-01 death-rest across look `6666c32`)

N64-feel slice on top of door-face brightness `a32ed6a` (KEEP). SHA `6666c32`.
Fetal last frame is recumbent along model +Z. Aiming-guard pad yaw faces
the player, so look-down at −40° was along the body (head in the near
plane). Extra 0/±90 so +Z is most perpendicular to player→pad;
snapshot on first dead emit so walking does not spin it. Floor pin,
tucked rest, and camera-space PP7 unchanged.

KEEP `a32ed6a` brown door fills, `a756d97` pad hitscan, `7886c41`
forward PP7, `a106ce5` clip-door fill, `fecd44a` sealed faces,
`e9247e9` portal cull, `42ba170` camera-space lock, `1207531` fetal
death, `e01e97f` spawn FPS.

**1 — look-down death.** `play_shoot_after_down` phi=-40 `die_across
add=90 olive=6278 bbox=59,0-204,204` `viewgun_lr n=2634 top_r=1228`
(no look-swing). Fetal ymin=-245 `held=0` `headj=2`. Floor KF7 beside
the corpse.

**2 — hitch / jump / Hor+ / heads / gun / doors.** Re-measured:
`fire_hitch miss_ms=3.43 hit_ms=3.18 hits=1`; `play_shoot_after`
kills=2 mag 7/14. `aim_look have=1 bound=1` `held=1` `headj=1`;
`play_aim_grip` held=1. A→B hopped=0 y=29.12 r71; `clipdoor hunt
teleports=0`; `clipdoor_olive n=34` `walked n=1 11` `clipdoor_fill
dark=0 metal=4737 area=5120`. `play_spawn` spawn_fill dark=1205
metal=7811; `play_hall_a` dark=712 metal=11390. `viewgun_lr play_spawn
n=13525`. Spawn `drawn=68 held=0 headj=1` camo olive=4172 uniq=22
frame_ms=25.26 (39.6 fps unopt) / `--bench` frame_ms=25.12 (39.8 fps)
— `e01e97f` 35ms class kept.

Native player/gun/lockstep/2p-corridor/g1 green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- Idle KF7 empty / death floor KF7 polish; camo SHADE-flattening /
  G1≠stan leftovers; door-leaf rim leftovers.
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-01 door-face brightness `a32ed6a`)

N64-feel slice on top of pad-cylinder hitscan `a756d97` (KEEP). SHA `a32ed6a`.
Chris live: brown door fills looked like black slabs / strange panels.
SETTEX 685 verts were shaded (118,112,98), so shade×texel crushed the
door tile to a near-black rectangle. Full-bright verts let 685 show as
a brown panel. Path/cutout oversize 1.15×/1.12× read as oversized
slabs; 1.02× still covers the G1 rim. r71 alcove stamp stays 640×360
so the mesh gap does not reopen. Closed-door portal cull unchanged
(`e9247e9`).

KEEP `a756d97` pad hitscan, `7886c41` forward PP7, `a106ce5` clip-door
fill, `fecd44a` sealed faces, `e9247e9` portal cull, `42ba170`
camera-space lock, `b1a9201` setup-chr hitscan, `e01e97f` spawn FPS.

**1 — door faces.** `play_spawn` spawn_fill dark=1205 metal=7811
area=13376 (was 1222/3619). `play_hall_a` dark=712 metal=11390 (was
3936). `play_clip_door` clipdoor_fill dark=0 metal=4737 area=5120
(was 3546) `clipdoor_olive n=34` `walked n=1 11`. Hunt teleports=0.

**2 — hitch / jump / Hor+ / heads / gun.** Re-measured: `fire_hitch
miss_ms=3.38 hit_ms=3.19 hits=1`; `play_shoot_after` kills=2 mag 7/14.
`viewgun_lr play_spawn n=13525`. Spawn `drawn=68 held=0 headj=1`
frame_ms=25.31 (39.5 fps unopt) / `--bench` frame_ms=25.41 (39.3 fps)
— `e01e97f` 35ms class kept.

Native player/gun/g1 green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- Look-down death still awkward; idle KF7 empty / death floor KF7 polish;
  camo SHADE-flattening / G1≠stan leftovers.
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

---

## STATUS (2026-09-01 walk FPS / wasm stack `ff37828`)

P0-1 Chris live: after a short walk Chrome dropped near 1 FPS and froze the
tab. `e01e97f` range-cull still held natively (spawn ~27ms). Cause:
`port_stage_draw` put `G1RoomDl passes[536]` + room pos (~85KB wasm32) on
the stack; first-room cutout collect adds ~40KB. wasm default stack is
64KB. Overflow corrupts the heap, detaches HEAPU8 (live HUD PP7 0/0),
grows memory, and freezes the tab after walked rooms / mallocs. Native
macOS 8MB stack hid it.

Static the draw / cutout / prop-sort arrays. Link wasm `-sSTACK_SIZE=1MB`.
Gate per-frame `head_joint` / `held_emit` printf (once, same as
`walk_step`). KEEP `e01e97f` 380u cull, `9b7b6e6` HEAPU8 rebind,
door-jump `102237e`.

**1 — spawn / walk FPS.** `play_spawn` frame_ms=27.58 (36.3 fps) drawn=71
seen=2 skip_range=1 skip_leaf=22 mag=7/21 settex=258 ok=258 miss=0.
`long_walk` frame_ms=30.25 fb=76037→76025 dark=1447. `long_walk_hall`
(Chris door pose −348,−2117) frame_ms=45.54 (22 fps) — 35ms class, not
500–1000ms. `door_jump` frame_ms=33.82. y=29.12.

**2 — hitch / doors.** Re-measured: `clipdoor_fill dark=53 metal=4446`
`clipdoor_olive n=34` `walked n=1 11` hunt teleports=0. Real stair still
`-244,-2098` eye 348.2. `play_spawn` spawn_fill dark=56 metal=10501
held=1 headj=1.

Native player/gun/lockstep/2p-corridor/g1 green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- Door faces still wrong (P0-2 SETTEX 685 panels vs stretched face).
- Guards / player still clip through walls (P0-3).
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-01 door 4-panel leaf `c816f7c`)

P0-2 Chris/Mihok live: sealed doors were a stretched mauve face, not N64
brown metal panels. Cause: fitted path/alcove/cutout faces used one SETTEX
685 backing tiled across the whole portal. Image 685 is the *top* Pgas
panel and contains two vertical handle bars — stretching that tile made
the face smear. Retail `Pgas_plant_met1_do1` is a solid 4-row leaf (685
top, then 686/687/688 ribbed plates; 706 is ±X thickness, not a face
handle). Fitted slabs now match that layout: one 32×33 copy per panel,
mirror wrap, two columns when hw≥160. KEEP door-jump `102237e`, wasm-stack
FPS `ff37828`, albedo `30db967`/`34e7bb9`.

**1 — door faces.** Chris pose `play_door_chris1` (−139,−2337 θ260) is
ribbed 685–688 panels, not a face. `play_spawn` spawn_fill dark=28
metal=10541 area=13376. `play_hall_a` dark=8 metal=12463. `play_clip_door`
clipdoor_fill dark=45 metal=4513 area=5120 `clipdoor_olive n=34`
`walked n=1 11` hunt teleports=0.

**2 — hitch / FPS.** Re-measured: `play_spawn` frame_ms=26.43 (37.8 fps)
drawn=71 seen=2 skip_range=1 skip_leaf=22 mag=7/21. `long_walk`
frame_ms=28.99. `long_walk_hall` (−348,−2117) frame_ms=45.21 — 35ms class
kept. `door_jump` frame_ms=31.29 alcove pinned +Z. y=29.12.

Native player/gun/lockstep/2p-corridor/g1 green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- Mihok pose (−161,−2382 θ290) still has a left-side black void (one-sided
  r71 alcove / missing neighbor). Not a 685 face smear.
- Guards / player still clip through walls (P0-3). chris2 still shows a
  body in the far corner.
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-01 living-guard slab push `ba5a817`)

P0-3 slice on top of 4-panel leaves `c816f7c` (KEEP). SHA `ba5a817`.
Pushed to `origin/main`, Hetzner `/home/grok/GoldenEye` pull + `make -C
native wasm` + `silveriris-vite` restart. Live wasm is this SHA.

The r71 alcove is a visual slab, not a stan door, so extra-idle at
−350,−2320 sat its skip=pose AABB in the left G1 void (Mihok: jammed
inside the left spawn-hall wall, los=1 shoot-through, W stuck at
x=−219). Emit and viscyl now push 80u off path/alcove/cutout faces the
same way stan doors already do. Pad stays for hitscan. KEEP door-jump
`102237e`, wasm-stack FPS `ff37828`, 4-panel `c816f7c`.

**1 — extra-idle off the leaf.** `hallwalk idle gi=0 pad=-350.0,-2320.0
vis=-347.9,-2345.9 r=115.0 aabb=-403.1,-2337.1..-277.4,-2271.7 block=0`.
`play_spawn` / `play_hall_a` / `play_door_chris1` show the body in the
hall beside the ribbed leaf, not a G1-void intersection. `play_clip_door`
clipdoor_fill dark=45 metal=4513 area=5120 `clipdoor_olive n=34`
`walked n=1 11` hunt teleports=0. `spawn_fill play_spawn dark=20
metal=12039 area=13376`.

**2 — hitch / FPS.** Re-measured: `play_spawn` frame_ms=26.18 (38.2 fps)
drawn=71 seen=2 skip_range=1 skip_leaf=22 mag=7/21. `long_walk`
frame_ms=28.84. `long_walk_hall` (−348,−2117) frame_ms=45.13 — 35ms class
kept. `door_jump` frame_ms=31.56 alcove pinned +Z. y=29.12.

Native player/gun/lockstep/2p-corridor/g1 green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- P0-2 live match after hard-refresh still required (Mihok ~10:35pm ET
  was pre-`c816f7c`). Harness spawn/hall/clipdoor leaves are ribbed
  685–688 metal, not a stretched 685 face. Mihok pose (−161,−2382 θ290)
  is an extra-idle close-up (`dark=321`), not a 30% black wedge in
  harness; one-sided r71 alcove / missing neighbor still open.
- P0-3 live match still required (Mihok ~10:35pm ET was pre-`ba5a817`).
  viscyl r=115 still overlaps the 80u slab push; `door_jump_249` can
  still read as camo on the leaf. chris2 far-corner body remains. Do not
  close P0-2/P0-3 without Mihok/live match.
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-01 viscyl slab push `dfde794`)

P0-3 follow-on to `ba5a817` (KEEP). SHA `dfde794`. Slab push used 80u while viscyl is
clamped to `PORT_VIS_RMIN` 115u, so the extra-idle still overlapped the
r71 leaf (`door_jump_249` camo on the door). Emit + viscyl now push
115u. Extra-idle skips camera-space G1 leaf push so the mesh does not
slide toward the look. Pad stays for hitscan. KEEP door-jump `102237e`,
4-panel `c816f7c`, wasm-stack FPS `ff37828`.

**1 — extra-idle.** `play_spawn` / `play_door_chris1` / `play_door_mihok`
show the body in the hall with a gap from the ribbed leaf.
`door_jump_249` is metal panels + guard to the right, not camo on the
face. `mihok_block` clip_step d=12.0 (W at −219,−2093 θ270 is not
stuck). `play_clip_door` clipdoor_fill dark=45 metal=4513 `olive n=34`
hunt teleports=0. `spawn_fill play_spawn dark=5 metal=12050`.

**2 — hitch / FPS.** `play_spawn` frame_ms=15.01 drawn=71 seen=2
skip_range=1 skip_leaf=22 mag=7/21. `long_walk` frame_ms=17.64.
`long_walk_hall` (−348,−2117) frame_ms=34.63 — 35ms class kept.
`door_jump` frame_ms=21.37 alcove pinned +Z. y=29.12.

Native player/gun/lockstep/2p-corridor/g1 green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- P0-2/P0-3 live match after hard-refresh still required (Mihok ~10:35pm
  ET was pre-`c816f7c`/`ba5a817`). Harness doors are ribbed 685–688;
  Mihok pose dark=221 not a 30% wedge. chris2 far-corner body remains.
  Alcove stamp is still player-left (door-jump KEEP), so extra-idle vis
  still shifts with camera along that leaf.
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-02 retail Pgas leaf + SHADE*TEXEL `6377093`)

P0-2: Mihok/Chris live after `c816f7c` was still a mauve/pink horizontally
ribbed slab, not N64 brown metal with handles. SHA `6377093`. The 4-quad G1DL used
SETTEX 685–688 at cn=255 and `keep_albedo`, so the dusty-rose CI8 tiles
drew unshaded. Image 685 *is* the handle-bar panel; 686–688 are the
ribbed plates — stretching them full-bright made a shutter. Retail
`Pgas_plant_met1_do1` is a solid 96-vert leaf (front+back, 706 ±X
thickness, Rare grey cn 119–202). Fitted path/alcove/cutout faces now
copy that P*Z, scale xyz to the Rare portal, and SHADE*TEXEL. Synthetic
packs keep a 4-row G1DL with the same grey cn. KEEP door-jump `102237e`,
viscyl `dfde794`, wasm-stack FPS `ff37828`.

**1 — door faces.** `play_clip_door` is brown metal 4-panel leaves with
685 handle bars (not a mauve shutter). `clipdoor_fill dark=43 metal=4185
area=5120 mauve=101` `clipdoor_olive n=34` hunt teleports=0.
`spawn_fill play_spawn dark=0 metal=10547 area=13376 mauve=0`.
`play_hall_a` dark=5 metal=10077 mauve=0.

**2 — hitch / FPS / clip.** `play_spawn` frame_ms=17.27 drawn=71 seen=2
skip_range=1 skip_leaf=22 mag=7/21. `long_walk` frame_ms=19.59.
`long_walk_hall` (−348,−2117) frame_ms=35.28 — 35ms class kept.
`door_jump` frame_ms=25.46 alcove pinned +Z. `mihok_block` clip_step
d=12.0. y=29.12.

Native player/gun/lockstep/2p-corridor/g1 green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.
`door tex shade mid=38400 full=0` (cn80 × SETTEX 685).

**Remaining holes**

- P0-2 live match after hard-refresh still required (Mihok ~23:43 ET was
  pre-this SHA). Harness clip-door is brown panels + handles; spawn
  alcove is still a 640-wide glancing leaf (covers the G1≠stan void).
- P0-3 live match still required. chris2 far-corner detached head
  remains. Mihok pose (−161,−2382 θ290) left black void (one-sided r71
  alcove / missing neighbor) is not a 685 face smear. Do not close
  P0-2/P0-3 without Mihok/live match.
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-02 spawn-pinned alcove + joint door radius `07e6b41`)

P0-2/P0-3 slice on top of retail Pgas SHADE*TEXEL `6377093` (KEEP). SHA `07e6b41`.
Pushed to `origin/main`, Hetzner `/home/grok/GoldenEye` pull + `make -C
native wasm` + `silveriris-vite` restart. Live wasm is this SHA.

The r71 640-wide alcove was player-relative, so walking to chris2 / rotating at
−219 glued a garage-door leaf to the camera (door-jump KEEP was yaw-only).
Stamp is now spawn-left, 280-wide (G1 cutout size), and only while Bond is
within 280u of spawn. Joint living bodies used the skip=pose 160u door
cylinder, which shoved chris2 guard 37 175u into the far corner (detached
hat). Radius 40u; same-room pads skip camera-space G1 leaf push (extra-idle
already did). KEEP door-jump `102237e`, viscyl `dfde794`, SHADE*TEXEL
`6377093`, wasm-stack FPS `ff37828`.

**1 — alcove / chris2.** `door_jump` alcove stays spawn-left (−89.5,−2274
yaw 180, spawn_d=0,+116) across 219–279; player_d=129.5,90.3 (did not
follow). `play_door_chris2` alcove none; gi=37 vis dpad=22.5 (was 175u);
full standing body, not a hat. `play_spawn` spawn_fill dark=55 metal=7175
area=13376 mauve=203. `play_hall_a` dark=17 metal=10150 mauve=536.
`play_clip_door` clipdoor_fill dark=43 metal=4185 mauve=101 `olive n=34`
hunt teleports=0.

**2 — hitch / FPS / clip.** `play_spawn` frame_ms=14.41 drawn=71 seen=2
skip_range=1 skip_leaf=22 mag=7/21. `long_walk` frame_ms=16.83.
`long_walk_hall` (−348,−2117) frame_ms=25.05 — 35ms class kept.
`door_jump` frame_ms=18.32. `mihok_block` clip_step d=12.0. y=29.12.

Native player/gun/lockstep/2p-corridor/g1 green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- P0-2 live match after hard-refresh still required (Mihok ~23:43 ET was
  pre-`6377093`). Harness clip-door is brown panels + handles; spawn
  alcove is a 280-wide spawn-left door, not a following 640 leaf.
- P0-3 live match still required. chris2 far-corner is a full standing
  guard on the pad (not a detached head). Player G1≠stan walk-through
  (`play_wall`) and Mihok pose left void / missing neighbor still open.
  Do not close P0-2/P0-3 without Mihok/live match.
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-02 player G1 wall clip + door-sized alcove `4785f0f`)

P0-3/P0-2 slice on top of spawn-pinned alcove `07e6b41` (KEEP). SHA `4785f0f`.
Pushed to `origin/main`, Hetzner `/home/grok/GoldenEye` pull + `make -C
native wasm` + `silveriris-vite` restart. Live wasm is this SHA.

Interior G1 faces that sit inside a walkable tile were stan-only, so
`play_corner` stood 10.6u through a visual wall. `clip_step` / visual xz /
hitscan now push and ray against interior G1 planes (path openings and
different-room portals skipped; neighbor rooms scanned). Fitted leaves
(alcove / cutout / path) also push the player cylinder. Spawn alcove is
180-wide (Pgas aspect), not 280, so door-jump at x=−219 is past the leaf
instead of looking along a garage door. KEEP door-jump `102237e`, viscyl
`dfde794`, SHADE*TEXEL `6377093`, alcove pin `07e6b41`, wasm-stack FPS
`ff37828`.

**1 — clip / alcove.** `play_corner` g1clip push 27u off a 10.6u-through
face. `play_wall_close` blocked=1 on stan skin (G1/stan aligned after
`e21097d`). `play_door_chris2` alcove none; gi=37 vis dpad=22.5; full
standing body. `door_jump` alcove stays spawn-left (−89.5,−2274 yaw 180,
spawn_d=0,+116). `play_spawn` / `play_door_chris1` show 685 handle bars
on the left leaf. `play_clip_door` clipdoor_fill dark=43 metal=4185
mauve=101 `olive n=34` hunt teleports=0. `mihok_block` clip_step d=12.0.

**2 — hitch / FPS.** `play_spawn` frame_ms=11.45 drawn=71 seen=2
skip_range=1 skip_leaf=22 mag=7/21. `long_walk` frame_ms=12.85.
`long_walk_hall` (−348,−2117) frame_ms=13.12 — 35ms class kept.
`door_jump` frame_ms=13.62. y=29.12.

Native player/gun/lockstep/2p-corridor green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- P0-2 live match after hard-refresh still required (Mihok ~01:06 ET was
  `07e6b41`; `4785f0f` narrows the alcove and keeps SHADE*TEXEL handles).
  Do not close without Mihok/live match.
- P0-3 live match still required. `play_wall` pitch −35 still reads as
  ceiling (already on stan skin 30). Mihok pose left void / missing
  neighbor still open. Do not close without Mihok/live match.
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-02 pack male-yelp + Bond-hurt overlay `6098d45`)

N64-feel SFX on top of G1 wall clip `4785f0f` (KEEP). SHA `6098d45`.
Guard body hits queue pack `GET_HIT_MALE0` 134 on a third mixer overlay
(voice) so PP7 / flesh-hit / BODY_FALL_C1 are not cut. Bond damage
(PvP hitscan and guard fire, including armour absorb) queues pack
`BOND_GET_HIT1` 68 on the same channel. Same runtime ALBankFile VADPCM
path. Placeholders remain without a pack. Fire≠use last_sfx contract
unchanged (play_gun after hitscan; dry=2, door=3). GET_HIT_MALE1–24
cycle not this slice (one dump ID, not game RNG).

KEEP `4785f0f` G1 clip+alcove180, `07e6b41` alcove/joint, `6377093`
Pgas SHADE*TEXEL, `ff37828` wasm-stack FPS, `4f05c3f` rifle-cock,
`6ad59c5` flesh-hit.

**1 — yelp / hurt overlay.** `sfx_bank ready=1 … reload_n=10320
yelp_n=1552 hurt_n=10544`. `sfx_yelp last=13 mix_diff=512`. `sfx_hurt
last=14 mix_diff=512`. `sfx_yelp_fall last=4 mix_diff=470` (voice does
not replace fall). `play_hall_a` hp 8→6 gfire=2 sfx=14 (Bond hurt on
guard fire). `pad_use_no_fire` sfx=3. `dry_fire` mag=0→0 flash=0 act=2
sfx=2. `pad_fire_no_unlatch` mag=7→6 flash=3 open=0→0 act=1 sfx=1.

**2 — hitch / doors / clip.** Re-measured: `play_spawn` spawn_fill
dark=55 metal=7175 area=13376 mauve=203 frame_ms=12.25 (81.6 fps)
drawn=71 seen=2 skip_range=1 skip_leaf=22 mag=7/21 held=1 headj=1.
`long_walk` frame_ms=13.56. `long_walk_hall` (−348,−2117) frame_ms=14.31
— 35ms class kept. `door_jump` frame_ms=14.22 alcove spawn-left
(−89.5,−2274 yaw 180, spawn_d=0,+116). `chris2 vis gi=37 dpad=22.5`.
`play_clip_door` clipdoor_fill dark=43 metal=4185 mauve=101 `olive n=34`
hunt teleports=0. `play_corner` g1clip push 27u. `play_wall_close`
blocked=1. `fire_hitch miss_ms=6.01 hit_ms=5.80 hits=1`. `play_shoot_after`
kills=2 mag 7/14 held=0 drop=1. y=29.12.

Native player/gun/lockstep/2p-corridor/g1/audio green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- P0-2 live match after hard-refresh still required (Mihok ~01:06 ET was
  `07e6b41`; live wasm after this push is `6098d45` on `4785f0f` clip +
  SHADE*TEXEL handles). Do not close without Mihok/live match.
- P0-3 live match still required. `play_wall` pitch −35 still reads as
  ceiling (already on stan skin 30). Mihok pose left void / missing
  neighbor still open. Do not close without Mihok/live match.
- GET_HIT_MALE1–24 cycle still one ID (`GET_HIT_MALE0`). Full ASP HLE
  still out (music, spatial, footsteps / other SFX IDs).
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-02 pack GET_HIT_MALE0–24 yelp cycle `83e2b16`)

N64-feel SFX on top of male-yelp overlay `6098d45` (KEEP). SHA `83e2b16`.
Guard body hits cycle pack `GET_HIT_MALE0`–`24` (sfx.ctl 134–158) like
Rare `male_guard_yelp_counter` (wrap at 25). Not game RNG. First yelp
stays MALE0 so the voice overlay contract is unchanged. Bond hurt still
`BOND_GET_HIT1` 68. Placeholders remain without a pack. Fire≠use last_sfx
contract unchanged (play_gun after hitscan; dry=2, door=3).

KEEP `6098d45` yelp/hurt overlay, `4785f0f` G1 clip+alcove180, `07e6b41`
alcove/joint, `6377093` Pgas SHADE*TEXEL, `ff37828` wasm-stack FPS,
`4f05c3f` rifle-cock, `6ad59c5` flesh-hit.

**1 — yelp cycle.** `sfx_bank ready=1 … reload_n=10320 yelp_n=1552
hurt_n=10544 yelp_vars=25`. `sfx_yelp last=13 mix_diff=512`. `sfx_hurt
last=14 mix_diff=512`. `sfx_yelp_fall last=4 mix_diff=464` (voice does
not replace fall). `sfx_yelp_cycle vars=25 e0=22523046412 e1=22267240840
mix_diff=512 wrap_diff=0 last=13`. `play_hall_a` hp 8→6 gfire=2 sfx=14
(Bond hurt on guard fire). `pad_use_no_fire` sfx=3. `dry_fire` mag=0→0
flash=0 act=2 sfx=2. `pad_fire_no_unlatch` mag=7→6 flash=3 open=0→0
act=1 sfx=1.

**2 — hitch / doors / clip.** Re-measured: `play_spawn` spawn_fill
dark=55 metal=7175 area=13376 mauve=203 frame_ms=11.91 (83.9 fps)
drawn=71 seen=2 skip_range=1 skip_leaf=22 mag=7/21 held=1 headj=1.
`long_walk` frame_ms=13.19. `long_walk_hall` (−348,−2117) frame_ms=13.90
— 35ms class kept. `door_jump` frame_ms=14.20 alcove spawn-left
(−89.5,−2274 yaw 180, spawn_d=0,+116). `chris2 vis gi=37 dpad=22.5`.
`play_clip_door` clipdoor_fill dark=43 metal=4185 mauve=101 `olive n=34`
hunt teleports=0. `play_corner` g1clip push 27u. `play_wall_close`
blocked=1. `fire_hitch miss_ms=5.72 hit_ms=5.45 hits=1`. `play_shoot_after`
kills=2 mag 7/14 held=0 drop=1. y=29.12.

Native player/gun/lockstep/2p-corridor/g1/audio green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- P0-2 live match after hard-refresh still required (Mihok ~01:06 ET was
  `07e6b41`; live wasm after this push is `83e2b16` on `4785f0f` clip +
  SHADE*TEXEL handles). Do not close without Mihok/live match.
- P0-3 live match still required. `play_wall` pitch −35 still reads as
  ceiling (already on stan skin 30). Mihok pose left void / missing
  neighbor still open. Do not close without Mihok/live match.
- Full ASP HLE still out (music, spatial, footsteps / other SFX IDs).
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-02 pack BODY_FALL thud cycle `8b2f09e`)

N64-feel SFX on top of yelp cycle `83e2b16` (KEEP). SHA `8b2f09e`.
Guard deaths cycle pack `BODY_FALL_C1`–`E3` + `BODY_ROLLOVER` (sfx.ctl
123–133) like Rare `thud_index` / `body_hit_SFX` (wrap at 11). Not game
RNG. First fall stays C1 so the overlay contract is unchanged. C/D/E
groups share a wavetable (ASP pitch still out); C≠D is the first
distinct PCM. Placeholders remain without a pack. Fire≠use last_sfx
contract unchanged (play_gun after hitscan; dry=2, door=3).

KEEP `83e2b16` yelp cycle, `6098d45` yelp/hurt overlay, `4785f0f` G1
clip+alcove180, `07e6b41` alcove/joint, `6377093` Pgas SHADE*TEXEL,
`ff37828` wasm-stack FPS, `4f05c3f` rifle-cock, `6ad59c5` flesh-hit.

**1 — fall cycle.** `sfx_bank ready=1 … reload_n=10320 yelp_n=1552
hurt_n=10544 yelp_vars=25 fall_vars=11`. `sfx_fall last=4 mix_diff=512`.
`sfx_yelp last=13 mix_diff=512`. `sfx_hurt last=14 mix_diff=512`.
`sfx_yelp_fall last=4 mix_diff=464` (voice does not replace fall).
`sfx_fall_cycle vars=11 nC=3904 nD=3792 eC=72772851402 eD=45187614454
mix_diff=4096 wrap_diff=0 last=4`. `play_hall_a` hp 8→6 gfire=2 sfx=14
(Bond hurt on guard fire). `pad_use_no_fire` sfx=3. `dry_fire` mag=0→0
flash=0 act=2 sfx=2. `pad_fire_no_unlatch` mag=7→6 flash=3 open=0→0
act=1 sfx=1.

**2 — hitch / doors / clip.** Re-measured: `play_spawn` spawn_fill
dark=55 metal=7175 area=13376 mauve=203 frame_ms=12.20 (82.0 fps)
drawn=71 seen=2 skip_range=1 skip_leaf=22 mag=7/21 held=1 headj=1.
`long_walk` frame_ms=13.58. `long_walk_hall` (−348,−2117) frame_ms=14.11
— 35ms class kept. `door_jump` frame_ms=14.25 alcove spawn-left
(−89.5,−2274 yaw 180, spawn_d=0,+116). `chris2 vis gi=37 dpad=22.5`.
`play_clip_door` clipdoor_fill dark=43 metal=4185 mauve=101 `olive n=34`
hunt teleports=0. `play_corner` g1clip push 27u. `play_wall_close`
blocked=1. `fire_hitch miss_ms=5.95 hit_ms=5.82 hits=1`. `play_shoot_after`
kills=2 mag 7/14 held=0 drop=1. y=29.12.

Native player/gun/lockstep/2p-corridor/g1/audio green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- P0-2 live match after hard-refresh still required (Mihok ~01:06 ET was
  `07e6b41`; live wasm after this push is `8b2f09e` on `4785f0f` clip +
  SHADE*TEXEL handles). Do not close without Mihok/live match.
- P0-3 live match still required. `play_wall` pitch −35 still reads as
  ceiling (already on stan skin 30). Mihok pose left void / missing
  neighbor still open. Do not close without Mihok/live match.
- Full ASP HLE still out (music, spatial, footsteps / other SFX IDs).
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-02 fitted Pgas skip leftover G_MTX + stall snap `bad9aff`)

P0-2/P0-3 on top of BODY_FALL cycle `8b2f09e` (KEEP). SHA `bad9aff`.
Pushed to `origin/main`, Hetzner `/home/grok/GoldenEye` pull + `make -C
native wasm` + `silveriris-vite` restart. Live wasm is this SHA.

Mihok 0143/0205/0221 after 4785f0f/6098d45/83e2b16: mauve/grey horizontal
ribbed slabs at HUD x=-219 z=-2364, extra-idle floating head in the leaf.
Retail Pgas GDL G_MTX 0x03 LOAD of leftover room seg 3 replaced the portal
look-at (native had seg 3 unbound so the LOAD was a no-op). Fitted copies
now NOP that command; interpret unbinds seg 3 and skips the LOAD when
seg4 is the vertex bank. Pad 167 can sit on a stall sliver if eye_y hits
first; Facility spawn snaps along look onto the hallway (~-89,-2390)
before keeping pad xz. Preload SETTEX 685-688/706. KEEP door-jump
`102237e`, viscyl `dfde794`, SHADE*TEXEL `6377093`, alcove pin `07e6b41`,
G1 clip `4785f0f`, wasm-stack FPS `ff37828`.

**1 — doors / spawn.** `playtest spawn xz=-89.5,-2390.0 y=29.1
retail_slab=1`. Not the stall jam. `play_door_live` (−219,−2364 θ270) is
brown metal 685 handles, extra-idle d=138 vis=-347.9,-2313.2
`near_living=0`. `spawn_fill play_spawn dark=55 metal=7175 area=13376
mauve=203`. `play_door_live` dark=54 metal=5470 mauve=133.
`play_hall_a` dark=17 metal=10150 mauve=536. `play_clip_door`
clipdoor_fill dark=43 metal=4185 mauve=101 `olive n=34` hunt teleports=0.
`chris2 vis gi=37 dpad=22.5`. `door_jump` alcove spawn-left
(−89.5,−2274 yaw 180).

**2 — hitch / FPS / clip.** Re-measured (touched interpret): `play_spawn`
frame_ms=13.42 (74.5 fps) drawn=71 seen=2 skip_range=1 skip_leaf=22
mag=7/21 held=1 headj=1. `long_walk` frame_ms=14.60. `long_walk_hall`
(−348,−2117) frame_ms=15.70 — 35ms class kept. `door_jump` frame_ms=15.40.
`mihok_block` clip_step d=12.0. `play_corner` g1clip push 27u.
`play_wall_close` blocked. `fire_hitch miss_ms=6.77 hit_ms=6.70 hits=1`.
y=29.12.

Native player/gun/lockstep/g1 green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.
`door tex shade mid=38400 full=0` (cn80 × SETTEX 685).

**Remaining holes**

- P0-2 live match after hard-refresh still required (Mihok ~02:21 ET was
  `83e2b16`; live wasm after this push is `bad9aff` skip G_MTX + hallway
  snap). Do not close without Mihok/live match.
- P0-3 live match still required. Spawn harness is −89.5,−2390 not the
  −219 stall sliver; extra-idle at play_door_live is in the hall (d=138),
  not a floating head. `play_wall` pitch −35 still reads as ceiling
  (already on stan skin 30). Mihok pose left void / missing neighbor
  still open. Do not close without Mihok/live match.
- Full ASP HLE still out (music, spatial, footsteps / other SFX IDs).
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-02 live rAF stall clip `b3fdadf`)

P0-2/P0-3 on top of fitted Pgas skip G_MTX `bad9aff` (KEEP). SHA `b3fdadf`.
Pushed to `origin/main`, Hetzner `/home/grok/GoldenEye` pull + `make -C
native wasm` + `silveriris-vite` restart. Live wasm is this SHA.

Mihok 0143/0205/0221/0235/0304 after hard-refresh of `bad9aff`: HUD still
x=-219 z=-2364 mauve 4-band + floating head. Harness never sim-ticked
after load so spawn stayed −89.5,−2390 with brown Pgas; live rAF ticks.
Tile 147's unlinked south edge made every zero-stick clip look trapped;
centroid snap walked along that mega-triangle into the stall (camera
inside the leaf). A 400u G1 cutout collision plane also shoved −X.
clip_step now pushes off the unlinked edge along its inward normal.
Fitted-slab collision half-w is a door (100u), not the visual oversize.
`play_spawn idle40` stays x>-170. KEEP door-jump `102237e`, viscyl
`dfde794`, SHADE*TEXEL `6377093`, alcove pin `07e6b41`, G1 clip
`4785f0f`, wasm-stack FPS `ff37828`, skip G_MTX `bad9aff`.

**1 — spawn / doors.** `playtest spawn xz=-89.5,-2390.0 y=29.1
retail_slab=1`. `play_spawn idle40 xz=-98.5,-2358.4 d=-9.0,31.6` (not
x=-219). `spawn_fill play_spawn dark=55 metal=7175 area=13376 mauve=203`.
`play_door_live` near_living=0. `play_clip_door` clipdoor_fill dark=43
metal=4185 mauve=101 `olive n=34` hunt teleports=0. `chris2 vis gi=37
dpad=22.2`. `door_jump` alcove spawn-left (−89.5,−2274 yaw 180).

**2 — hitch / FPS / clip.** `play_spawn` frame_ms=29.78 drawn=71 seen=2
skip_range=1 skip_leaf=22 mag=7/21 held=1 headj=1. `long_walk`
frame_ms=29.72. `long_walk_hall` (−348,−2117) frame_ms=40.39 — 35ms class
kept (this box; live rAF KEEP `ff37828`). `door_jump` frame_ms=31.93.
`mihok_block` clip_step d=12.0. `play_corner` g1clip push 27u.
`play_wall_close` blocked. `fire_hitch miss_ms=23.62 hit_ms=23.26 hits=1`.
y=29.12.

Native player/gun/lockstep/g1 green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- P0-2 live match after hard-refresh still required (Mihok 0304 was
  `bad9aff`; live wasm after this push is `b3fdadf` idle-tick stall
  clip). Harness spawn is brown 685 handles at −89.5,−2390. Do not close
  without Mihok/live match.
- P0-3 live match still required. Idle ticks no longer walk to x=-219;
  extra-idle at play_door_live is in the hall. `play_wall` pitch −35 still
  reads as ceiling (already on stan skin 30). Mihok pose left void /
  missing neighbor still open. Do not close without Mihok/live match.
- Full ASP HLE still out (music, spatial, footsteps / other SFX IDs).
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-02 look-at inward-edge `9cadb53`)

P0-1/P0-2/P0-3 on top of live rAF stall clip `b3fdadf` (KEEP). SHA `9cadb53`.
Pushed to `origin/main`, Hetzner `/home/grok/GoldenEye` pull + `make -C
native wasm` + `silveriris-vite` restart. Live wasm is this SHA.

Mihok 0345 after `b3fdadf`: clip HUD x=-98.5 z=-2358.4 full body; doors
brown-ish partial; rAF ≈22.5 (sample included walk/stairs). `clip_step`
stayed in the hall, but `port_stan_visual_xz` still pulled the look-at
toward tile 147's centroid (the stall cubicle) whenever spawn sat inside
draw-skin of that unlinked south edge. Camera in the leaf = mauve 4-band
/ no 685 handles, and extra Pgas fill. Look-at now uses the same inward
edge normal as clip. Spawn nudges off the wall at load so the first rAF
frame is not in the skin. KEEP door-jump `102237e`, viscyl `dfde794`,
SHADE*TEXEL `6377093`, alcove pin `07e6b41`, G1 clip `4785f0f`,
wasm-stack FPS `ff37828`, skip G_MTX `bad9aff`, stall clip `b3fdadf`.

**1 — spawn / doors.** `playtest spawn xz=-89.5,-2358.4 y=29.1
retail_slab=1`. `play_spawn visual xz=-114.5,-2358.4 d=-25.0,0.0` (not
x=-219). `play_spawn idle40 xz=-98.5,-2358.4 d=-9.0,0.0` visual
`-125.1,-2358.4`. `spawn_fill play_spawn dark=70 metal=8821 area=13376
mauve=290`. `play_door_live` near_living=0 dark=64 metal=8361 mauve=185.
`play_clip_door` clipdoor_fill dark=43 metal=4185 mauve=101 `olive n=34`
hunt teleports=0. `chris2 vis gi=37 dpad=22.2`. `door_jump` alcove
spawn-left (−89.5,−2242 yaw 180). Harness `play_spawn` / `play_door_chris1`
show brown SETTEX 685 handle bars.

**2 — hitch / FPS / clip.** `play_spawn` frame_ms=29.81 (33.5 fps) drawn=71
seen=2 skip_range=1 skip_leaf=22 mag=7/21 held=1 headj=1. `long_walk`
frame_ms=29.80. `long_walk_hall` (−347,−2116) frame_ms=40.24 — 35ms class
kept (this box; live rAF KEEP `ff37828`; 0345 ≈22.5 matches hall-walk
class, not a freeze). `door_jump` frame_ms=31.28. `mihok_block` clip_step
d=12.0. `play_corner` g1clip push 27u. `play_wall_close` blocked.
`fire_hitch miss_ms=23.70 hit_ms=23.49 hits=1`. y=29.12.

Native player/gun/lockstep/g1/2p-corridor green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- P0-1 live spawn-idle rAF after hard-refresh still required. Harness
  spawn is 33.5 fps / 35ms class; hall walk is ~25 fps. Do not close
  ff37828 re-prove from harness alone.
- P0-2 live match after hard-refresh still required (Mihok 0345 was
  `b3fdadf`; live wasm after this push is `9cadb53` inward look-at).
  Harness spawn is brown 685 handles at −89.5,−2358. Do not close
  without Mihok/live match.
- P0-3 live match still required. Spawn/idle stay x>-170; extra-idle at
  play_door_live is in the hall. `play_wall` pitch −35 still reads as
  ceiling (already on stan skin 30). Mihok pose left void / missing
  neighbor still open. Do not close without Mihok/live match.
- Full ASP HLE still out (music, spatial, footsteps / other SFX IDs).
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-02 look-at cap `2630bd1`)

P0-2/P0-3 on top of look-at inward-edge `9cadb53` (KEEP). SHA `2630bd1`.
Pushed to `origin/main`, Hetzner `/home/grok/GoldenEye` pull + `make -C
native wasm` + `silveriris-vite` restart. Live wasm is this SHA.

Mihok 0414 after `9cadb53`: stall HUD x=-98.5 z=-2358.4 kept; spawn door
brown-ribbed partial; look-left floating head; close approach still a
ribbed slab. `clip_step` stayed in the hall, but uncapped DRAW_SKIN
inward/G1 look-at still shoved the camera −27u −X (idle40 visual −125)
into the stall leaf. Cap the draw offset at 16u (DRAW 46 − WALL 30).
idle40 visual is −114.5 (the 9cadb53 spawn camera), not −125. KEEP
door-jump `102237e`, viscyl `dfde794`, SHADE*TEXEL `6377093`, alcove pin
`07e6b41`, G1 clip `4785f0f`, wasm-stack FPS `ff37828`, skip G_MTX
`bad9aff`, stall clip `b3fdadf`, inward look-at `9cadb53`.

**1 — spawn / doors / head.** `playtest spawn xz=-89.5,-2358.4 y=29.1
retail_slab=1`. `play_spawn visual xz=-105.5,-2358.4 d=-16.0,0.0`.
`play_spawn idle40 xz=-98.5,-2358.4 d=-9.0,0.0` visual `-114.5,-2358.4`
(not x=-219 / visual −125). `play_spawn_idle` / `play_lookleft` (θ257 at
idle40 xz) are brown SETTEX 685 handle bars, extra-idle Chead on the
neck. `spawn_fill play_spawn dark=58 metal=8730 area=13376 mauve=405`.
`play_spawn_idle` dark=70 metal=8821 mauve=290. `play_lookleft` dark=0
metal=11659 mauve=3737. `play_door_live` near_living=0 dark=65 metal=7635
mauve=145. `play_clip_door` clipdoor_fill dark=43 metal=4185 mauve=101
`olive n=34` hunt teleports=0. `chris2 vis gi=37 dpad=22.2`. `door_jump`
alcove spawn-left (−89.5,−2242 yaw 180).

**2 — hitch / FPS / clip.** `play_spawn` frame_ms=29.52 (33.9 fps) drawn=71
seen=2 skip_range=1 skip_leaf=22 mag=7/21 held=1 headj=1. `long_walk`
frame_ms=29.69. `long_walk_hall` (−347,−2116) frame_ms=40.71 — 35ms class
kept (this box; live rAF KEEP `ff37828`). `door_jump` frame_ms=31.45.
`mihok_block` clip_step d=12.0. `play_corner` g1clip push 27u.
`play_wall_close` blocked. `fire_hitch miss_ms=23.98 hit_ms=23.74 hits=1`.
y=29.12.

Native player/gun/lockstep/g1/2p-corridor green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- P0-1 live spawn-idle rAF after hard-refresh still required. Harness
  spawn is 33.9 fps / 35ms class; hall walk is ~25 fps. Do not close
  ff37828 re-prove from harness alone.
- P0-2 live match after hard-refresh still required (Mihok 0414 was
  `9cadb53`; live wasm after this push is `2630bd1` 16u look-at cap).
  Harness idle40 / look-left is brown 685 handles at −98.5,−2358. Close
  approach (−719,−2135) not this slice. Do not close without Mihok/live
  match.
- P0-3 live match still required. Spawn/idle stay x>-170; look-left
  extra-idle is a full body + neck Chead in harness. `play_wall` pitch
  −35 still reads as ceiling (already on stan skin 30). Mihok pose left
  void / missing neighbor still open. Do not close without Mihok/live
  match.
- Full ASP HLE still out (music, spatial, footsteps / other SFX IDs).
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-02 HUD + stall-ward look-at `a8d6c15`)

P0-1/P0-2 on top of look-at cap `2630bd1` (KEEP). SHA `a8d6c15`.
Pushed to `origin/main`, Hetzner `/home/grok/GoldenEye` pull + `make -C
native wasm` + `silveriris-vite` restart. Live wasm is this SHA.

Mihok 0446 after `2630bd1`: stall HUD x=-98.5 z=-2358.4 KEEP; live rAF
≈28.8 (KEEP ff37828); spawn door brown-ribbed, 685 handle bars not
distinct on the live canvas; look-left Chead still detached. Harness
idle40 / look-left already had 685 bars — the 62px HUD `fillRect`
painted over that band, and DRAW_SKIN 46 on tile 147's south edge still
shoved look-at −X (capped 16u) so the leaf was edge-on / profile. Skip
that −X unlinked shove (clip_step WALL_SKIN 30 stays). Stroke HUD text.
Nearest-neighbor blit when the canvas is not 1:1. Harness look-left is
θ263 (Mihok 0446). KEEP door-jump `102237e`, viscyl `dfde794`,
SHADE*TEXEL `6377093`, alcove pin `07e6b41`, G1 clip `4785f0f`,
wasm-stack FPS `ff37828`, skip G_MTX `bad9aff`, stall clip `b3fdadf`,
inward look-at `9cadb53`, 16u cap `2630bd1`.

**1 — spawn / doors / head.** `playtest spawn xz=-89.5,-2358.4 y=29.1
retail_slab=1`. `play_spawn visual xz=-89.5,-2342.4 d=0.0,16.0` (not
−16 −X). `play_spawn idle40 xz=-98.5,-2358.4 d=-9.0,0.0` visual
`-106.2,-2344.4 d=-7.7,14.0` (x>-170). `play_spawn_idle` / `play_lookleft`
(θ263 at idle40 xz) are brown SETTEX 685 handle bars, extra-idle Chead
on the neck. `spawn_fill play_spawn dark=14 metal=9369 area=13376
mauve=1139`. `play_spawn_idle dark=40 metal=9086 mauve=796`.
`play_lookleft dark=6 metal=10893 mauve=2515`. `play_door_live`
near_living=0 dark=77 metal=5546 mauve=129. `chris2 vis gi=37 dpad=22.2`.
`door_jump` alcove spawn-left (−89.5,−2242 yaw 180).

**2 — hitch / FPS / clip.** `play_spawn` frame_ms=50.66 (cold; 19.7 fps)
drawn=71 seen=2 skip_range=1 skip_leaf=22 mag=7/21 held=1 headj=1.
`long_walk` frame_ms=29.73 (33.6 fps). `long_walk_hall` (−347,−2116)
frame_ms=40.47 — 35ms class kept (this box; live rAF KEEP `ff37828`).
`door_jump` frame_ms=31.21. `mihok_block` clip_step d=12.0.
`fire_hitch miss_ms=23.74 hit_ms=23.40 hits=1`. y=29.12.

Native player/g1 playtest green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- P0-1 live spawn-idle rAF after hard-refresh still required. Harness
  long_walk is 33.6 fps / 35ms class; hall walk is ~25 fps. Do not close
  ff37828 re-prove from harness alone.
- P0-2 live match after hard-refresh still required (Mihok 0446 was
  `2630bd1`; live wasm after this push is `a8d6c15` HUD + stall-ward
  look-at skip). Harness idle40 / look-left is brown 685 handles at
  −98.5,−2358 θ263. Close approach (−719,−2135) not this slice. Do not
  close without Mihok/live match.
- P0-3 live match still required. Spawn/idle stay x>-170; look-left
  extra-idle is a full body + neck Chead in harness. `play_wall` pitch
  −35 still reads as ceiling (already on stan skin 30). Mihok pose left
  void / missing neighbor still open. Do not close without Mihok/live
  match.
- Full ASP HLE still out (music, spatial, footsteps / other SFX IDs).
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-02 685 stretch + Chead seat `fe54dba`)

P0-2/P0-3 on top of HUD + stall-ward look-at `a8d6c15` (KEEP). SHA
`fe54dba`. Pushed to `origin/main`, Hetzner `/home/grok/GoldenEye` pull
+ `make -C native wasm` + `silveriris-vite` restart. Live wasm is this
SHA.

Mihok 0542 after `a8d6c15`: stall HUD x=-98.5 z=-2358.4 KEEP; live rAF
≈26.9 (KEEP ff37828); spawn/look-left 685 bars were a thin lintel of
nubs (harness claim ≠ distinct on the 320 canvas); look-left Chead
still a wall gap. Retail Pgas mapped 685 to the top 25% (Y 394..788 of
−787..788). Stretch that band to the top 42% so the two handle bars
read at spawn. Remaining DRAW_SKIN −X from G1/slabs still pulled idle40
look-at −7.7 −X; drop all −X look-at (unlinked skip was not enough;
clip_step WALL_SKIN 30 stays). Slide Chead 48u down the neck Y axis
(copy, not the shared table). KEEP door-jump `102237e`, viscyl
`dfde794`, SHADE*TEXEL `6377093`, alcove pin `07e6b41`, G1 clip
`4785f0f`, wasm-stack FPS `ff37828`, skip G_MTX `bad9aff`, stall clip
`b3fdadf`, inward look-at `9cadb53`, 16u cap `2630bd1`, HUD stroke
`a8d6c15`.

**1 — spawn / doors / head.** `playtest spawn xz=-89.5,-2358.4 y=29.1
retail_slab=1`. `play_spawn visual xz=-89.5,-2342.4 d=0.0,16.0`.
`play_spawn idle40 xz=-98.5,-2358.4 d=-9.0,0.0` visual
`-98.5,-2342.4 d=0.0,16.0` (no −X; x>-170). `play_spawn_idle` /
`play_lookleft` (θ263 at idle40 xz) are brown SETTEX 685 handle bars
(top ~42% of the leaf), extra-idle Chead on the neck. `spawn_fill
play_spawn dark=14 metal=9519 area=13376 mauve=1147`. `play_spawn_idle
dark=18 metal=9478 mauve=1001`. `play_lookleft dark=1 metal=11022
mauve=2715`. `play_door_live` near_living=0 dark=65 metal=7851
mauve=153. `chris2 vis gi=37 dpad=22.2`. `door_jump` alcove spawn-left
(−89.5,−2242 yaw 180).

**2 — hitch / FPS / clip.** `play_spawn` frame_ms=50.97 (cold; 19.6 fps)
drawn=71 seen=2 skip_range=1 skip_leaf=22 mag=7/21 held=1 headj=1.
`long_walk` frame_ms=31.07 (32.2 fps). `long_walk_hall` (−347,−2116)
frame_ms=40.34 — 35ms class kept (this box; live rAF KEEP `ff37828`).
`door_jump` frame_ms=31.41. `mihok_block` clip_step d=12.0.
`fire_hitch miss_ms=23.55 hit_ms=23.46 hits=1`. y=29.12.

Native player/g1 playtest green.

**Remaining holes**

- P0-1 live spawn-idle rAF after hard-refresh still required. Harness
  long_walk is 32.2 fps / 35ms class; hall walk is ~25 fps. Do not close
  ff37828 re-prove from harness alone.
- P0-2 live match after hard-refresh still required (Mihok 0542 was
  `a8d6c15`; live wasm after this push is `fe54dba` 685 stretch + Chead
  seat + no −X look-at). Harness idle40 / look-left is distinct 685
  bars at −98.5,−2358 θ263. Close approach (−719,−2135) not this slice.
  Do not close without Mihok/live match.
- P0-3 live match still required. Spawn/idle stay x>-170; look-left
  extra-idle is a full body + neck Chead in harness. `play_wall` pitch
  −35 still reads as ceiling (already on stan skin 30). Mihok pose left
  void / missing neighbor still open. Do not close without Mihok/live
  match.
- Full ASP HLE still out (music, spatial, footsteps / other SFX IDs).
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-02 685 full-leaf + Chead model-Y `c00070a`)

P0-2/P0-3 on top of 685 stretch `fe54dba` (KEEP stall/FPS). SHA
`c00070a`. Pushed to `origin/main`, Hetzner `/home/grok/GoldenEye` pull
+ `make -C native wasm` + `silveriris-vite` restart. Live wasm is this
SHA.

Mihok 0612 after `fe54dba`: stall HUD x=-98.5 z=-2358.4 KEEP; live rAF
≈24.5 (KEEP ff37828); spawn/look-left still brown ribs, no distinct 685
bars (42% Y stretch left four thin per-half nubs); Chead 48u neck-column
seat (~6 world units after fit=0.123) still a collar gap. Retail Pgas
verts 0-15 are 685 at Y 394..788 with s=0 at x=0. Remap 685 to the top
78% and one ST copy across the full leaf so the two handle bars read at
spawn/θ263. 686-688 are a short kickplate; 706 edges follow the Y map.
Seat Chead 100u in model Y (220u buried the face). KEEP door-jump
`102237e`, viscyl `dfde794`, SHADE*TEXEL `6377093`, alcove pin
`07e6b41`, G1 clip `4785f0f`, wasm-stack FPS `ff37828`, skip G_MTX
`bad9aff`, stall clip `b3fdadf`, inward look-at `9cadb53`, 16u cap
`2630bd1`, HUD stroke `a8d6c15`, 685 stretch `fe54dba`.

**1 — spawn / doors / head.** `playtest spawn xz=-89.5,-2358.4 y=29.1
retail_slab=1`. `play_spawn visual xz=-89.5,-2342.4 d=0.0,16.0`.
`head_joint chr=0 T=5.8,420.1,-36.7 seatY=100 idle`. `play_spawn idle40
xz=-98.5,-2358.4 d=-9.0,0.0` visual `-98.5,-2342.4 d=0.0,16.0` (no −X;
x>-170). `play_spawn_idle` / `play_lookleft` (θ263 at idle40 xz) are
brown SETTEX 685 handle bars (top ~78% of the leaf, one ST copy),
`handle_xrange=61/63`. Extra-idle Chead on the collar. `spawn_fill
play_spawn dark=14 metal=9877 area=13376 mauve=1199`. `play_spawn_idle
dark=17 metal=9833 mauve=1029`. `play_lookleft dark=1 metal=11262
mauve=2742`. `play_door_live` near_living=0 dark=65 metal=8246
mauve=260. `chris2 vis gi=37 dpad=22.2`. `door_jump` alcove spawn-left
(−89.5,−2242 yaw 180).

**2 — hitch / FPS / clip.** `play_spawn` frame_ms=52.01 (cold; 19.2 fps)
drawn=71 seen=2 skip_range=1 skip_leaf=22 mag=7/21 held=1 headj=1.
`long_walk` frame_ms=30.29 (33.0 fps). `long_walk_hall` (−347,−2116)
frame_ms=41.08 — 35ms class kept (this box; live rAF KEEP `ff37828`).
`door_jump` frame_ms=31.86. `mihok_block` clip_step d=12.0.
`fire_hitch miss_ms=25.69 hit_ms=23.52 hits=1`. y=29.12.

Native player/g1 playtest green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- P0-1 live spawn-idle rAF after hard-refresh still required. Harness
  long_walk is 33.0 fps / 35ms class; hall walk is ~24 fps. Do not close
  ff37828 re-prove from harness alone.
- P0-2 live match after hard-refresh still required (Mihok 0612 was
  `fe54dba`; live wasm after this push is `c00070a` 685 full-leaf +
  Chead model-Y). Harness idle40 / look-left is distinct 685 bars at
  −98.5,−2358 θ263. Do not close without Mihok/live match.
- P0-3 live match still required. Spawn/idle stay x>-170; look-left
  extra-idle is a full body + neck Chead in harness. `play_wall` pitch
  −35 still reads as ceiling (already on stan skin 30). Do not close
  without Mihok/live match.
- Full ASP HLE still out (music, spatial, footsteps / other SFX IDs).
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-02 686-688 ribs + Chead 26u world-Y `a4dd15e`)

P0-2/P0-3 on top of 685 full-leaf `c00070a` (KEEP stall/FPS). SHA
`a4dd15e`. Pushed to `origin/main`, Hetzner `/home/grok/GoldenEye` pull
+ `make -C native wasm` + `silveriris-vite` restart. Live wasm is this
SHA.

Mihok 0645 after `c00070a`: stall HUD x=-98.5 z=-2358.4 KEEP; live rAF
≈26.8 (KEEP ff37828); look-left two 685 pill bars readable but leaf
still pinkish-tan/mauve (685 empty-mauve field on 78% of the face);
Chead 100u model Y (~12 world) still a collar gap. Keep the full-leaf
ST copy so bars are not four nubs. Crop 685 T to bar texels (512..896)
and map 685 to the top ~38%; 686-688 take the bottom 62% ribbed plates.
Seat Chead 26 world Y only (neck-column / pad-yaw XZ buried or slid the
head). KEEP door-jump `102237e`, viscyl `dfde794`, SHADE*TEXEL
`6377093`, alcove pin `07e6b41`, G1 clip `4785f0f`, wasm-stack FPS
`ff37828`, skip G_MTX `bad9aff`, stall clip `b3fdadf`, inward look-at
`9cadb53`, 16u cap `2630bd1`, HUD stroke `a8d6c15`, 685 stretch
`fe54dba`, 685 full-leaf `c00070a`.

**1 — spawn / doors / head.** `playtest spawn xz=-89.5,-2358.4 y=29.1
retail_slab=1`. `play_spawn visual xz=-89.5,-2342.4 d=0.0,16.0`.
`head_joint chr=0 T=5.8,520.1,-36.7 seatY=26 idle`. `play_spawn idle40
xz=-98.5,-2358.4 d=-9.0,0.0` visual `-98.5,-2342.4 d=0.0,16.0` (no −X;
x>-170). `play_spawn_idle` / `play_lookleft` (θ263 at idle40 xz) are
686-688 ribbed metal + 685 handle bars (top ~38%, one ST copy, T crop),
`handle_xrange=57/49` `rib_yrange=44/63` `neck_gap=7`. Extra-idle Chead
on the collar from behind; profile indent 7px (not the 0645 wall band).
`spawn_fill play_spawn dark=14 metal=9476 area=13376 mauve=1193`.
`play_spawn_idle dark=18 metal=9414 mauve=1026`. `play_lookleft dark=1
metal=10990 mauve=2733`. `play_door_live` near_living=0 dark=65
metal=7713 mauve=263. `chris2 vis gi=37 dpad=22.2`. `door_jump` alcove
spawn-left (−89.5,−2242 yaw 180).

**2 — hitch / FPS / clip.** `play_spawn` frame_ms=50.53 (cold; 19.8 fps)
drawn=71 seen=2 skip_range=1 skip_leaf=22 mag=7/21 held=1 headj=1.
`long_walk` frame_ms=29.67 (33.7 fps). `long_walk_hall` (−347,−2116)
frame_ms=41.41 — 35ms class kept (this box; live rAF KEEP `ff37828`).
`door_jump` frame_ms=31.89. `mihok_block` clip_step d=12.0.
`fire_hitch miss_ms=23.76 hit_ms=23.63 hits=1`. y=29.12.

Native player/g1 playtest green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- P0-1 live spawn-idle rAF after hard-refresh still required. Harness
  long_walk is 33.7 fps / 35ms class; hall walk is ~24 fps. Do not close
  ff37828 re-prove from harness alone.
- P0-2 live match after hard-refresh still required (Mihok 0645 was
  `c00070a` mauve 685-face; live wasm after this push is `a4dd15e`
  686-688 ribs + 685 bar band). Harness idle40 / look-left is ribbed
  metal + distinct 685 bars at −98.5,−2358 θ263. Do not close without
  Mihok/live match.
- P0-3 live match still required. Spawn/idle stay x>-170; look-left
  extra-idle is Y-seated from behind with a 7px profile indent in
  harness. `play_wall` pitch −35 still reads as ceiling (already on stan
  skin 30). Do not close without Mihok/live match.
- Full ASP HLE still out (music, spatial, footsteps / other SFX IDs).
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-02 685 mid-leaf overlay + Chead 52u world-Y `fea5c04`)

P0-2/P0-3 on top of 686-688 ribs `a4dd15e` (KEEP stall/FPS). SHA
`fea5c04`. Pushed to `origin/main`, Hetzner `/home/grok/GoldenEye` pull
+ `make -C native wasm` + `silveriris-vite` restart. Live wasm is this
SHA (250743, Last-Modified 13:18:42 GMT).

Mihok 0732/0733 after `a4dd15e`: stall HUD x=-98.5 z=-2358.4 KEEP; live
rAF ≈27.4 (KEEP ff37828); spawn/look-left brown horizontal ribs but no
distinct 685 bars (top 38% band was a lintel sliver on live; harness
still green). Chead 26u world Y still a collar gap. Stretch 686-688
over the full leaf and overlay T-cropped 685 (one ST copy, t=512..896)
in the visible middle (16%..72%) with a 4u Z push so spawn/θ263 reads
two pills AND brown ribs. Seat Chead 52 world Y on idle only (26u left
the hole; 220u buried the face; neck-column / pad-yaw XZ shoved). KEEP
door-jump `102237e`, viscyl `dfde794`, SHADE*TEXEL `6377093`, alcove pin
`07e6b41`, G1 clip `4785f0f`, wasm-stack FPS `ff37828`, skip G_MTX
`bad9aff`, stall clip `b3fdadf`, inward look-at `9cadb53`, 16u cap
`2630bd1`, HUD stroke `a8d6c15`, 685 stretch `fe54dba`, 685 full-leaf
`c00070a`, 686-688 ribs `a4dd15e`.

**1 — spawn / doors / head.** `playtest spawn xz=-89.5,-2358.4 y=29.1
retail_slab=1`. `play_spawn visual xz=-89.5,-2342.4 d=0.0,16.0`.
`head_joint chr=0 T=5.8,520.1,-36.7 seatY=52 idle`. `play_spawn idle40
xz=-98.5,-2358.4 d=-9.0,0.0` visual `-98.5,-2342.4 d=0.0,16.0` (no −X;
x>-170). `play_spawn_idle` / `play_lookleft` (θ263 at idle40 xz) are
686-688 ribbed metal + 685 handle bars (mid 16–72%, one ST copy, T
crop, Z push), `handle_xrange=57/52` `rib_yrange=38/65` `neck_gap=7`.
Extra-idle Chead on the collar from behind; profile indent 7px (not
the 0645 wall band). `spawn_fill play_spawn dark=14 metal=9742
area=13376 mauve=1211`. `play_spawn_idle dark=18 metal=9707 mauve=1072`.
`play_lookleft dark=1 metal=11137 mauve=2745`. `play_door_live`
near_living=0 dark=65 metal=8135 mauve=319. `chris2 vis gi=37
dpad=22.2`. `door_jump` alcove spawn-left (−89.5,−2242 yaw 180).

**2 — hitch / FPS / clip.** `play_spawn` frame_ms=51.96 (cold; 19.2 fps)
drawn=71 seen=2 skip_range=1 skip_leaf=22 mag=7/21 held=1 headj=1.
`long_walk` frame_ms=30.59 (32.7 fps). `long_walk_hall` (−347,−2116)
frame_ms=41.43 — 35ms class kept (this box; live rAF KEEP `ff37828`).
`door_jump` frame_ms=32.41. `mihok_block` clip_step d=12.0.
`fire_hitch miss_ms=24.27 hit_ms=23.89 hits=1`. y=29.12.

Native player/g1 playtest green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- P0-1 live spawn-idle rAF after hard-refresh still required. Harness
  long_walk is 32.7 fps / 35ms class; hall walk is ~24 fps. Do not close
  ff37828 re-prove from harness alone.
- P0-2 live match after hard-refresh still required (Mihok 0732 was
  `a4dd15e` ribs-only lintel; live wasm after this push is `fea5c04`
  686-688 full-leaf + mid 685 overlay). Harness idle40 / look-left is
  ribbed metal + distinct 685 bars at −98.5,−2358 θ263. Do not close
  without Mihok/live match.
- P0-3 live match still required. Spawn/idle stay x>-170; look-left
  extra-idle is 52u Y-seated from behind with a 7px profile indent in
  harness. `play_wall` pitch −35 still reads as ceiling (already on stan
  skin 30). Do not close without Mihok/live match.
- Full ASP HLE still out (music, spatial, footsteps / other SFX IDs).
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-02 685 bar-quad overlay `5ceb364`)

P0-1 on top of fea5c04 mid-leaf overlay (KEEP stall/FPS/ribs). SHA
`5ceb364`. Pushed to `origin/main`, Hetzner `/home/grok/GoldenEye` pull
+ `make -C native wasm` + `silveriris-vite` restart. Live wasm is this
SHA.

Mihok 0732/0733 after `a4dd15e` (pre-fea5c04): brown ribs, no 685 bars.
fea5c04 full-width mid-leaf 685 was the dusty-rose field (harness
handle_xrange green, pills faint). Image 685 bars live at s≈384..544 /
736..928, t≈512..896. Stretch 686-688 over the full leaf and overlay
two S/T-cropped 685 quads (16%..72%, 4u Z push) so spawn/θ263 reads
two pills AND brown ribs. Chead stays 52 world Y. KEEP door-jump
`102237e`, viscyl `dfde794`, SHADE*TEXEL `6377093`, alcove pin
`07e6b41`, G1 clip `4785f0f`, wasm-stack FPS `ff37828`, skip G_MTX
`bad9aff`, stall clip `b3fdadf`, inward look-at `9cadb53`, 16u cap
`2630bd1`, HUD stroke `a8d6c15`, 685 stretch `fe54dba`, 685 full-leaf
`c00070a`, 686-688 ribs `a4dd15e`, mid-leaf overlay `fea5c04`.

**1 — spawn / doors / head.** `playtest spawn xz=-89.5,-2358.4 y=29.1
retail_slab=1`. `play_spawn visual xz=-89.5,-2342.4 d=0.0,16.0`.
`head_joint chr=0 T=5.8,520.1,-36.7 seatY=52 idle`. `play_spawn idle40
xz=-98.5,-2358.4 d=-9.0,0.0` visual `-98.5,-2342.4 d=0.0,16.0` (no −X;
x>-170). `play_spawn_idle` / `play_lookleft` (θ263 at idle40 xz) are
686-688 ribbed metal + two 685 handle bars (S/T crop, Z push),
`handle_xrange=49/52` `rib_yrange=44/60` `neck_gap=7`. Extra-idle
Chead on the collar from behind; profile indent 7px (not the 0645
wall band). `spawn_fill play_spawn dark=14 metal=9279 area=13376
mauve=1148`. `play_spawn_idle dark=18 metal=9234 mauve=1013`.
`play_lookleft dark=1 metal=10821 mauve=2694`. `play_door_live`
near_living=0 dark=65 metal=7642 mauve=203. `chris2 vis gi=37
dpad=22.2`. `door_jump` alcove spawn-left (−89.5,−2242 yaw 180).

**2 — hitch / FPS / clip.** `play_spawn` frame_ms=51.04 (cold; 19.6 fps)
drawn=71 seen=2 skip_range=1 skip_leaf=22 mag=7/21 held=1 headj=1.
`long_walk` frame_ms=29.63 (33.8 fps). `long_walk_hall` (−347,−2116)
frame_ms=40.82 — 35ms class kept (this box; live rAF KEEP `ff37828`).
`door_jump` frame_ms=31.55. `mihok_block` clip_step d=12.0.
`fire_hitch miss_ms=23.52 hit_ms=23.30 hits=1`. y=29.12.

Native player/g1 playtest green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- P0-1 live spawn-idle rAF after hard-refresh still required. Harness
  long_walk is 33.8 fps / 35ms class; hall walk is ~24 fps. Do not close
  ff37828 re-prove from harness alone.
- P0-2 live match after hard-refresh still required (Mihok 0732 was
  `a4dd15e` ribs-only; fea5c04 was a mauve mid-leaf plate). Harness
  idle40 / look-left is ribbed metal + two distinct 685 bars at
  −98.5,−2358 θ263. Do not close without Mihok/live match.
- P0-3 live match still required. Spawn/idle stay x>-170; look-left
  extra-idle is 52u Y-seated from behind with a 7px profile indent in
  harness. `play_wall` pitch −35 still reads as ceiling (already on stan
  skin 30). Do not close without Mihok/live match.
- Full ASP HLE still out (music, spatial, footsteps / other SFX IDs).
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-02 685 highlight-texel bars `f2a753a`)

P0-1 on top of 5ceb364 two-bar overlay (KEEP stall/FPS/ribs). SHA
`f2a753a`. Pushed to `origin/main`, Hetzner `/home/grok/GoldenEye` pull
+ `make -C native wasm` + `silveriris-vite` restart. Live wasm is this
SHA (251970, Last-Modified 14:22:49 GMT).

Mihok 0732/0733 after `a4dd15e` (pre-5ceb364): brown ribs, no 685 bars.
5ceb364 cropped bar 0 at s=384–544 (texel 12–17), past the light
highlight at texel 11, so harness xrange was green but the quads were
dark smears (not Mihok 0645 light pills). Native 32x32: highlights at
x=11/25, shadows at x=13–14/27–28, pills t=15–27. Crop those plus
1-texel field (s=320–512 / 768–960, t=448–896); keep two quads so
686-688 ribs stay between them. Chead stays 52 world Y. KEEP door-jump
`102237e`, viscyl `dfde794`, SHADE*TEXEL `6377093`, alcove pin
`07e6b41`, G1 clip `4785f0f`, wasm-stack FPS `ff37828`, skip G_MTX
`bad9aff`, stall clip `b3fdadf`, inward look-at `9cadb53`, 16u cap
`2630bd1`, HUD stroke `a8d6c15`, 685 stretch `fe54dba`, 685 full-leaf
`c00070a`, 686-688 ribs `a4dd15e`, mid-leaf overlay `fea5c04`, two-bar
overlay `5ceb364`.

**1 — spawn / doors / head.** `playtest spawn xz=-89.5,-2358.4 y=29.1
retail_slab=1`. `play_spawn visual xz=-89.5,-2342.4 d=0.0,16.0`.
`head_joint chr=0 T=5.8,520.1,-36.7 seatY=52 idle`. `play_spawn idle40
xz=-98.5,-2358.4 d=-9.0,0.0` visual `-98.5,-2342.4 d=0.0,16.0` (no −X;
x>-170). `play_spawn_idle` / `play_lookleft` (θ263 at idle40 xz) are
686-688 ribbed metal + two 685 handle bars (highlight+shadow crop, Z
push), `handle_xrange=49/52` `rib_yrange=44/60` `neck_gap=7`. Extra-idle
Chead on the collar from behind; profile indent 7px (not the 0645
wall band). `spawn_fill play_spawn dark=14 metal=9330 area=13376
mauve=1199`. `play_spawn_idle dark=18 metal=9269 mauve=1056`.
`play_lookleft dark=1 metal=10862 mauve=2720`. `play_door_live`
near_living=0 dark=65 metal=7727 mauve=316. `chris2 vis gi=37
dpad=22.2`. `door_jump` alcove spawn-left (−89.5,−2242 yaw 180).

**2 — hitch / FPS / clip.** `play_spawn` frame_ms=50.51 (cold; 19.8 fps)
drawn=71 seen=2 skip_range=1 skip_leaf=22 mag=7/21 held=1 headj=1.
`long_walk` frame_ms=29.70 (33.7 fps). `long_walk_hall` (−347,−2116)
frame_ms=40.40 — 35ms class kept (this box; live rAF KEEP `ff37828`).
`door_jump` frame_ms=31.26. `mihok_block` clip_step d=12.0.
`fire_hitch miss_ms=23.57 hit_ms=23.28 hits=1`. y=29.12.

Native player/g1 playtest green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- P0-1 live spawn-idle rAF after hard-refresh still required. Harness
  long_walk is 33.7 fps / 35ms class; hall walk is ~24 fps. Do not close
  ff37828 re-prove from harness alone.
- P0-2 live match after hard-refresh still required (Mihok 0732 was
  `a4dd15e` ribs-only; 5ceb364 bar-0 crop missed the light column).
  Harness idle40 / look-left is ribbed metal + two 685 highlight+shadow
  pills at −98.5,−2358 θ263. Do not close without Mihok/live match.
- P0-3 live match still required. Spawn/idle stay x>-170; look-left
  extra-idle is 52u Y-seated from behind with a 7px profile indent in
  harness. `play_wall` pitch −35 still reads as ceiling (already on stan
  skin 30). Do not close without Mihok/live match.
- Full ASP HLE still out (music, spatial, footsteps / other SFX IDs).
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-02 Chead 80u world-Y `1be1d29`)

P0-3 on top of 685 highlight-texel bars `f2a753a` (KEEP stall/FPS/ribs).
SHA `1be1d29`. Pushed to `origin/main`, Hetzner `/home/grok/GoldenEye`
pull + `make -C native wasm` + `silveriris-vite` restart. Live wasm is
this SHA (251970, Last-Modified 14:36:01 GMT).

Mihok 1018 after `5ceb364`: stall HUD x=-98.5 z=-2358.4 KEEP; live rAF
≈26.0 (KEEP ff37828); 685 bars were dark smears (crop missed highlight
texel 11). Mihok 1032 after `f2a753a` hard-refresh live-closed doors:
brown 686-688 ribs + two 685 highlight handle bars. 52u world-Y seat
still a high-behind / θ263 neck gap. Seat Chead 80 world Y (26u/52u
failed live; 220u buried the face). KEEP door-jump `102237e`, viscyl `dfde794`,
SHADE*TEXEL `6377093`, alcove pin `07e6b41`, G1 clip `4785f0f`,
wasm-stack FPS `ff37828`, skip G_MTX `bad9aff`, stall clip `b3fdadf`,
inward look-at `9cadb53`, 16u cap `2630bd1`, HUD stroke `a8d6c15`, 685
stretch `fe54dba`, 685 full-leaf `c00070a`, 686-688 ribs `a4dd15e`,
mid-leaf overlay `fea5c04`, two-bar overlay `5ceb364`, highlight-texel
bars `f2a753a`.

**1 — spawn / doors / head.** `playtest spawn xz=-89.5,-2358.4 y=29.1
retail_slab=1`. `play_spawn visual xz=-89.5,-2342.4 d=0.0,16.0`.
`head_joint chr=0 T=5.8,520.1,-36.7 seatY=80 idle`. `play_spawn idle40
xz=-98.5,-2358.4 d=-9.0,0.0` visual `-98.5,-2342.4 d=0.0,16.0` (no −X;
x>-170). `play_spawn_idle` / `play_lookleft` (θ263 at idle40 xz) are
686-688 ribbed metal + two 685 highlight+shadow bars, `handle_xrange=49/52`
`rib_yrange=44/60` `neck_gap=7`. Extra-idle Chead on the collar from
behind; profile indent 7px (not the 0645/1018 wall band). `spawn_fill
play_spawn dark=14 metal=9330 area=13376 mauve=1199`. `play_spawn_idle
dark=18 metal=9269 mauve=1056`. `play_lookleft dark=1 metal=10862
mauve=2720`. `play_door_live` near_living=0 dark=65 metal=7727
mauve=316. `chris2 vis gi=37 dpad=22.2`. `door_jump` alcove spawn-left
(−89.5,−2242 yaw 180).

**2 — hitch / FPS / clip.** `play_spawn` frame_ms=50.75 (cold; 19.7 fps)
drawn=71 seen=2 skip_range=1 skip_leaf=22 mag=7/21 held=1 headj=1.
`long_walk` frame_ms=29.67 (33.7 fps). `long_walk_hall` (−347,−2116)
frame_ms=40.32 — 35ms class kept (this box; live rAF KEEP `ff37828`).
`door_jump` frame_ms=31.25. `mihok_block` clip_step d=12.0.
`fire_hitch miss_ms=23.49 hit_ms=23.22 hits=1`. y=29.12.

Native player/g1 playtest green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- P0-1 live spawn-idle rAF after hard-refresh still required. Harness
  long_walk is 33.7 fps / 35ms class; hall walk is ~24 fps. Do not close
  ff37828 re-prove from harness alone.
- P0-2 doors: Mihok 1032 after `f2a753a` hard-refresh (wasm 251970,
  Last-Modified 14:22:49 GMT) live-closed spawn/θ263 — brown 686-688
  ribs + two 685 highlight handle bars (0645-readable, not 1018 smears).
  Keep closed unless a later pass regresses ribs or bars.
- P0-3 live match still required. Spawn/idle stay x>-170; look-left
  extra-idle is 80u Y-seated from behind with a 7px profile indent in
  harness. Mihok 1032 (still on 52u wasm) still floated. Live wasm after
  this push is `1be1d29` 80u. `play_wall` pitch −35 still reads as
  ceiling (already on stan skin 30). Do not close without Mihok/live match.
- Full ASP HLE still out (music, spatial, footsteps / other SFX IDs).
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-02 Chead neck-4x4 model Y `de9800f`)

P0-3 on top of 685 highlight bars `f2a753a` (KEEP stall/FPS/ribs/handles).
SHA `de9800f`. Pushed to `origin/main`, Hetzner `/home/grok/GoldenEye`
pull + `make -C native wasm` + `silveriris-vite` restart. Live wasm is
this SHA (251983, Last-Modified 14:48:34 GMT).

Mihok 1032 after `f2a753a`: doors live-closed (brown 686-688 ribs + two
685 highlight handle bars). 52u pad extra_y still floated. Pad extra_y
52 vs 80 was bit-identical on spawn — njoints==1 Chead follows
HeadPlaceholder T.y≈520, not rooms.oy. Drop 160 model Y on the copied
neck 4x4 (~20 world at fit=0.123). Die stays 0. KEEP door-jump
`102237e`, viscyl `dfde794`, SHADE*TEXEL `6377093`, alcove pin
`07e6b41`, G1 clip `4785f0f`, wasm-stack FPS `ff37828`, skip G_MTX
`bad9aff`, stall clip `b3fdadf`, inward look-at `9cadb53`, 16u cap
`2630bd1`, HUD stroke `a8d6c15`, 685 stretch `fe54dba`, 685 full-leaf
`c00070a`, 686-688 ribs `a4dd15e`, mid-leaf overlay `fea5c04`, two-bar
overlay `5ceb364`, highlight-texel bars `f2a753a`.

**1 — spawn / doors / head.** `playtest spawn xz=-89.5,-2358.4 y=29.1
retail_slab=1`. `play_spawn visual xz=-89.5,-2342.4 d=0.0,16.0`.
`head_joint chr=0 T=5.8,360.1,-36.7 seatM=160 idle`. `play_spawn idle40
xz=-98.5,-2358.4 d=-9.0,0.0` visual `-98.5,-2342.4 d=0.0,16.0` (no −X;
x>-170). `play_spawn_idle` / `play_lookleft` (θ263 at idle40 xz) are
686-688 ribbed metal + two 685 highlight+shadow bars, `handle_xrange=49/52`
`rib_yrange=44/60` `neck_gap=7`. Extra-idle Chead seated on the collar
from behind (head pixels moved vs pad-Y). `spawn_fill play_spawn dark=14
metal=9330 area=13376 mauve=1199`. `play_spawn_idle dark=18 metal=9269
mauve=1056`. `play_lookleft dark=1 metal=10862 mauve=2720`.
`play_door_live` near_living=0 dark=65 metal=7727 mauve=316. `chris2 vis
gi=37 dpad=22.2`. `door_jump` alcove spawn-left (−89.5,−2242 yaw 180).

**2 — hitch / FPS / clip.** `play_spawn` frame_ms=50.48 (cold; 19.8 fps)
drawn=71 seen=2 skip_range=1 skip_leaf=22 mag=7/21 held=1 headj=1.
`long_walk` frame_ms=29.67 (33.7 fps). `long_walk_hall` (−347,−2116)
frame_ms=40.24 — 35ms class kept (this box; live rAF KEEP `ff37828`).
`door_jump` frame_ms=31.22. `mihok_block` clip_step d=12.0.
`fire_hitch miss_ms=23.61 hit_ms=23.34 hits=1`. y=29.12.

Native player/g1 playtest green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- P0-1 live spawn-idle rAF after hard-refresh still required. Harness
  long_walk is 33.7 fps / 35ms class; hall walk is ~24 fps. Do not close
  ff37828 re-prove from harness alone.
- P0-2 doors: Mihok 1032 after `f2a753a` live-closed spawn/θ263 — brown
  686-688 ribs + two 685 highlight handle bars. Keep closed unless a
  later pass regresses ribs or bars.
- P0-3 live match still required. Spawn/idle stay x>-170; look-left
  extra-idle is 160 model-Y on the neck 4x4 from behind with a 7px
  profile indent in harness. Mihok 1032 (52u pad-Y wasm) still floated.
  Live wasm after this push is `de9800f`. `play_wall` pitch −35 still
  reads as ceiling (already on stan skin 30). Do not close without
  Mihok/live match.
- Full ASP HLE still out (music, spatial, footsteps / other SFX IDs).
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-02 Chead stitched-neck DL `8cc1ce1`)

P0-3 on top of 685 highlight bars `f2a753a` (KEEP stall/FPS/ribs/handles).
SHA `8cc1ce1`. Pushed to `origin/main`, Hetzner `/home/grok/GoldenEye`
pull + `make -C native wasm` + `silveriris-vite` restart. Live wasm is
this SHA (251961, Last-Modified 15:40:41 GMT).

Mihok 1058 after `de9800f`: 160 model-Y on the neck 4x4 moved harness
pixels from behind but LIVE 3/4 still floated. Pad extra_y never moved
njoints==1 Chead. The gap was a missing collar: jointed oliveguard
stripped the neck-GROUP sibling DL that G_MTX LOADs torso slot 1 then
neck slot 0 and TRI4s the stitched collar. Keep that DL on the joint
path; skip=pose still strips (ceiling slab). HeadPlaceholder T stays
dump T.y≈520. Die stays 0. KEEP door-jump `102237e`, viscyl `dfde794`,
SHADE*TEXEL `6377093`, alcove pin `07e6b41`, G1 clip `4785f0f`,
wasm-stack FPS `ff37828`, skip G_MTX `bad9aff`, stall clip `b3fdadf`,
inward look-at `9cadb53`, 16u cap `2630bd1`, HUD stroke `a8d6c15`, 685
stretch `fe54dba`, 685 full-leaf `c00070a`, 686-688 ribs `a4dd15e`,
mid-leaf overlay `fea5c04`, two-bar overlay `5ceb364`, highlight-texel
bars `f2a753a`.

**1 — spawn / doors / head.** `playtest spawn xz=-89.5,-2358.4 y=29.1
retail_slab=1`. `play_spawn visual xz=-89.5,-2342.4 d=0.0,16.0`.
`head_joint chr=0 T=5.8,520.1,-36.7 seatM=0 idle`. `play_spawn idle40
xz=-98.5,-2358.4 d=-9.0,0.0` visual `-98.5,-2342.4 d=0.0,16.0` (no −X;
x>-170). `play_spawn_idle` / `play_lookleft` (θ263 at idle40 xz) are
686-688 ribbed metal + two 685 highlight+shadow bars, `handle_xrange=49/52`
`rib_yrange=44/60` `neck_gap=7`. Extra-idle Chead on the collar from
behind and 3/4 (stitched neck, not a wall band). `spawn_fill play_spawn
dark=14 metal=9330 area=13376 mauve=1199`. `play_spawn_idle dark=18
metal=9269 mauve=1056`. `play_lookleft dark=1 metal=10862 mauve=2720`.
`play_door_live` near_living=0 dark=65 metal=7727 mauve=316. `chris2 vis
gi=37 dpad=22.3`. `door_jump` alcove spawn-left (−89.5,−2242 yaw 180).

**2 — hitch / FPS / clip.** `play_spawn` frame_ms=51.89 (cold; 19.3 fps)
drawn=72 seen=2 skip_range=1 skip_leaf=22 mag=7/21 held=1 headj=1.
`long_walk` frame_ms=30.63 (32.7 fps). `long_walk_hall` (−347,−2116)
frame_ms=41.60 — 35ms class kept (this box; live rAF KEEP `ff37828`).
`door_jump` frame_ms=32.41. `mihok_block` clip_step d=12.0.
`fire_hitch miss_ms=24.50 hit_ms=23.79 hits=1`. y=29.12.

Native player/g1 playtest green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- P0-1 live spawn-idle rAF after hard-refresh still required. Harness
  long_walk is 32.7 fps / 35ms class; hall walk is ~24 fps. Do not close
  ff37828 re-prove from harness alone.
- P0-2 doors: Mihok 1032 after `f2a753a` live-closed spawn/θ263 — brown
  686-688 ribs + two 685 highlight handle bars. Keep closed unless a
  later pass regresses ribs or bars.
- P0-3 live match still required. Spawn/idle stay x>-170; look-left
  extra-idle is stitched-neck + Chead on the collar from behind and 3/4
  with a 7px profile indent in harness. Mihok 1058 (`de9800f` 160 model-Y)
  still floated. Live wasm after this push is `8cc1ce1`. `play_wall`
  pitch −35 still reads as ceiling (already on stan skin 30). Do not
  close without Mihok/live match.
- Full ASP HLE still out (music, spatial, footsteps / other SFX IDs).
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-02 8cc1ce1 Chead LIVE 3/4 `c35de27`)

P0-3 live-close of stitched-neck Chead `8cc1ce1` (no new wasm). Hard-refresh
Chrome on https://007.goodhouseinc.com `?ff_netplay=0`, pack `fff814d2…`
(same IDB hash as Mihok 1018–1058), wasm 251961 Last-Modified 15:40:41 GMT.

Mihok 1058 was `de9800f` (160 model-Y, neck DL still stripped on that
build) and floated on live 3/4. Do not treat 1058 as an 8cc1ce1 result.
Live θ263 after this hard-refresh: extra-idle Chead on the olive collar,
`neck_gap=7` (same as harness play_lookleft; profile indent, not the 1018
wall band). Spawn/θ270 behind `neck_gap=28` matches harness
`play_spawn_idle` (nape indent, not a detached hat). HUD
`x=-98.5 z=-2358.4 y=29.1` stall KEEP. θ263 door face still brown 686-688
ribs + two 685 highlight handle bars (f2a753a KEEP). Do not reintroduce
`de9800f` 160 model-Y. Do not strip the neck-GROUP sibling DL on the
jointed path.

**1 — spawn / doors / head.** `playtest spawn xz=-89.5,-2358.4 y=29.1
retail_slab=1`. Live spawn-idle HUD `xz=-98.5,-2358.4 θ270`. Live look-left
forced to θ263 (same pose as `play_lookleft`). `spawn_fill` metric on the
live canvas: look-left `neck_gap=7`; idle `neck_gap=28`. Handles/ribs
unchanged from Mihok 1032/1058.

**2 — hitch / FPS / clip.** This capture box live rAF ≈51 fps (180 /
3513ms) — not the Facility box; do not close `ff37828` from it. Harness
numbers unchanged from `8cc1ce1` STATUS.

Native player/g1 playtest green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- P0-1 live spawn-idle rAF after hard-refresh still required. Harness
  long_walk is 32.7 fps / 35ms class; hall walk is ~24 fps. Do not close
  ff37828 re-prove from this Mac capture (~51 fps) or from harness alone.
- P0-2 doors: Mihok 1032 after `f2a753a` live-closed spawn/θ263 — brown
  686-688 ribs + two 685 highlight handle bars. Live 1215 θ263 still
  shows both. Keep closed unless a later pass regresses ribs or bars.
- P0-3 LIVE-CLOSED on `8cc1ce1` after hard-refresh 3/4 (θ263 neck_gap=7,
  Chead on collar). Behind matches harness idle nape (28px), not Mihok
  1018/1058 wall band. KEEP stitched-neck DL; no model-Y fudge.
  `play_wall` pitch −35 still reads as ceiling (already on stan skin 30).
- Missing-neighbor black void / portal depth still open (not exercised).
- Full ASP HLE still out (music, spatial, footsteps / other SFX IDs).
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-02 missing-neighbor harness `23be532`)

P0 missing-neighbor / portal-depth slice `23be532` on `8cc1ce1` (no new
wasm). Harness exercised `play_door_mihok` (−161,−2382 θ290). Not
LIVE-closed — live 1215 was stall spawn/θ263 only.

r71 has three bg portals: path `r7-r71` and `r11-r71` (closed slabs, vis
cull) and stacked `r6-r71` at the **same local xz** as `r7-r71`
(650.7,−2058.9) with y=−106 vs −425. `portal_vis_closed` is xz-only on
purpose: walking r6 from r71 would BFS r6→r8 (`r8-r6` stacked over
`r8-r7`) and draw the next hall through the closed leaf (`e9247e9`
clipdoor KEEP). No side-neighbor portal for the left alcove — that hole
is one-sided in-room G1, already stamped kind=1 alcove (−89.5,−2242 yaw
180) + kind=2 cutout (−71.1,−2224 yaw −90).

**1 — Mihok pose.** `play_door_mihok walked n=1 71 cur=71 slabs=4`.
`mihok_void left_clear=0 left_dark=259 area=16960` (uncleared FB in the
left third, skip HUD). PNG has no left black rectangle; crate + tiled
wall + ceiling fill the frame. `g1cut[0]` at that pose is off-screen
(`along=-142 on=0`). Do not vis-walk stacked twins of closed path
doors.

**2 — hitch / FPS / KEEP.** No wasm. Handles `f2a753a` and Chead
`8cc1ce1` untouched. Stall spawn `xz=-98.5,-2358.4` untouched. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

Native player/g1 playtest green (synthetic greyscale unchanged).

**Remaining holes**

- P0-1 live spawn-idle rAF after hard-refresh still required. Harness
  long_walk is 32.7 fps / 35ms class; hall walk is ~24 fps. Do not close
  ff37828 re-prove from a Mac capture or from harness alone.
- P0-2 doors: Mihok 1032/1215 live-closed spawn/θ263 — brown 686-688 ribs
  + two 685 highlight handle bars. Keep closed unless a later pass
  regresses ribs or bars.
- P0-3 LIVE-CLOSED on `8cc1ce1` (θ263 neck_gap=7). KEEP stitched-neck DL;
  no model-Y fudge.
- Missing-neighbor: harness `play_door_mihok` left_clear=0. Not
  LIVE-closed — need Mihok at −161,−2382 θ290 after hard-refresh. Do not
  walk r6 from r71 to "fill" it.
- Full ASP HLE still out (music, spatial, footsteps / other SFX IDs).
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-02 missing-neighbor LIVE `8cc1ce1`)

P0 missing-neighbor live-close of harness `23be532` (no new wasm).
Hard-refresh Chrome on https://007.goodhouseinc.com `?ff_netplay=0`, pack
`fff814d2…` (same IDB hash as Mihok 1018–1215), wasm 251961 Last-Modified
15:40:41 GMT (`8cc1ce1`).

Live 1215 was stall spawn/θ263 only. This capture pokes the documented
Mihok pose (−161,−2382 θ290 φ−5) in the live wasm ply slot and
`port_api_draw`s — same place+draw as harness `play_door_mihok`. Do not
walk stacked r6-r71 (would BFS r8 through the closed r7 leaf). KEEP
handles `f2a753a`, Chead `8cc1ce1`, stall −98.5/−2358.4, FPS `ff37828`.

**1 — Mihok pose.** Live G1 at −161,−2382 θ290 φ−5: `cur=71 walked=1`
`mihok_void left_clear=0 left_dark=257 area=16960` (left third, skip HUD;
identical to the harness PNG). Crate + tiled wall + ceiling + brown
leaf fill the frame; extra-idle Chead on the collar at frame left. No
uncleared black rectangle. Live rAF look-at will not *park* z at −2382
(snaps back to idle −2358.4); HUD x=−161 θ290 at that idle z is also
not a void (`left_clear=2`). The hole was missing-neighbor G1, not
look-at.

**2 — hitch / FPS / KEEP.** No wasm. Handles/Chead/stall untouched.
This box live rAF is not the Facility box; do not close `ff37828` from
it. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

Native player/g1 playtest green (synthetic greyscale unchanged).

**Remaining holes**

- P0-1 live spawn-idle rAF after hard-refresh still required. Harness
  long_walk is 32.7 fps / 35ms class; hall walk is ~24 fps. Do not close
  ff37828 re-prove from a Mac capture or from harness alone.
- P0-2 doors: Mihok 1032/1215 live-closed spawn/θ263 — brown 686-688 ribs
  + two 685 highlight handle bars. Keep closed unless a later pass
  regresses ribs or bars.
- P0-3 LIVE-CLOSED on `8cc1ce1` (θ263 neck_gap=7). KEEP stitched-neck DL;
  no model-Y fudge.
- Missing-neighbor LIVE-CLOSED on `8cc1ce1` at −161,−2382 θ290
  (`left_clear=0`). Do not walk r6 from r71 to "fill" it.
- Full ASP HLE still out (music, spatial, footsteps / other SFX IDs).
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-02 fire-hit chase clip `7ab099c`)

P0-1 first-enemy Chrome freeze on `8cc1ce1` / footsteps live. SHA `7ab099c`.
Pushed to `origin/main`, Hetzner `/home/grok/GoldenEye` pull + `make -C
native wasm` + `silveriris-vite` restart. Live wasm is this SHA.

Chris ~14:16 ET: FPS < 1 when shooting the first Facility extra-idle.
Not the walking stack freeze (`ff37828` KEEP). Hear-shot alerted two
room-71 chasers; each chase dest ran player `clip_step_ground` (G1 wall
push × stan tile probes). Unopt harness: `guard_fire_ms=322` then
`after=393` (2.5 fps). Sit chase dests on a ground tile; closed doors
still unlatch. rAF catch-up capped at 100ms / 2 ticks. Huffman tree
walks stay finite. KEEP handles `f2a753a`, Chead `8cc1ce1`, stall
−98.5/−2358.4, missing-neighbor `23be532`, wasm-stack `ff37828`.

**1 — fire-hit.** `--firehit` idle40 xz=−98.5,−2358.4 aim extra-idle
−350,−2320: `miss_ms=24.19` `hit_ms=24.11` hits=2 kills=1.
`guard_fire_ms=1.29` (was 322). `draw0=52` `after19=73` (was 393).
Playtest `fire_hitch miss_ms=23.57 hit_ms=23.25 draw_after_ms=30.13`
`after[0..7]≈52–54` kills=2 mag 7/14.

**2 — hitch / FPS / KEEP.** `play_spawn` frame_ms=51.07 drawn=72.
`long_walk` 30.70. `long_walk_hall` 41.01 — 35ms class kept (this box;
live rAF KEEP `ff37828`). `door_jump` 32.35. Stall idle40
xz=−98.5,−2358.4. y=29.12.

Native player/gun/g1 playtest green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- P0-1 live fire-hit rAF after hard-refresh still required. Harness
  post-hit is 52–73ms unopt / 35ms class. Do not close without
  Mihok/live Chrome (shoot first Facility enemy, miss vs hit).
- P0-2 live spawn-idle rAF still required. Do not close `ff37828`
  re-prove from harness alone.
- P0-2 doors: Mihok 1032/1215 live-closed spawn/θ263 — brown 686-688
  ribs + two 685 highlight handle bars. Keep closed unless a later
  pass regresses ribs or bars.
- P0-3 LIVE-CLOSED on `8cc1ce1` (θ263 neck_gap=7). KEEP stitched-neck
  DL; no model-Y fudge.
- Missing-neighbor LIVE-CLOSED on `8cc1ce1` at −161,−2382 θ290. Do not
  walk stacked r6.
- Full ASP HLE still out (music, spatial; footsteps are mixer
  placeholder `3c97070`, not pack IDs).
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-02 fire-hit LIVE `7ab099c`)

P0-1 first-enemy Chrome freeze LIVE-CLOSED on `7ab099c` (no new wasm).
Hard-refresh Chrome on https://007.goodhouseinc.com `?ff_netplay=0`, pack
`fff814d2…`, wasm **253721** Last-Modified 18:47:03 GMT (`7ab099c`).
Chris 14:21 was the pre-fix freeze (wasm 252947 @ 18:18 GMT, hit 1,
Incognito ~101% CPU). Do not treat 14:21 as a `7ab099c` result.

Sit chase dests on a ground tile; rAF catch-up capped 100ms / 2 ticks.
KEEP handles `f2a753a`, Chead `8cc1ce1`, stall −98.5/−2358.4,
missing-neighbor `23be532`, wasm-stack `ff37828`, footsteps `3c97070`.

**1 — fire-hit LIVE.** Stall HUD `x=-98.5 z=-2358.4 y=29.1 θ261` aimed at
extra-idle −350,−2320. First fire: hits 1 kills 1 mag 7→6 sfx=1. Guard
drops; post-hit rAF **201 / 3500ms ≈ 57.4 fps**. Tab did not freeze.
`.local/mihok-chrome-playtest-20260902-1458.md`. Harness `--firehit`
miss 23.70 / hit 23.46 `guard_fire_ms=1.31` (was 322) after 73–76ms
dead=1 hits=2 kills=1.

**2 — hitch / FPS / KEEP.** This capture box live rAF idle ≈49.7 fps
(174 / 3501ms); walk KeyW ≈40.9 fps (143 / 3500ms) last_sfx=15. Mac
box, not Facility rAF — do not close `ff37828` from it. Stall idle
xz=−98.5,−2358.4. Handles/Chead/void untouched.

Native player/gun/g1 playtest green (firehit re-run). Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- P0-1 fire-hit LIVE-CLOSED on `7ab099c` (stall θ261 hit 1 / kill 1,
  post-hit ≈57 fps). Do not reopen as the walking stack freeze.
- P0-2 live spawn-idle / walk rAF still required on the Facility box.
  Mac live idle ~50 / walk ~41 does not close `ff37828`.
- P0-2 doors: Mihok 1032/1215 live-closed spawn/θ263 — brown 686-688
  ribs + two 685 highlight handle bars. Keep closed unless a later
  pass regresses ribs or bars.
- P0-3 LIVE-CLOSED on `8cc1ce1` (θ263 neck_gap=7). KEEP stitched-neck
  DL; no model-Y fudge.
- Missing-neighbor LIVE-CLOSED on `8cc1ce1` at −161,−2382 θ290. Do not
  walk stacked r6.
- Full ASP HLE still out (music, spatial; footsteps are mixer
  placeholder `3c97070`, live last_sfx=15 while walking, not pack IDs).
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-02 Facility compact MIDI `dd988da`)

Music slice on fire-hit LIVE-CLOSED `7ab099c` (KEEP). SHA `33b62c5` (HUD
seq-before-rAF). Wasm `9b39896` **259886** Last-Modified 19:22:29 GMT.
Pushed to `origin/main`, Hetzner `/home/grok/GoldenEye` pull + `make -C
native wasm` + `silveriris-vite` restart. Live wasm is this SHA.

Walk pack `assets/music/Mfacility.bin` compact MIDI onto eight mixer
triangle voices. Not ASP HLE (no `instruments.tbl`, envelopes, pitch, or
spatial). Placeholder 196/294 Hz drone stays off. `last_sfx` unchanged
(idle 0, walk 15). HUD `seq 1` when the pack seq is running.

**1 — live seq / walk.** Hard-refresh Chrome `?ff_netplay=0`, pack
`fff814d2…`. Spawn HUD `x=-98.5 z=-2358.4 y=29.1 θ270` **seq=1**. Idle rAF
**149 / 3500ms ≈ 42.6 fps**; walk KeyW ≈**34.7 fps** (122 / 3516ms) to
x=−573.3 last_sfx=15 seq=1. Tab did not freeze. Mac box, not Facility rAF
— do not close `ff37828`. Pre-seq Mac 1508 on wasm 253721 was idle 49.7 /
walk 40.9; seq costs ~7 fps here, not a 1 fps freeze.
`.local/mihok-chrome-playtest-20260902-1525.md` (1508 same box pre-seq).

**2 — hitch / KEEP.** Stall xz=−98.5,−2358.4. Handles/Chead/void/fire-hit
untouched. Native audio-test (new `seq.pcm.sha256`) + player/gun/port-api
green. Greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.

**Remaining holes**

- P0-1 fire-hit LIVE-CLOSED on `7ab099c`. Do not reopen as walking freeze.
- P0-2 live spawn-idle / walk rAF still required on the Facility box.
  Mac live idle ~43 / walk ~35 (with seq) does not close `ff37828`.
- P0-2 doors: Mihok 1032/1215 live-closed spawn/θ263 — brown 686-688
  ribs + two 685 highlight handle bars. Keep closed unless a later
  pass regresses ribs or bars.
- P0-3 LIVE-CLOSED on `8cc1ce1` (θ263 neck_gap=7). KEEP stitched-neck
  DL; no model-Y fudge.
- Missing-neighbor LIVE-CLOSED on `8cc1ce1` at −161,−2382 θ290. Do not
  walk stacked r6.
- Full ASP HLE still out (wavetable, envelopes, spatial). Music is pack
  compact MIDI on mixer triangles `dd988da`, not ASP. Footsteps remain
  mixer placeholder `3c97070` (live last_sfx=15 while walking).
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-02 occluded-texel skip `e6e6cb4`)

P0-2 walking rAF slice on fire-hit LIVE-CLOSED `7ab099c` / seq `dd988da`
(KEEP). SHA `e6e6cb4`. Pushed to `origin/main`, Hetzner `/home/grok/GoldenEye`
pull + `make -C native wasm` + `silveriris-vite` restart. Live wasm is this
SHA (**259888**, Last-Modified 19:47:32 GMT).

`put_px` already dropped farther fragments (`z > zb`). Sample and shade
after that same test so occluded texels are not fetched. G1 greyscale
unchanged. Spawn_fill KEEP (dark/metal/mauve, handle_xrange 49/52,
neck_gap=7, mihok_void left_clear=0). Native unopt play_spawn ~53–57ms
class; long_walk ~32–36ms. Not ASP.

**1 — live walk / fire.** Hard-refresh Chrome `?ff_netplay=0`, pack
`fff814d2…`. Spawn HUD `x=-98.5 z=-2358.4 y=29.1 θ270 seq=1`. Idle rAF
**151 / 3500ms ≈ 43.1 fps**; walk KeyW ≈**35.7 fps** (126 / 3533ms) to
x=−573.3 last_sfx=15 seq=1. Tab did not freeze. Pre-e6e6cb4 Mac 1538 on
wasm 259886 was idle 42.6 / walk 34.7 — about +1 fps here, not a freeze.
`.local/mihok-chrome-playtest-20260902-1547.md`. Fire-at-first-enemy after
this wasm: stall θ261 hits 1 kills 1 mag 7→6, post-hit rAF **179 / 3516ms
≈ 50.9 fps**, no freeze. `.local/mihok-chrome-playtest-20260902-1548.md`.

**2 — hitch / KEEP.** Stall xz=−98.5,−2358.4. Handles/Chead/void/fire-hit
untouched. Native g1 greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.
player/gun/audio-test green.

**Remaining holes**

- P0-1 fire-hit LIVE-CLOSED on `7ab099c` (re-proved on `e6e6cb4` wasm:
  stall θ261 hit 1 / kill 1, post-hit ≈51 fps). Do not reopen as walking
  freeze.
- P0-2 live spawn-idle / walk rAF still required on the Facility box.
  Mac live idle ~43 / walk ~36 (seq + early-Z) does not close `ff37828`.
  Facility-box CDP :9226 was not reachable this pass.
- P0-2 doors: Mihok 1032/1215 live-closed spawn/θ263 — brown 686-688
  ribs + two 685 highlight handle bars. Live 1547 still shows both.
  Keep closed unless a later pass regresses ribs or bars.
- P0-3 LIVE-CLOSED on `8cc1ce1` (θ263 neck_gap=7). KEEP stitched-neck
  DL; no model-Y fudge.
- Missing-neighbor LIVE-CLOSED on `8cc1ce1` at −161,−2382 θ290. Do not
  walk stacked r6.
- Full ASP HLE still out (wavetable, envelopes, spatial). Music is pack
  compact MIDI on mixer triangles `dd988da`, not ASP. Footsteps remain
  mixer placeholder `3c97070` (live last_sfx=15 while walking).
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-02 inner-loop hoist `ff43a68`)

P0-2 residual polish on fire-hit LIVE-CLOSED `7ab099c` / seq `dd988da` /
early-Z `e6e6cb4` (KEEP). SHA `ff43a68`. Pushed to `origin/main`, Hetzner
`/home/grok/GoldenEye` pull + `make -C native wasm` + `silveriris-vite`
restart. Live wasm is this SHA (**259544**, Last-Modified 20:07:26 GMT).

Hoist `keep_albedo` per triangle, run perspective ST after the z test, stamp
clipped pixels without a second bounds/z check. Seq mix skips dead voices
and silent seq. G1 greyscale unchanged. Spawn_fill KEEP (handle_xrange 49/52,
neck_gap=7, mihok_void left_clear=0). Native unopt play_spawn 56.5ms;
long_walk 35.3ms; firehit miss 24.12 / hit 24.13 `guard_fire_ms=1.34`
after ~79ms dead=1. Not ASP.

**1 — live walk / fire.** Hard-refresh Chrome `?ff_netplay=0`, pack
`fff814d2…`. Spawn HUD `x=-98.5 z=-2358.4 y=29.1 θ270 seq=1`. Idle rAF
**149 / 3517ms ≈ 42.4 fps**; walk KeyW ≈**35.3 fps** (124 / 3515ms) to
x=−573.3 last_sfx=15 seq=1. Tab did not freeze. Same class as Mac 1547
(idle 43.1 / walk 35.7) — not a freeze and not a Facility close.
`.local/mihok-chrome-playtest-20260902-1608.md`. Fire-at-first-enemy after
this wasm: stall θ261 hits 1 kills 1 mag 7→6, post-hit rAF **178 / 3500ms
≈ 50.9 fps**, no freeze. `.local/mihok-chrome-playtest-20260902-1609.md`.

**2 — hitch / KEEP.** Stall xz=−98.5,−2358.4. Handles/Chead/void/fire-hit
untouched. Facility CDP :9226 still unreachable (LAN / Parallels /
tailscale :9226 closed). Native g1 greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.
player/gun/audio-test green.

**Remaining holes**

- P0-1 fire-hit LIVE-CLOSED on `7ab099c` (re-proved on `ff43a68` wasm:
  stall θ261 hit 1 / kill 1, post-hit ≈51 fps). Do not reopen as walking
  freeze.
- P0-2 live spawn-idle / walk rAF still required on the Facility box.
  Mac live idle ~42 / walk ~35 (seq + early-Z + hoist) does not close
  `ff37828`. Facility-box CDP :9226 was not reachable this pass.
  Overnight box idle 24.5–28.8 (0446/0612/1147) still the last Facility
  samples — cite, do not claim CLOSED.
- P0-2 doors: Mihok 1032/1215 live-closed spawn/θ263 — brown 686-688
  ribs + two 685 highlight handle bars. Live 1608 still shows both.
  Keep closed unless a later pass regresses ribs or bars.
- P0-3 LIVE-CLOSED on `8cc1ce1` (θ263 neck_gap=7). KEEP stitched-neck
  DL; no model-Y fudge.
- Missing-neighbor LIVE-CLOSED on `8cc1ce1` at −161,−2382 θ290. Do not
  walk stacked r6.
- Full ASP HLE still out (envelopes, RSP mixer, spatial). Music is pack
  compact MIDI plus instruments.tbl PCM `9e225af` (not ASP). Footsteps
  remain mixer placeholder `3c97070` (live last_sfx=15 while walking).
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-02 instruments wavetable `9e225af`)

P0-3 music slice on fire-hit LIVE-CLOSED `7ab099c` / seq `dd988da` /
early-Z `e6e6cb4` / hoist `ff43a68` (KEEP). SHA `9e225af`. Pushed to
`origin/main`, Hetzner `/home/grok/GoldenEye` pull + `make -C native wasm`
+ `silveriris-vite` restart. Live wasm is this SHA (**264233**,
Last-Modified 20:28:08 GMT).

Decode pack `instruments.ctl` / `instruments.tbl` VADPCM to host PCM (106
unique waves). Seq notes pick by MIDI program + ALKeyMap, pitch by key vs
keyBase, loop ALADPCMloop. Triangle fallback without a bank. HUD `seq 1w`
when wavetable is on. Not ASP HLE (no envelopes, RSP mixer, or spatial).
G1 greyscale unchanged. Native firehit miss 24.61 / hit 23.74
`guard_fire_ms=1.28` after ~79ms dead=1 hits=2 kills=1. Pack smoke
seq_on=1 inst_on=1.

**1 — live walk / fire.** Hard-refresh Chrome `?ff_netplay=0`, pack
`fff814d2…`. Spawn HUD `x=-98.5 z=-2358.4 y=29.1 θ270` **seq 1w**. Idle rAF
**151 / 3517ms ≈ 42.9 fps**; walk KeyW ≈**35.4 fps** (125 / 3533ms) to
x=−573.3 last_sfx=15 seq=1 wav=1. Tab did not freeze. Same class as Mac 1608
(idle 42.4 / walk 35.3) — wavetable is not a freeze.
`.local/mihok-chrome-playtest-20260902-1630.md`. Fire-at-first-enemy after
this wasm: stall θ261 hits 1 kills 1 mag 7→6, post-hit rAF **177 / 3500ms
≈ 50.6 fps**, no freeze. `.local/mihok-chrome-playtest-20260902-1631.md`.

**2 — hitch / KEEP.** Stall xz=−98.5,−2358.4. Handles/Chead/void/fire-hit
untouched. Facility CDP :9226 still unreachable (LAN / Parallels /
tailscale :9226 closed). Native g1 greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.
player/gun/audio-test green (seq.pcm KEEP; wavetable distinct=1 restore=1).

**Remaining holes**

- P0-1 fire-hit LIVE-CLOSED on `7ab099c` (re-proved on `9e225af` wasm:
  stall θ261 hit 1 / kill 1, post-hit ≈51 fps). Do not reopen as walking
  freeze.
- P0-2 live spawn-idle / walk rAF still required on the Facility box.
  Mac live idle ~43 / walk ~35 (seq + wavetable) does not close
  `ff37828`. Facility-box CDP :9226 was not reachable this pass.
  Overnight box idle 24.5–28.8 (0446/0612/1147) still the last Facility
  samples — cite, do not claim CLOSED.
- P0-2 doors: Mihok 1032/1215 live-closed spawn/θ263 — brown 686-688
  ribs + two 685 highlight handle bars. Live 1630 still shows both.
  Keep closed unless a later pass regresses ribs or bars.
- P0-3 LIVE-CLOSED on `8cc1ce1` (θ263 neck_gap=7). KEEP stitched-neck
  DL; no model-Y fudge.
- Missing-neighbor LIVE-CLOSED on `8cc1ce1` at −161,−2382 θ290. Do not
  walk stacked r6.
- Full ASP HLE still out (envelopes, RSP mixer, spatial). Music is pack
  compact MIDI plus instruments.tbl PCM `9e225af`, not ASP. Footsteps
  remain mixer placeholder `3c97070` (live last_sfx=15 while walking).
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-02 seq ALEnvelope `ec00f5b`)

P0-3 music slice on fire-hit LIVE-CLOSED `7ab099c` / seq `dd988da` /
wavetable `9e225af` / early-Z `e6e6cb4` / hoist `ff43a68` (KEEP). SHA
`ec00f5b` (HUD `4a58e25` seq 1e). Pushed to `origin/main`, Hetzner
`/home/grok/GoldenEye` pull + `make -C native wasm` + `silveriris-vite`
restart. Live wasm is `ec00f5b` (**267781**, Last-Modified 20:48:15 GMT).

Apply pack `instruments.ctl` ALEnvelope attack/decay/release on seq
wavetable voices. decay_us==-1 sustains until note-off. Default
(0,-1,0,127,127) keeps the pre-envelope mix. HUD `seq 1e` when envelopes
are loaded (320-wide; `1we` cropped the `e`). Not ASP HLE (no RSP mixer
or spatial). G1 greyscale unchanged. Native firehit miss 24.70 / hit 24.15
`guard_fire_ms=1.33` after ~79ms dead=1 hits=2 kills=1. Pack ctl: 138/138
sounds have a non-default envelope (mostly 2ms release). Footsteps stay
mixer thud `3c97070`.

**1 — live walk / fire.** Hard-refresh Chrome `?ff_netplay=0`, pack
`fff814d2…`. Spawn HUD `x=-98.5 z=-2358.4 y=29.1 θ270` wasm **seq=1 wav=1
env=1**. Idle rAF **153 / 3533ms ≈ 43.3 fps**; walk KeyW ≈**36.4 fps**
(128 / 3517ms) to x=−573.3 last_sfx=15. Tab did not freeze. Same class as
Mac 1630 (idle 42.9 / walk 35.4) — envelope is not a freeze.
`.local/mihok-chrome-playtest-20260902-1649.md`. Fire-at-first-enemy after
this wasm + HUD 1e: stall θ261 **seq 1e** hits 1 kills 1 mag 7→6, post-hit
rAF **182 / 3517ms ≈ 51.8 fps**, no freeze.
`.local/mihok-chrome-playtest-20260902-1652.md`. Chris 14:21 pre-fix freeze
was wasm 252947 @ 18:18 GMT (hit 1, Incognito ~101% CPU) — still not a
`7ab099c` result.

**2 — hitch / KEEP.** Stall xz=−98.5,−2358.4. Handles/Chead/void/fire-hit
untouched. Facility CDP :9226 still unreachable (LAN / Parallels /
tailscale :9226 closed). Native g1 greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.
player/gun/audio-test green (seq.pcm KEEP; wavetable distinct=1 restore=1;
envelope distinct=1 restore=1).

**Remaining holes**

- P0-1 fire-hit LIVE-CLOSED on `7ab099c` (re-proved on `ec00f5b` wasm:
  stall θ261 hit 1 / kill 1, post-hit ≈52 fps). Do not reopen as walking
  freeze. Evidence: Chris 14:21
  `.local/mihok-chrome-playtest-chris-fire-freeze-20260902-1421.md`;
  fix SHA `7ab099c`; latest live re-prove 1652.
- P0-2 live spawn-idle / walk rAF still required on the Facility box.
  Mac live idle ~43 / walk ~36 (seq + wavetable + envelope) does not close
  `ff37828`. Facility-box CDP :9226 was not reachable this pass.
  Overnight box idle 24.5–28.8 (0446/0612/1147) still the last Facility
  samples — cite, do not claim CLOSED.
- P0-2 doors: Mihok 1032/1215 live-closed spawn/θ263 — brown 686-688
  ribs + two 685 highlight handle bars. Live 1649/1652 still shows both.
  Keep closed unless a later pass regresses ribs or bars.
- P0-3 LIVE-CLOSED on `8cc1ce1` (θ263 neck_gap=7). KEEP stitched-neck
  DL; no model-Y fudge.
- Missing-neighbor LIVE-CLOSED on `8cc1ce1` at −161,−2382 θ290. Do not
  walk stacked r6.
- Full ASP HLE still out (RSP mixer, spatial). Music is pack compact MIDI
  plus instruments.tbl PCM plus ALEnvelope `ec00f5b`, not ASP. Footsteps
  remain mixer placeholder `3c97070` (live last_sfx=15 while walking).
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-02 seq pan `82e68d3`)

P0-3 music slice on fire-hit LIVE-CLOSED `7ab099c` / seq `dd988da` /
wavetable `9e225af` / envelope `ec00f5b` / early-Z `e6e6cb4` / hoist
`ff43a68` (KEEP). SHA `82e68d3`. Pushed to `origin/main`, Hetzner
`/home/grok/GoldenEye` pull + `make -C native wasm` + `silveriris-vite`
restart. Live wasm is this SHA (**268543**, Last-Modified 21:06:10 GMT).

Apply pack `instruments.ctl` ALSound.samplePan and MIDI CC10 on seq
voices. Center-unity at pan 64 keeps both channels full (seq.pcm KEEP).
Pack: 23/138 sounds non-center; all 75 inst pans are 64; Mfacility.bin
has 13 CC10 events. HUD stays `seq 1e` (env still on; 320-wide). Not ASP
HLE (no RSP mixer or spatial SFX). G1 greyscale unchanged. Native firehit
miss 24.20 / hit 23.88 `guard_fire_ms=1.29` after ~79ms dead=1 hits=2
kills=1. Footsteps stay mixer thud `3c97070`.

**1 — live walk / fire.** Hard-refresh Chrome `?ff_netplay=0`, pack
`fff814d2…`. Spawn HUD `x=-98.5 z=-2358.4 y=29.1 θ270` **seq 1e** wasm
seq=1 wav=1 env=1 pan=1. Idle rAF **176 / 3517ms ≈ 50.0 fps**; walk KeyW
≈**41.6 fps** (147 / 3533ms) to x=−573.3 last_sfx=15. Tab did not freeze.
Same class as Mac 1649 (idle 43.3 / walk 36.4) — pan is not a freeze.
`.local/mihok-chrome-playtest-20260902-1710.md`. Fire-at-first-enemy after
this wasm: stall θ261 **seq 1e** hits 1 kills 1 mag 7→6, post-hit rAF
**205 / 3500ms ≈ 58.6 fps**, no freeze.
`.local/mihok-chrome-playtest-20260902-1711.md`. Chris 14:21 pre-fix freeze
was wasm 252947 @ 18:18 GMT (hit 1, Incognito ~101% CPU) — still not a
`7ab099c` result.

**2 — hitch / KEEP.** Stall xz=−98.5,−2358.4. Handles/Chead/void/fire-hit
untouched. Facility CDP :9226 still unreachable (LAN / Parallels /
tailscale :9226 closed). Native g1 greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.
player/gun/audio-test green (seq.pcm KEEP; wavetable distinct=1 restore=1;
envelope distinct=1 restore=1; pan distinct=1 restore=1; cc10 left=1).

**Remaining holes**

- P0-1 fire-hit LIVE-CLOSED on `7ab099c` (re-proved on `82e68d3` wasm:
  stall θ261 hit 1 / kill 1, post-hit ≈59 fps). Do not reopen as walking
  freeze. Evidence: Chris 14:21
  `.local/mihok-chrome-playtest-chris-fire-freeze-20260902-1421.md`;
  fix SHA `7ab099c`; latest live re-prove 1711.
- P0-2 live spawn-idle / walk rAF still required on the Facility box.
  Mac live idle ~50 / walk ~42 (seq + wavetable + envelope + pan) does
  not close `ff37828`. Facility-box CDP :9226 was not reachable this
  pass. Overnight box idle 24.5–28.8 (0446/0612/1147) still the last
  Facility samples — cite, do not claim CLOSED.
- P0-2 doors: Mihok 1032/1215 live-closed spawn/θ263 — brown 686-688
  ribs + two 685 highlight handle bars. Live 1710/1711 still shows both.
  Keep closed unless a later pass regresses ribs or bars.
- P0-3 LIVE-CLOSED on `8cc1ce1` (θ263 neck_gap=7). KEEP stitched-neck
  DL; no model-Y fudge.
- Missing-neighbor LIVE-CLOSED on `8cc1ce1` at −161,−2382 θ290. Do not
  walk stacked r6.
- Full ASP HLE still out (RSP mixer, spatial SFX). Music is pack compact
  MIDI plus instruments.tbl PCM plus ALEnvelope plus ALSound/CC10 pan
  `82e68d3`, not ASP. Footsteps remain mixer placeholder `3c97070`
  (live last_sfx=15 while walking).
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-02 spat SFX pan `67b7bce`)

P0-3 mixer pan slice on fire-hit LIVE-CLOSED `7ab099c` / seq pan `82e68d3` /
envelope `ec00f5b` / wavetable `9e225af` / early-Z `e6e6cb4` / hoist
`ff43a68` (KEEP). SHA `67b7bce` (CDP hook `ba8f316`). Pushed to
`origin/main`, Hetzner `/home/grok/GoldenEye` pull + `make -C native wasm`
+ `silveriris-vite` restart. Live wasm is `67b7bce` (**270820**,
Last-Modified 21:23:30 GMT).

World SFX (hit/yelp/fall/rico) take mixer pan from Bond look vs impact xz.
Walk steps alternate pan 48/80. Center-unity at 64 keeps gun.pcm. HUD stays
`seq 1e`. `port_audio_spat_on` is 1 while an SFX voice is off-center. Not
ASP HLE (no RSP mixer). G1 greyscale unchanged. Native firehit miss 23.82 /
hit 24.10 `guard_fire_ms=1.34` after ~78ms dead=1 hits=2 kills=1. Footsteps
stay mixer thud `3c97070` with L/R pan (step.pcm updated).

**1 — live walk / fire.** Hard-refresh Chrome `?ff_netplay=0`, pack
`fff814d2…`. Spawn HUD `x=-98.5 z=-2358.4 y=29.1 θ270` **seq 1e** wasm
seq=1 wav=1 env=1 pan=1. Idle rAF **177 / 3513ms ≈ 50.4 fps**; walk KeyW
≈**42.7 fps** (150 / 3510ms) to x=−573.3 last_sfx=15 **spat=1**. Tab did
not freeze. Same class as Mac 1710 (idle 50.0 / walk 41.6) — spat pan is
not a freeze. `.local/mihok-chrome-playtest-20260902-1727.md`.
Fire-at-first-enemy after this wasm: stall θ270 **seq 1e** hits 1 kills 1
mag 7→6, post-hit rAF **203 / 3517ms ≈ 57.7 fps**, no freeze.
`.local/mihok-chrome-playtest-20260902-1728.md`. Chris 14:21 pre-fix freeze
was wasm 252947 @ 18:18 GMT (hit 1, Incognito ~101% CPU) — still not a
`7ab099c` result.

**2 — hitch / KEEP.** Stall xz=−98.5,−2358.4. Handles/Chead/void/fire-hit
untouched. Facility CDP :9226 still unreachable (LAN / Parallels /
tailscale :9226 closed). Native g1 greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.
player/gun/audio-test green (seq.pcm KEEP; wavetable distinct=1 restore=1;
envelope distinct=1 restore=1; pan distinct=1 restore=1; cc10 left=1;
spat sfx distinct=1 restore=1; step pan lr=1). play_spawn frame_ms=56.74;
long_walk 35.08; long_walk_hall 45.75; handle_xrange 49/52; neck_gap=7;
mihok_void left_clear=0.

**Remaining holes**

- P0-1 fire-hit LIVE-CLOSED on `7ab099c` (re-proved on `67b7bce` wasm:
  stall θ270 hit 1 / kill 1, post-hit ≈58 fps). Do not reopen as walking
  freeze. Evidence: Chris 14:21
  `.local/mihok-chrome-playtest-chris-fire-freeze-20260902-1421.md`;
  fix SHA `7ab099c`; latest live re-prove 1728.
- P0-2 live spawn-idle / walk rAF still required on the Facility box.
  Mac live idle ~50 / walk ~43 (seq + wavetable + envelope + pan + spat)
  does not close `ff37828`. Facility-box CDP :9226 was not reachable this
  pass. Overnight box idle 24.5–28.8 (0446/0612/1147) still the last
  Facility samples — cite, do not claim CLOSED.
- P0-2 doors: Mihok 1032/1215 live-closed spawn/θ263 — brown 686-688
  ribs + two 685 highlight handle bars. Live 1727/1728 still shows both.
  Keep closed unless a later pass regresses ribs or bars.
- P0-3 LIVE-CLOSED on `8cc1ce1` (θ263 neck_gap=7). KEEP stitched-neck
  DL; no model-Y fudge.
- Missing-neighbor LIVE-CLOSED on `8cc1ce1` at −161,−2382 θ290. Do not
  walk stacked r6.
- Full ASP HLE still out (RSP mixer). Music is pack compact MIDI plus
  instruments.tbl PCM plus ALEnvelope plus ALSound/CC10 pan `82e68d3`,
  not ASP. World SFX mixer pan + L/R steps `67b7bce` (live spat=1 while
  walking, last_sfx=15). Not distance/RSP spatial.
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-02 dist SFX vol `f134481`)

P0-3 mixer distance slice on fire-hit LIVE-CLOSED `7ab099c` / spat pan
`67b7bce` / seq pan `82e68d3` / envelope `ec00f5b` / wavetable `9e225af` /
early-Z `e6e6cb4` / hoist `ff43a68` (KEEP). SHA `f134481`. Pushed to
`origin/main`, Hetzner `/home/grok/GoldenEye` pull + `make -C native wasm`
+ `silveriris-vite` restart. Live wasm is `f134481` (**270964**,
Last-Modified 21:39:58 GMT).

World SFX (hit/yelp/fall/rico) take mixer vol from Bond xz vs impact.
Full within 400u so stall first-enemy (~254u) stays loud; floor 32 at
4000u. 127 keeps gun.pcm. HUD stays `seq 1e`. `port_audio_dist_on` is 1
while a voice plays at vol < 127. Not ASP HLE (no RSP mixer). G1 greyscale
unchanged. Native firehit miss 24.45 / hit 23.51 `guard_fire_ms=1.30`
after ~78ms dead=1 hits=2 kills=1. Footsteps stay mixer thud `3c97070`
(full vol; L/R pan from `67b7bce`).

**1 — live walk / fire.** Hard-refresh Chrome `?ff_netplay=0`, pack
`fff814d2…`. Spawn HUD `x=-98.5 z=-2358.4 y=29.1 θ270` **seq 1e** wasm
seq=1 wav=1 env=1 pan=1. Idle rAF **153 / 3505ms ≈ 43.7 fps**; walk KeyW
≈**35.9 fps** (126 / 3508ms) to x=−573.3 last_sfx=15 **spat=1 dist=0**.
Tab did not freeze. Same class as Mac 1649 (idle 43.3 / walk 36.4) and
1727 (idle 50.4 / walk 42.7) — dist vol is not a freeze.
`.local/mihok-chrome-playtest-20260902-1740.md`. Fire-at-first-enemy after
this wasm: stall θ270 **seq 1e** hits 1 kills 1 mag 7→6, post-hit rAF
**180 / 3507ms ≈ 51.3 fps**, no freeze. dist=0 (near < 400u, full vol).
`.local/mihok-chrome-playtest-20260902-1741.md`. Chris 14:21 pre-fix freeze
was wasm 252947 @ 18:18 GMT (hit 1, Incognito ~101% CPU) — still not a
`7ab099c` result.

**2 — hitch / KEEP.** Stall xz=−98.5,−2358.4. Handles/Chead/void/fire-hit
untouched. Facility CDP :9226 still unreachable (LAN / Parallels /
tailscale :9226 closed). Native g1 greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.
player/gun/audio-test green (seq.pcm KEEP; wavetable distinct=1 restore=1;
envelope distinct=1 restore=1; pan distinct=1 restore=1; cc10 left=1;
spat sfx distinct=1 restore=1; step pan lr=1; dist sfx distinct=1
restore=1). This pass did not re-run long_walk / handle_xrange / neck_gap
(audio-only slice).

**Remaining holes**

- P0-1 fire-hit LIVE-CLOSED on `7ab099c` (re-proved on `f134481` wasm:
  stall θ270 hit 1 / kill 1, post-hit ≈51 fps). Do not reopen as walking
  freeze. Evidence: Chris 14:21
  `.local/mihok-chrome-playtest-chris-fire-freeze-20260902-1421.md`;
  fix SHA `7ab099c`; latest live re-prove 1741.
- P0-2 live spawn-idle / walk rAF still required on the Facility box.
  Mac live idle ~44 / walk ~36 (seq + wavetable + envelope + pan + spat
  + dist) does not close `ff37828`. Facility-box CDP :9226 was not
  reachable this pass. Overnight box idle 24.5–28.8 (0446/0612/1147)
  still the last Facility samples — cite, do not claim CLOSED.
- P0-2 doors: Mihok 1032/1215 live-closed spawn/θ263 — brown 686-688
  ribs + two 685 highlight handle bars. Live 1740/1741 still shows both.
  Keep closed unless a later pass regresses ribs or bars.
- P0-3 LIVE-CLOSED on `8cc1ce1` (θ263 neck_gap=7). KEEP stitched-neck
  DL; no model-Y fudge.
- Missing-neighbor LIVE-CLOSED on `8cc1ce1` at −161,−2382 θ290. Do not
  walk stacked r6.
- Full ASP HLE still out (RSP mixer). Music is pack compact MIDI plus
  instruments.tbl PCM plus ALEnvelope plus ALSound/CC10 pan `82e68d3`,
  not ASP. World SFX mixer pan `67b7bce` + distance vol `f134481` (live
  spat=1 / dist=0 while walking, last_sfx=15; stall fire dist=0 because
  extra-idle is inside 400u). Not RSP spatial. Footsteps remain mixer
  placeholder `3c97070`.
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-02 P0-1 evidence + Mac walk rAF `1efb61c`)

P0-1 first-enemy Chrome freeze stays LIVE-CLOSED on `7ab099c` (no new wasm).
Live wasm is still `f134481` (**270964**, Last-Modified 21:39:58 GMT).
Hard-refresh Chrome `?ff_netplay=0`, pack `fff814d2…`.

Chris 14:21 was the **pre-fix** freeze (wasm **252947** @ 18:18 GMT, hit 1,
Incognito ~101% CPU). Do not treat 14:21 as a `7ab099c` / `f134481` result.
`.local/mihok-chrome-playtest-chris-fire-freeze-20260902-1421.md`

**1 — fire-hit LIVE re-prove.** Stall HUD `x=-98.5 z=-2358.4 y=29.1 θ270`
**seq 1e**. First KeyF: hits 1 kills 1 mag 7→6 sfx=1. Guard 0 dead=1.
Post-hit rAF **178 / 3500ms ≈ 50.9 fps**. Tab did not freeze. hp stayed 8.
`.local/mihok-chrome-playtest-20260902-1747.md`. Do not reopen as P0#1.

**2 — walk rAF this Mac only.** Idle rAF **152 / 3517ms ≈ 43.2 fps**; walk
KeyW ≈**36.0 fps** (126 / 3500ms) to x=−573.3 last_sfx=15 spat=1 dist=0.
Tab did not freeze. `.local/mihok-chrome-playtest-20260902-1746.md`.
Facility-box CDP :9226 still unreachable (LAN 192.168.1.0/24 / Parallels
10.211.55 / tailscale linux :9226 closed). Overnight box idle 24.5–28.8
(0446/0612/1147) still the last Facility samples — cite, do not close
`ff37828`. No ASP slice (clock-out).

**Remaining holes**

- P0-1 fire-hit LIVE-CLOSED on `7ab099c` (re-proved on `f134481` wasm:
  stall θ270 hit 1 / kill 1, post-hit ≈51 fps). Do not reopen as walking
  freeze. Evidence: Chris 14:21
  `.local/mihok-chrome-playtest-chris-fire-freeze-20260902-1421.md`;
  fix SHA `7ab099c`; latest live re-prove 1747.
- P0-2 live spawn-idle / walk rAF still required on the Facility box.
  Mac live idle ~43 / walk ~36 (seq + wavetable + envelope + pan + spat
  + dist) does not close `ff37828`. Facility-box CDP :9226 was not
  reachable this pass. Overnight box idle 24.5–28.8 (0446/0612/1147)
  still the last Facility samples — cite, do not claim CLOSED.
- P0-2 doors: Mihok 1032/1215 live-closed spawn/θ263 — brown 686-688
  ribs + two 685 highlight handle bars. Live 1746/1747 still shows both.
  Keep closed unless a later pass regresses ribs or bars.
- P0-3 LIVE-CLOSED on `8cc1ce1` (θ263 neck_gap=7). KEEP stitched-neck
  DL; no model-Y fudge.
- Missing-neighbor LIVE-CLOSED on `8cc1ce1` at −161,−2382 θ290. Do not
  walk stacked r6.
- Full ASP HLE still out (RSP mixer). Music is pack compact MIDI plus
  instruments.tbl PCM plus ALEnvelope plus ALSound/CC10 pan `82e68d3`,
  not ASP. World SFX mixer pan `67b7bce` + distance vol `f134481` (live
  spat=1 / dist=0 while walking, last_sfx=15; stall fire dist=0 because
  extra-idle is inside 400u). Not RSP spatial. Footsteps remain mixer
  placeholder `3c97070`.
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-02 seq ALKeyMap detune `a01f1bf`)

P0-2 music slice on fire-hit LIVE-CLOSED `7ab099c` / dist vol `f134481` /
spat pan `67b7bce` / seq pan `82e68d3` / envelope `ec00f5b` / wavetable
`9e225af` / early-Z `e6e6cb4` / hoist `ff43a68` (KEEP). SHA `a01f1bf`.
Pushed to `origin/main`, Hetzner `/home/grok/GoldenEye` pull + `make -C
native wasm` + `silveriris-vite` restart. Live wasm is this SHA (**272239**,
Last-Modified 23:07:35 GMT).

Apply pack `instruments.ctl` ALKeyMap.detune cents on seq wavetable pitch.
0 keeps the 12-TET `pitch_step` (seq.pcm KEEP). Pack: 46/138 sounds
non-zero (typical ±40 cents). HUD stays `seq 1e` (320-wide).
`port_audio_det_on` is 1 when any InstSound has detune≠0. Not ASP HLE
(no RSP mixer). G1 greyscale unchanged. Native firehit miss 24.58 / hit
24.31 `guard_fire_ms=1.45` after ~80ms dead=1 hits=2 kills=1. Pack smoke
seq_on=1 inst_on=1 env_on=1 pan_on=1 det_on=1. Footsteps stay mixer thud
`3c97070`.

**1 — live walk / fire.** Hard-refresh Chrome `?ff_netplay=0`, pack
`fff814d2…`. Spawn HUD `x=-98.5 z=-2358.4 y=29.1 θ270` **seq 1e** wasm
seq=1 wav=1 env=1 pan=1 **det=1**. Idle rAF **149 / 3501ms ≈ 42.6 fps**;
walk KeyW ≈**35.0 fps** (123 / 3516ms) to x=−573.3 last_sfx=15 **spat=1
dist=0 det=1**. Tab did not freeze. Same class as Mac 1746 (idle 43.2 /
walk 36.0) — detune is not a freeze.
`.local/mihok-chrome-playtest-20260902-1908.md`. Fire-at-first-enemy after
this wasm: stall θ270 **seq 1e** hits 1 kills 1 mag 7→6, post-hit rAF
**177 / 3501ms ≈ 50.6 fps**, no freeze.
`.local/mihok-chrome-playtest-20260902-1909.md`. Chris 14:21 pre-fix freeze
was wasm 252947 @ 18:18 GMT (hit 1, Incognito ~101% CPU) — still not a
`7ab099c` result.

**2 — hitch / KEEP.** Stall xz=−98.5,−2358.4. Handles/Chead/void/fire-hit
untouched. Facility CDP :9226 still unreachable (LAN / Parallels /
tailscale :9226 closed). Native g1 greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.
player/gun/audio-test green (seq.pcm KEEP; wavetable distinct=1 restore=1;
envelope distinct=1 restore=1; pan distinct=1 restore=1; cc10 left=1;
spat sfx distinct=1 restore=1; step pan lr=1; dist sfx distinct=1
restore=1; detune distinct=1 restore=1). No hitch ship this pass
(audio-only). This pass did not re-run long_walk / handle_xrange / neck_gap.

**Remaining holes**

- P0-1 fire-hit LIVE-CLOSED on `7ab099c` (re-proved on `a01f1bf` wasm:
  stall θ270 hit 1 / kill 1, post-hit ≈51 fps). Do not reopen as walking
  freeze. Evidence: Chris 14:21
  `.local/mihok-chrome-playtest-chris-fire-freeze-20260902-1421.md`;
  fix SHA `7ab099c`; latest live re-prove 1909.
- P0-2 live spawn-idle / walk rAF still required on the Facility box.
  Mac live idle ~43 / walk ~35 (seq + wavetable + envelope + pan + spat
  + dist + detune) does not close `ff37828`. Facility-box CDP :9226 was
  not reachable this pass. Overnight box idle 24.5–28.8 (0446/0612/1147)
  still the last Facility samples — cite, do not claim CLOSED. No remaining
  hitch ship; audio-only.
- P0-2 doors: Mihok 1032/1215 live-closed spawn/θ263 — brown 686-688
  ribs + two 685 highlight handle bars. Live 1908/1909 still shows both.
  Keep closed unless a later pass regresses ribs or bars.
- P0-3 LIVE-CLOSED on `8cc1ce1` (θ263 neck_gap=7). KEEP stitched-neck
  DL; no model-Y fudge.
- Missing-neighbor LIVE-CLOSED on `8cc1ce1` at −161,−2382 θ290. Do not
  walk stacked r6.
- Full ASP HLE still out (RSP mixer). Music is pack compact MIDI plus
  instruments.tbl PCM plus ALEnvelope plus ALSound/CC10 pan `82e68d3`
  plus ALKeyMap.detune `a01f1bf`, not ASP. World SFX mixer pan `67b7bce`
  + distance vol `f134481` (live spat=1 / dist=0 while walking, last_sfx=15;
  stall fire dist=0 because extra-idle is inside 400u). Not RSP spatial.
  Footsteps remain mixer placeholder `3c97070`.
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-02 door spat `2bd608e`)

P0-2 spat slice on detune `a01f1bf` / dist vol `f134481` / spat pan
`67b7bce` / fire-hit LIVE-CLOSED `7ab099c` (KEEP). SHA `2bd608e`. Pushed to
`origin/main`, Hetzner `/home/grok/GoldenEye` pull + `make -C native wasm`
+ `silveriris-vite` restart. Live wasm is this SHA (**272409**,
Last-Modified 23:17:58 GMT).

Door open/close take the same mixer pan + distance vol as world SFX, from
the used door xz (`port_stan_last_use_xz`). Center/full when the leaf is
ahead and within 400u. HUD stays `seq 1e`. Not ASP HLE. G1 greyscale
unchanged. Native firehit miss 24.35 / hit 24.23 `guard_fire_ms=1.29`
dead=1 hits=2 kills=1. player-test last-use xz KEEP. Footsteps stay mixer
thud `3c97070`.

**1 — live walk / fire.** Hard-refresh Chrome `?ff_netplay=0`, pack
`fff814d2…`. Spawn HUD `x=-98.5 z=-2358.4 y=29.1 θ270` **seq 1e** wasm
seq=1 wav=1 env=1 pan=1 det=1. Idle rAF **153 / 3502ms ≈ 43.7 fps**; walk
KeyW ≈**36.3 fps** (127 / 3500ms) to x=−573.3 last_sfx=15 det=1. Tab did
not freeze. Same class as Mac 1908 (idle 42.6 / walk 35.0). Stall KeyZ did
not latch (last_sfx=0; extra-idle is the look target, A-use range 200u).
Door spat is the same `port_sfx_play_world` path as hit/yelp.
`.local/mihok-chrome-playtest-20260902-1918.md`. Fire-at-first-enemy after
this wasm: stall θ270 hits 1 kills 1 mag 7→6, post-hit rAF **180 / 3500ms
≈ 51.4 fps**, no freeze.
`.local/mihok-chrome-playtest-20260902-1919.md`.

**2 — hitch / KEEP.** Stall xz=−98.5,−2358.4. Handles/Chead/void/fire-hit
untouched. Facility CDP :9226 still unreachable. Native g1 greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.
player/gun/audio-test green (seq.pcm KEEP; detune distinct=1 restore=1).

**Remaining holes**

- P0-1 fire-hit LIVE-CLOSED on `7ab099c` (re-proved on `2bd608e` wasm:
  stall θ270 hit 1 / kill 1, post-hit ≈51 fps). Do not reopen as walking
  freeze. Evidence: Chris 14:21; fix SHA `7ab099c`; latest live re-prove
  1919.
- P0-2 live spawn-idle / walk rAF still required on the Facility box.
  Mac live idle ~44 / walk ~36 does not close `ff37828`. Facility-box CDP
  :9226 was not reachable this pass. Overnight box idle 24.5–28.8
  (0446/0612/1147) still the last Facility samples — cite, do not claim
  CLOSED.
- P0-2 doors: Mihok 1032/1215 live-closed spawn/θ263 — brown 686-688
  ribs + two 685 highlight handle bars. Live 1918/1919 still shows both.
  Keep closed unless a later pass regresses ribs or bars.
- P0-3 LIVE-CLOSED on `8cc1ce1` (θ263 neck_gap=7). KEEP stitched-neck
  DL; no model-Y fudge.
- Missing-neighbor LIVE-CLOSED on `8cc1ce1` at −161,−2382 θ290. Do not
  walk stacked r6.
- Full ASP HLE still out (RSP mixer). Music is pack compact MIDI plus
  instruments.tbl PCM plus ALEnvelope plus ALSound/CC10 pan plus
  ALKeyMap.detune `a01f1bf`, not ASP. World SFX mixer pan + distance vol;
  door open/close now use the same spat `2bd608e`. Footsteps remain mixer
  placeholder `3c97070` (L/R pan). Not RSP spatial.
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-02 seq MIDI pitch bend `2d75c43`)

P0-2 music slice on door spat `2bd608e` / detune `a01f1bf` / dist vol `f134481` /
spat pan `67b7bce` / fire-hit LIVE-CLOSED `7ab099c` (KEEP). SHA `2d75c43`.
Pushed to `origin/main`, Hetzner `/home/grok/GoldenEye` pull + `make -C native
wasm` + `silveriris-vite` restart. Live wasm is this SHA (**270131**,
Last-Modified 23:37:57 GMT).

Apply compact-MIDI pitch bend (14-bit, center 8192) times pack
`instruments.ctl` ALInstrument.bendRange (N64 default 200; two unused progs
are 1200). Center is a no-op so seq.pcm / unbent wavetable stay KEEP. Facility
`Mfacility.bin` has **70** pitch-bend events on MIDI ch 7 starting at **50s**.
Pack vibType is on progs 6/20 and tremType on 43/68 — none of those programs
are used in Facility, so vibrato would be a no-op here. HUD stays `seq 1e`
(320-wide). `port_audio_bend_on` is 1 while any channel is off center. Not ASP
HLE (no RSP mixer). G1 greyscale unchanged. Native firehit miss 25.00 / hit
23.80 `guard_fire_ms=1.39` after ~81ms dead=1 hits=2 kills=1. Pack smoke
seq_on=1 inst_on=1 env_on=1 pan_on=1 det_on=1 bend_on=0 (load is before 50s).
Footsteps stay mixer thud `3c97070`.

**1 — live walk / fire.** Hard-refresh Chrome `?ff_netplay=0`, pack
`fff814d2…`. Spawn HUD `x=-98.5 z=-2358.4 y=29.1 θ270` **seq 1e** wasm
seq=1 wav=1 env=1 pan=1 det=1 bend=0. Idle rAF **153 / 3500ms ≈ 43.7 fps**;
walk KeyW ≈**35.7 fps** (125 / 3501ms) to x=−573.3 last_sfx=15 det=1. Tab did
not freeze. Same class as Mac 1918 (idle 43.7 / walk 36.3) — pitch bend is not
a freeze. Live bend=0 is the 50s-to-first-event, not a miss.
`.local/mihok-chrome-playtest-20260902-1938.md`. Fire-at-first-enemy after
this wasm: stall θ270 hits 1 kills 1 mag 7→6, post-hit rAF **181 / 3500ms
≈ 51.7 fps**, no freeze.
`.local/mihok-chrome-playtest-20260902-1939.md`.

**2 — hitch / KEEP.** Stall xz=−98.5,−2358.4. Handles/Chead/void/fire-hit
untouched. Facility CDP :9226 still unreachable. Native g1 greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.
player/gun/audio-test green (seq.pcm KEEP; detune distinct=1 restore=1;
bend distinct=1 restore=1). No hitch ship this pass (audio-only).

**Remaining holes**

- P0-1 fire-hit LIVE-CLOSED on `7ab099c` (re-proved on `2d75c43` wasm:
  stall θ270 hit 1 / kill 1, post-hit ≈52 fps). Do not reopen as walking
  freeze. Evidence: Chris 14:21; fix SHA `7ab099c`; latest live re-prove
  1939.
- P0-2 live spawn-idle / walk rAF still required on the Facility box.
  Mac live idle ~44 / walk ~36 does not close `ff37828`. Facility-box CDP
  :9226 was not reachable this pass. Overnight box idle 24.5–28.8
  (0446/0612/1147) still the last Facility samples — cite, do not claim
  CLOSED.
- P0-2 doors: Mihok 1032/1215 live-closed spawn/θ263 — brown 686-688
  ribs + two 685 highlight handle bars. Live 1938/1939 still shows both.
  Keep closed unless a later pass regresses ribs or bars.
- P0-3 LIVE-CLOSED on `8cc1ce1` (θ263 neck_gap=7). KEEP stitched-neck
  DL; no model-Y fudge.
- Missing-neighbor LIVE-CLOSED on `8cc1ce1` at −161,−2382 θ290. Do not
  walk stacked r6.
- Full ASP HLE still out (RSP mixer). Music is pack compact MIDI plus
  instruments.tbl PCM plus ALEnvelope plus ALSound/CC10 pan plus
  ALKeyMap.detune plus MIDI pitch bend `2d75c43`, not ASP. World SFX mixer
  pan + distance vol; door open/close spat `2bd608e`. Footsteps remain mixer
  placeholder `3c97070` (L/R pan). Not RSP spatial.
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-02 8x8 tri reject + live CC7/CC10 `085c5a5`)

P0-1 hitch + P0-2 music slice on pitch-bend `2d75c43` / door spat `2bd608e` /
fire-hit LIVE-CLOSED `7ab099c` (KEEP). SHAs `a2a4a95` (8x8 reject) then
`085c5a5` (live CC7/CC10). Pushed to `origin/main`, Hetzner
`/home/grok/GoldenEye` pull + `make -C native wasm` + `silveriris-vite`
restart. Live wasm is `085c5a5` (**271574**, Last-Modified 00:02:43 GMT).

`draw_tri_raw` skips 8x8 pixel-center blocks whose four corners miss the
same half-plane (linear w; never drops an inside pixel). G1 greyscale
unchanged. Compact-MIDI CC7 volume and CC10 pan update sounding seq voices
(Rare csplayer; CC7 skips envelope RELEASE). Facility `Mfacility.bin` has
**35** CC7 events during notes (ch8 ramps 27–127) and **6** live CC10.
seq.pcm / unbent wavetable stay KEEP when CC is not mid-note. HUD stays
`seq 1e` (320-wide). Not ASP HLE (no RSP mixer). Native firehit miss 24.66 /
hit 23.90 `guard_fire_ms=1.35` after ~79ms dead=1 hits=2 kills=1. Pack smoke
seq_on=1 inst_on=1 env_on=1 pan_on=1 det_on=1 bend_on=0. play_spawn
frame_ms=56.29; long_walk 35.09. Footsteps stay mixer thud `3c97070`.

**1 — live walk / fire.** Hard-refresh Chrome `?ff_netplay=0`, pack
`fff814d2…`. Spawn HUD `x=-98.5 z=-2358.4 y=29.1 θ270` **seq 1e** wasm
seq=1 wav=1 env=1 pan=1 det=1 bend=0. Idle rAF **151 / 3517ms ≈ 42.9 fps**;
walk KeyW ≈**36.0 fps** (127 / 3532ms) to x=−573.3 last_sfx=15 spat=1. Tab did
not freeze. Same class as Mac 1938 (idle 43.7 / walk 35.7) — 8x8 reject +
live CC is not a freeze. `.local/mihok-chrome-playtest-20260902-2005.md`.
Fire-at-first-enemy after this wasm: stall θ270 hits 1 kills 1 mag 7→6,
post-hit rAF **178 / 3504ms ≈ 50.8 fps**, no freeze. Guard 0 dead=1.
`.local/mihok-chrome-playtest-20260902-2006.md`.

**2 — hitch / KEEP.** Stall xz=−98.5,−2358.4. Handles/Chead/void/fire-hit
untouched (spawn_fill handle_xrange 49/52, neck_gap=7, mihok_void
left_clear=0). Facility CDP :9226 still unreachable. Native g1 greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.
player/gun/audio-test green (seq.pcm KEEP; cc7 live distinct=1 drop=1;
cc10 live distinct=1).

**Remaining holes**

- P0-1 fire-hit LIVE-CLOSED on `7ab099c` (re-proved on `085c5a5` wasm:
  stall θ270 hit 1 / kill 1, post-hit ≈51 fps). Do not reopen as walking
  freeze. Evidence: Chris 14:21; fix SHA `7ab099c`; latest live re-prove
  2006.
- P0-2 live spawn-idle / walk rAF still required on the Facility box.
  Mac live idle ~43 / walk ~36 does not close `ff37828`. Facility-box CDP
  :9226 was not reachable this pass. Overnight box idle 24.5–28.8
  (0446/0612/1147) still the last Facility samples — cite, do not claim
  CLOSED.
- P0-2 doors: Mihok 1032/1215 live-closed spawn/θ263 — brown 686-688
  ribs + two 685 highlight handle bars. Live 2005/2006 still shows both.
  Keep closed unless a later pass regresses ribs or bars.
- P0-3 LIVE-CLOSED on `8cc1ce1` (θ263 neck_gap=7). KEEP stitched-neck
  DL; no model-Y fudge.
- Missing-neighbor LIVE-CLOSED on `8cc1ce1` at −161,−2382 θ290. Do not
  walk stacked r6.
- Full ASP HLE still out (RSP mixer). Music is pack compact MIDI plus
  instruments.tbl PCM plus ALEnvelope plus ALSound/CC10 pan plus
  ALKeyMap.detune plus MIDI pitch bend plus live CC7/CC10 `085c5a5`, not
  ASP. World SFX mixer pan + distance vol; door open/close spat `2bd608e`.
  Footsteps remain mixer placeholder `3c97070` (L/R pan). Not RSP spatial.
- Combat AI / matching engine later. Campaign out of v1.

---

## STATUS (2026-09-02 16x16 span + wavetable lerp `2e9916c`)

P0-1 hitch + P0-2 music slice on 8x8 reject `a2a4a95` / live CC7/CC10 `085c5a5` /
fire-hit LIVE-CLOSED `7ab099c` (KEEP). SHAs `595d591` (16x16/span) then
`2e9916c` (wavetable lerp). Pushed to `origin/main`, Hetzner
`/home/grok/GoldenEye` pull + `make -C native wasm` + `silveriris-vite`
restart. Live wasm is `2e9916c` (**272358**, Last-Modified 00:30:14 GMT).

`draw_tri_raw` classes 16x16 then 8x8 pixel-center blocks (linear w). BOX_OUT
skips empty blocks; BOX_IN fills without a coverage test; mixed 8x8 stamps
the convex span. Never drops an inside pixel. G1 greyscale unchanged.
Pitched wavetable PCM linear-interpolates adjacent samples in Q16; unity
keyBase (frac 0) stays nearest so seq.pcm KEEP. Pack vibType/tremType still
unused in Facility. HUD stays `seq 1e` (320-wide). Not ASP HLE (no RSP mixer).
Native firehit miss 24.38 / hit 24.01 `guard_fire_ms=1.30` after ~80ms dead=1
hits=2 kills=1. Pack smoke seq_on=1 inst_on=1 env_on=1 pan_on=1 det_on=1
bend_on=0. play_spawn frame_ms=57.27; bench spawn 56.76 long_walk 46.47.
Footsteps stay mixer thud `3c97070`.

**1 — live walk / fire.** Hard-refresh Chrome `?ff_netplay=0`, pack
`fff814d2…`. Spawn HUD `x=-98.5 z=-2358.4 y=29.1 θ270` **seq 1e** wasm
seq=1 wav=1 env=1 pan=1 det=1 bend=0. Idle rAF **152 / 3529ms ≈ 43.1 fps**;
walk KeyW ≈**36.2 fps** (127 / 3508ms) to x=−573.3 last_sfx=15 spat=1. Tab did
not freeze. Same class as Mac 2005 (idle 42.9 / walk 36.0) — 16x16 span + lerp
is not a freeze. `.local/mihok-chrome-playtest-20260902-2030.md`.
Fire-at-first-enemy after this wasm: stall θ270 hits 1 kills 1 mag 7→6,
post-hit rAF **166 / 3514ms ≈ 47.2 fps**, no freeze. Guard 0 dead=1.
`.local/mihok-chrome-playtest-20260902-2031.md`.

**2 — hitch / KEEP.** Stall xz=−98.5,−2358.4. Handles/Chead/void/fire-hit
untouched (spawn_fill handle_xrange 49/52, neck_gap=7, mihok_void
left_clear=0). Facility CDP :9226 still unreachable. Native g1 greyscale
`643fcb7f83cabd7f505df4163130af8cebfb76b7cd524ec5881e2d81972cd477`.
player/gun/audio-test green (seq.pcm KEEP; wavetable lerp distinct=1).

**Remaining holes**

- P0-1 fire-hit LIVE-CLOSED on `7ab099c` (re-proved on `2e9916c` wasm:
  stall θ270 hit 1 / kill 1, post-hit ≈47 fps). Do not reopen as walking
  freeze. Evidence: Chris 14:21; fix SHA `7ab099c`; latest live re-prove
  2031.
- P0-2 live spawn-idle / walk rAF still required on the Facility box.
  Mac live idle ~43 / walk ~36 does not close `ff37828`. Facility-box CDP
  :9226 was not reachable this pass. Overnight box idle 24.5–28.8
  (0446/0612/1147) still the last Facility samples — cite, do not claim
  CLOSED.
- P0-2 doors: Mihok 1032/1215 live-closed spawn/θ263 — brown 686-688
  ribs + two 685 highlight handle bars. Live 2030/2031 still shows both.
  Keep closed unless a later pass regresses ribs or bars.
- P0-3 LIVE-CLOSED on `8cc1ce1` (θ263 neck_gap=7). KEEP stitched-neck
  DL; no model-Y fudge.
- Missing-neighbor LIVE-CLOSED on `8cc1ce1` at −161,−2382 θ290. Do not
  walk stacked r6.
- Full ASP HLE still out (RSP mixer). Music is pack compact MIDI plus
  instruments.tbl PCM plus ALEnvelope plus ALSound/CC10 pan plus
  ALKeyMap.detune plus MIDI pitch bend plus live CC7/CC10 `085c5a5` plus
  wavetable lerp `2e9916c`, not ASP. World SFX mixer pan + distance vol;
  door open/close spat `2bd608e`. Footsteps remain mixer placeholder
  `3c97070` (L/R pan). Not RSP spatial. Pack vibrato/tremolo unused in
  Facility seq; CC91 FX mix is ASP.
- Combat AI / matching engine later. Campaign out of v1.

