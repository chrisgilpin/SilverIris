# Native ge_sim bring-up

Host `gcc` compiles a **subset** of the matching C with PORT shims.
Native binaries are developer/CI only — not a public desktop player.

```bash
make -C native
./build/native/port_stub
make -C native rng-test
make -C native tick-test          # 8 ticks, no window
make -C native joy-playback       # playback func → ConsumeSamplesWrapper
make -C native rom-dma            # filelist + synthetic DMA, no dump
make -C native product-no-rom-dma # silveriris has no port_rom_open
make -C native g0-test            # osSpTaskLoad → G0T1; hash of gfx words
make -C native g1-test            # software T&L + raster; greyscale FB hash
make -C native pack-dma           # product DMA from synthetic .c0pack
make -C native port-api           # glue init(pack,hash) + G1 hash
make -C native audio-test         # silence + placeholder PCM hashes; no game RNG
make -C native stage-test         # synthetic Facility bg/stan + g_ClockTimer==3
make -C native player-test        # 10 s analog walk tape; |z| shows dt=3
make -C native gun-test           # PP7 mag spend, wall hit, CRC32C ammo
make -C native chr-test           # one guard on a looping pad path; crc_chrs
make -C native score-test         # you-vs-nothing kill_count + crc_objectives
make -C native split-test         # 2-4P seats, ENVIRONMENTDATA viewports
make -C native tape-test          # TAPE1 record/replay (synthetic, no ROM)
make -C native replay-synth       # tools/det/replay testdata/tapes/synthetic.tape
make -C native match-config       # 160-byte MatchConfig pack + pad0
make -C native lockstep-test      # 2P delay-1 inject, replay crc, missing seat
make -C native wasm               # emcc → web/shell/public/game.{js,wasm}
./build/native/silveriris         # SDL blits G1 synthetic triangle
./build/native/silveriris --pack testdata/pack/synthetic.c0pack --headless
```

## K18 ROM DMA (`silveriris_bringup` only)

Product-shaped `silveriris` is compiled **without** `PORT_BRINGUP_ROM_DMA` and
refuses `--rom`. The bring-up binary may fopen a local NTSC-U dump and DMA from
decomp `filelist.u.csv` offsets (`src/port/fs/rom_dma.c`).

```bash
make -C native silveriris_bringup
./build/native/silveriris_bringup --rom /path/to/your/ge007.u.z64 --headless
```

US SHA-1 `abe01e4aeb033b6c0836819f549c791b26cfde83` after byteswap. JP/EU and
unknown dumps are rejected. Never commit the dump. Title boot still needs later
PRs; this target only supplies the PI/filelist path.

## G0 dump (PR-06b)

`osSpTaskLoad` captures a G0T1 record (16 segment bases, last modelview /
projection as f32 bits, raw `Gfx[]`). Set `SILVERIRIS_G0_DIR` to write one
file per gfx task. Those files may contain assets — do not commit them. CI
stores `testdata/g0/synthetic.words.sha256` (hash of canonical gfx words).
`python3 tools/gbi_dump.py path.g0` prints the opcode histogram.

## G1 first picture (PR-07)

Software interpreter (`src/port/gfx/gbi_interp.c`) walks classic F3D-style
GBI: segments, modelview/projection stack, `gSPVertex` T&L to clip space,
`gSP1Triangle` / `gDPFillRectangle`. The raster (`sw_raster.c`) writes a
320×240 RGBA buffer. `osSpTaskLoad` runs G0 then G1. The SDL window blits
that buffer. CI stores `testdata/g1/synthetic.fb.sha256` (greyscale hash of
a synthetic triangle, not a screenshot). Title / Facility skybox still need
stage load.

## Pack cutover (PR-08)

Product `silveriris` takes `--pack ge.u.c0pack`. `port_init` validates the
pack and `osPiStartDma` copies from pack payloads, mapped through decomp
`filelist.u.csv` offsets (or `--filelist`). No `.reloc` sidecar.

Developer extract (fopen of a dump lives here, not in `silveriris`):

```bash
make -C tools/pack extract
./tools/pack/extract --rom /path/to/your/ge007.u.z64 \
  --filelist third_party/goldeneye_src/scripts/filelist.u.csv \
  -o ge.u.c0pack
```

Do not commit the pack from a retail dump. Public CI uses `synthetic.c0pack`.

## WASM (PR-09)

`make -C native wasm` needs emcc (`~/emsdk`). It writes `web/shell/public/game.js`
and `game.wasm` (256 MB initial heap, growth to 1 GB). `port_api_init` checks
`packHash`, then rasters the G1 synthetic picture. Shell
`web/shell/src/game/bridge.ts` loads the module after extract. Flags
`netplay`/`campaign` stay off. Title is still later.

## Audio stub (PR-10)

`src/port/audio` implements `osAi*` plus `port_audio_cb` (s16 stereo at
22050 Hz). Mixing must not call `randomGetNext` / `chrObjRandomGetNext`.
Until bank HLE (needs stage load), a deterministic placeholder title loop
and an 80 ms gun one-shot play through SDL (native, Z/Space) or an
AudioWorklet (browser, click / Z / Space). Real `music.sbk` is not in this
build. CI: `testdata/audio/*.pcm.sha256`.

`compile_trial.sh` attempts every non-libultra `.c` and writes ok/fail lists.

Current blockers for the rest of the tree:

- `inherits` / anonymous struct field name clashes (`duplicate member 'pad'`)
- remaining incomplete types in headers
- implicit declarations of game functions (need headers or stubs)
- libultra OS symbols (provided later, not compiled from matching `os/*.c`)

This is not a matching IDO build. Overlay: `build/overlay/bondtypes.h`.
