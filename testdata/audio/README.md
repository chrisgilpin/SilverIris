Audio mixer fixtures. CI stores SHA-256 of 4096 interleaved s16 stereo
frames at 22050 Hz from the port mixer (silence, placeholder title loop,
placeholder gun one-shot).

These tones are generated in `src/port/audio/mixer.c` when no pack bank
is installed. They are not cartridge banks and must not be replaced with
ROM-derived PCM. Runtime Facility play decodes pack `sfx.ctl`/`sfx.tbl`
VADPCM into host PCM (gun / dry / door / fall / hit / rico / ammo / armour /
reload / yelp / hurt) without shipping samples. Pack GET_HIT_MALE0–24 cycle
on yelp (Rare counter, not game RNG); BODY_FALL_C1–E3 + BODY_ROLLOVER
cycle on fall (Rare thud_index / body_hit_SFX, wrap at 11); placeholders
stay one tone. Walk steps are a mixer placeholder (GE has no footstep
SFX ID) on a fifth voice so they do not cut gun/door; L/R feet pan
48/80. Compact MIDI seq is a synthetic one-note fixture
(`seq.pcm.sha256`); runtime Facility play walks pack `Mfacility.bin` on
seq voices. Pack `instruments.ctl` / `instruments.tbl` VADPCM is decoded
to host PCM and pitched by MIDI key vs keyBase plus ALKeyMap.detune
cents (loops plus ALEnvelope attack/decay/release). MIDI pitch bend
uses ALInstrument.bendRange (center 8192 is bit-identical). Pitched
wavetable samples linear-interpolate adjacent PCM (unity keyBase is
the nearest sample). Without that bank the voices stay triangles.
World SFX take a mixer pan and distance vol from listener xz. Not ASP
HLE (no RSP mixer).
