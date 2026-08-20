# SilverIris extractor

In-tab ROM extract. Offset maps come from the decomp submodule (`filelist.u.csv`,
`imagelist.u.csv`, `assets/images.def`). Inflate of Rare 1172 streams is raw
deflate after a 2-byte header — the same contract as `tools/extractor/puff.c`.

v1 runs in a Worker, single-thread, **without** asp/gsp/rsp ucode
(`flags.bit1 = 0`). Pass 3 is implemented but off.

A C/Emscripten `extractor-wasm` target can replace `fflate` later; public tests
are the 1172 vector and a 3-line CSV, not a retail ROM.
