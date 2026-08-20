Synthetic Facility bg/stan headers for PR-11a CI. Big-endian N64
segment words (`0x0F000000` + offset) only — not ROM-derived geometry.
Do not replace with a dump from a retail pack.

`tools/det/test_stage` builds two in-memory packs:

- `G1DL` magic (`0x4731444C`) plus a Fast3D room GDL. G1 rasters it;
  greyscale FB must match `testdata/g1/synthetic.fb.sha256`.
- Rare-shaped header (`word0 == 0`): room table walks, GDL is not
  interpreted (retail payloads are compressed C0/4Tri).
