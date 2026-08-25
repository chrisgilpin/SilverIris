Input tapes only. No ROMs, no extracted assets, no framebuffer screenshots
that contain game art. Public CI may hold synthetic controller streams.
Full ramrom-derived tapes stay private.

`synthetic.tape` is a TAPE1 (magic `TAPE`, version 1) written by
`make -C native tape-test`: 40 ticks, 1 seat, rngSeed 1, walk then one Z.
Replay: `make -C native replay-synth`. Pack hash is zeros.

`2p-corridor.tape` is written by `make -C native 2p-corridor-test`: 48
ticks, 2 seats, synthetic corridor + door, P0 walks, Z-unlatch, one PvP
shot. Replay is inside that test (world setup is not in the tape).
