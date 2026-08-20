# Legal posture

This is a statement of how SilverIris is built. It is **not legal advice**,
not a fair-use conclusion, and not a clearance path.

## What we ship

- Original port, shell, extractor, signaling, and tooling we write
  (`LICENSE`: All Rights Reserved).
- A git submodule of the matching GoldenEye 007 decompilation
  (`third_party/goldeneye_src`). That C is **unlicensed-to-us**. See `NOTICE`.

We never ship:

- A Nintendo 64 ROM
- Extracted textures, models, audio, levels, or text
- Links to ROM downloads
- Official trademarks as the product name

## ROM gate

Players must supply a legally obtained NTSC-U dump. Extraction and caching
happen in the browser. ROM bytes and asset packs are not uploaded to our
servers.

Native `silveriris_bringup` may fopen a developer’s local dump
(`PORT_BRINGUP_ROM_DMA`, K18) so title/stage DMA can be brought up before the
pack reader. Product `silveriris` and `game.wasm` are compiled without that
flag and load a `.c0pack` (`--pack` / `port_init`). `tools/pack/extract --rom`
is a developer tool. Neither native binary is a public download.

Requiring a user ROM **does not** make redistributing a compiled GoldenEye
engine automatically lawful. Hosting `game.wasm` on a public URL **is**
distributing a compiled derivative of copyrighted C, with or without assets.

This follows the community convention used by sm64-port and Ship of Harkinian.
That convention is not a safe harbor.

## Rights holders (facts, not permission)

GoldenEye 007 involves, among others, Nintendo; Rare / Microsoft; Danjaq /
Eon; and MGM. Ship of Harkinian’s Zelda posture does not transfer.

News around the decomp’s 100% milestone also claimed an official port is in
progress. That is a crowding / takedown risk, not a reason to brand this as
the official game.

## Takedown

Unpublish the public vhost, delete `game.wasm` from the server, stop
signaling and coturn. Client-side packs stay on user machines. The source
repo should still contain no ROMs or assets.

## Decomp credit

KholdFuzion and the GoldenEye 007 decompilation contributors authored the
matching C. We do not claim affiliation, endorsement, or a license grant.
A courtesy ping before a public repo is polite. It is not permission.
