import { readFileSync, writeFileSync, mkdirSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { spawnSync } from "node:child_process";
import { describe, expect, it } from "vitest";
import { sha256Software } from "../../extractor/src/sha256.ts";
import {
  buildPack,
  parsePack,
  C0PACK_MAGIC,
} from "./pack.ts";

const here = dirname(fileURLToPath(import.meta.url));
const repo = join(here, "..", "..", "..");

describe("c0pack", () => {
  const files = [
    { path: "assets/hello.bin", bytes: new TextEncoder().encode("hello") },
    { path: "assets/test.bin", bytes: new TextEncoder().encode("abc") },
  ];

  it("is stable and round-trips", async () => {
    const a = await buildPack(files, "U", 0);
    const b = await buildPack(files, "U", 0);
    expect(a.packHash).toBe(b.packHash);
    expect([...a.bytes]).toEqual([...b.bytes]);
    expect(String.fromCharCode(...a.bytes.subarray(0, 4))).toBe(C0PACK_MAGIC);
    const parsed = parsePack(a.bytes);
    expect(parsed.packHash).toBe(a.packHash);
    expect(parsed.region).toBe("U");
    expect(parsed.files).toHaveLength(2);
    expect(parsed.files[0].path).toBe("assets/hello.bin");
    expect(new TextDecoder().decode(parsed.files[1].bytes)).toBe("abc");
    expect(parsed.files[1].sha256).toBe(sha256Software(files[1].bytes));
  });

  it("rejects truncated and corrupt packs", async () => {
    const built = await buildPack(files, "U", 0);
    expect(() => parsePack(built.bytes.subarray(0, 10))).toThrow(/truncated/);
    const badMagic = built.bytes.slice();
    badMagic[0] = 0x41;
    expect(() => parsePack(badMagic)).toThrow(/magic/);
    const badTrail = built.bytes.slice();
    badTrail[badTrail.length - 1] ^= 0xff;
    expect(() => parsePack(badTrail)).toThrow(/packHash/);
  });

  it("rejects illegal paths", async () => {
    await expect(
      buildPack([{ path: "foo/bar.bin", bytes: new Uint8Array([1]) }], "U"),
    ).rejects.toThrow(/assets\/ or bin\//);
  });

  it("matches the C builder", async () => {
    const make = spawnSync("make", ["-C", join(repo, "tools/pack"), "test_c0pack"], {
      encoding: "utf8",
    });
    if (make.status !== 0) {
      throw new Error(`make test_c0pack failed: ${make.stderr}\n${make.stdout}`);
    }
    const bin = join(repo, "tools/pack/test_c0pack");
    const outFile = join(repo, "testdata/pack/synthetic.c0pack");
    mkdirSync(dirname(outFile), { recursive: true });
    const run = spawnSync(bin, [outFile], { encoding: "utf8" });
    expect(run.status).toBe(0);
    const packHashLine = run.stdout.split("\n").find((l) => l.startsWith("PACKHASH="));
    const abcLine = run.stdout.split("\n").find((l) => l.startsWith("SHA256_ABC="));
    expect(abcLine).toBe(
      "SHA256_ABC=ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
    );
    const ts = await buildPack(files, "U", 0);
    expect(packHashLine).toBe(`PACKHASH=${ts.packHash}`);
    const fromC = parsePack(new Uint8Array(readFileSync(outFile)));
    expect(fromC.packHash).toBe(ts.packHash);
    writeFileSync(outFile, ts.bytes); // keep fixture identical to TS bytes
    expect([...fromC.files.map((f) => f.path)]).toEqual([
      "assets/hello.bin",
      "assets/test.bin",
    ]);
  });
});
