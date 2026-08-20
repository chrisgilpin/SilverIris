Golden vectors for the matching GoldenEye LCG (`src/random.s` / `chrObjRandom.s`).

- `seed0.vec` — 256 `u32` hex outputs after `randomSetSeed(0)` (seed word becomes 1)
- `seed1.vec` — same after `randomSetSeed(1)` (seed word becomes 2)

Both streams use the same LCG; they must match these files when given the same seed.
Do not simplify the generator. Public CI: `make -C native rng-test`.
