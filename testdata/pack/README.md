Synthetic `c0pack` fixtures only. No ROM-derived payloads.

`synthetic.c0pack` is two tiny files (`assets/hello.bin`, `assets/test.bin`)
built by `tools/pack/test_c0pack` / the TypeScript builder. Public CI uses it
to prove parse/build agreement. Do not replace it with a pack from a retail dump.

`extract_synth.csv` is a 3-line filelist for pack-DMA tests (uncompressed
payloads). `tools/pack/extract --rom … -o ge.u.c0pack` is developer-only.
