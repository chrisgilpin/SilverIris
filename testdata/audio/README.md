Audio mixer fixtures. CI stores SHA-256 of 4096 interleaved s16 stereo
frames at 22050 Hz from the port mixer (silence, placeholder title loop,
placeholder gun one-shot).

These tones are generated in `src/port/audio/mixer.c` when no pack bank
is installed. They are not cartridge banks and must not be replaced with
ROM-derived PCM. Runtime Facility play decodes pack `sfx.ctl`/`sfx.tbl`
VADPCM into host PCM (gun / dry / door / fall / hit / rico / ammo / armour /
reload / yelp / hurt) without shipping samples.
