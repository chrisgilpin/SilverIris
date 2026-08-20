Synthetic Facility bg/stan headers for PR-11a CI. Big-endian N64
segment words (`0x0F000000` + offset) only — not ROM-derived geometry.
Do not replace with a dump from a retail pack.

`tools/det/test_stage` builds two in-memory packs:

- `G1DL` magic (`0x4731444C`) plus a Fast3D room GDL. G1 rasters it;
  greyscale FB must match `testdata/g1/synthetic.fb.sha256`.
- Rare-shaped header (`word0 == 0`) plus a 1172-compressed C0 GDL
  (`G_SETTEX` + `G_TRI4`). Runtime `bgDecompress` inflates it; G1 must
  match the same greyscale hash when no SITX bank is bound. A junk Rare header without `0x11 0x72`
  still walks rooms and refuses to draw.

`port_api_draw` after a drawable load reports `PORT_DRAW_STAGE`
and the same greyscale hash. A junk Rare header reports `PORT_DRAW_FALLBACK`
(the live presenter keeps the PORT mesh in that case).

A retail-shaped C0 pack (no `G_MTX` in the GDL) 1172-compresses a 3-vertex
table at `pPointTableBin` and a `G_SETTEX` + unresolved `G_DL` + unknown
opcode + `G_VTX` (segment 14) + `G_TRI4` GDL. Identity camera stays black;
player look-at paints. `port_api_fb_nonzero` counts painted pixels.

`checker.sitx` is an 8×8 I8 checker (0x20/0xE0) in the PORT SITX header
(magic `SITX`, TEXFORMAT_I8). It is not a Rare image bank and is not
ROM-derived. `test_stage` loads it via pack path `image7.bin` / `COPYICON.bin`
to prove SETTEX → TMEM → TRI4 sampling.

`checker.rare.bin` is the same 8×8 I8 checker in the Rare *non-zlib*
bank container (`uzllllll` + format4/w8/h8/method4 uncompressed).
`checker.zbank.bin` is an 8×8 CI4 checker (red/green 5551) in the Rare
*zlib* bank container (`z=1`, palette, 1172-wrapped indices). Neither
file is ROM-derived. Retail Facility banks are the same *container*,
but their payloads are unproven here (no retail texels in git).

