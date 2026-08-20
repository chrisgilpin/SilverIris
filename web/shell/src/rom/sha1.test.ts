import { describe, expect, it } from "vitest";
import { sha1Hex, sha1Software } from "./sha1.ts";

describe("sha1", () => {
  it("matches FIPS-180 vectors", async () => {
    const abc = new TextEncoder().encode("abc");
    const empty = new Uint8Array();
    expect(sha1Software(abc)).toBe("a9993e364706816aba3e25717850c26c9cd0d89d");
    expect(sha1Software(empty)).toBe("da39a3ee5e6b4b0d3255bfef95601890afd80709");
    expect(await sha1Hex(abc)).toBe("a9993e364706816aba3e25717850c26c9cd0d89d");
  });

  it("hashes 12 MiB without using byte-at-a-time copies", async () => {
    const u8 = new Uint8Array(12 * 1024 * 1024);
    u8[0] = 0x80;
    u8[1] = 0x37;
    const t0 = Date.now();
    const hex = await sha1Hex(u8);
    expect(hex).toMatch(/^[0-9a-f]{40}$/);
    expect(Date.now() - t0).toBeLessThan(5000);
  });
});
