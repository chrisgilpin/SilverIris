# Decomp overrides

The only place we patch matching C.

Each override is a small `#ifdef PORT` or a one-function replacement with a
comment citing the original symbol. Compile decomp files by path from
`third_party/goldeneye_src`. Do not copy the game tree into `src/game`.

`lv_clock.c` is the timer preamble of `lvlManageMpGame` (NTSC `0x7F0BEB88`)
until full `lv.c` compiles. `port_sim_tick` calls it after
`updateFrameCounters(3)` and does not write `g_ClockTimer` itself.

`src/port/player/move.c` is the analog walk/look slice of `bondviewProcessInput`
+ `MoveBond` until `bondview2.c` compiles.

`src/port/player/gun.c` is the PP7 fire/reload slice of `gunfire.c` until that
file compiles. `src/port/chr/patrol.c` is `set_actor_on_path` /
`chrlvTickPatrol` until `chr.c` / `chraction.c` compile.
`src/port/mp/score.c` is `reset_mp_options_for_scenario` +
`increment_num_kills_display_text_in_MP` until `front.c` / `gunfire.c` compile.
`src/port/player/move.c` seats + NTSC viewports are
`bondviewGetCurrentPlayerViewport*` until `bondview2.c` compiles.
`src/port/det/checksum.c` is CRC32C (never `fileGenerateCRC`).
`src/port/det/tape.c` is TAPE1 record/replay (pads + both RNG seeds).
