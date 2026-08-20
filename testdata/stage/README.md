Synthetic Facility bg/stan headers for PR-11a CI. Big-endian N64
segment words (`0x0F000000` + offset) only — not ROM-derived geometry.
Do not replace with a dump from a retail pack.

`tools/det/test_stage` builds two in-memory packs:

- `G1DL` magic (`0x4731444C`) plus a Fast3D room GDL. G1 rasters it;
  greyscale FB must match `testdata/g1/synthetic.fb.sha256`.
- Rare-shaped header (`word0 == 0`) plus a 1172-compressed C0 GDL
  (`G_SETTEX` + `G_TRI4`). Runtime `bgDecompress` inflates it; G1 must
  match the same greyscale hash. A junk Rare header without `0x11 0x72`
  still walks rooms and refuses to draw.
