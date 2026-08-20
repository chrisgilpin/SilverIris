import "fake-indexeddb/auto";
import { describe, expect, it } from "vitest";
import { kvGet, kvSet, packGet, packPut, resetIdbCache } from "./idb.ts";

describe("idb kv", () => {
  it("round-trips lastRegion and nothing else is required in PR-02", async () => {
    resetIdbCache();
    await kvSet("lastRegion", "U");
    await expect(kvGet<string>("lastRegion")).resolves.toBe("U");
  });

  it("stores a pack blob by packHash", async () => {
    resetIdbCache();
    const blob = new Uint8Array([1, 2, 3]).buffer;
    await packPut("abc", { region: "U", romSha1: "00", created: 1, blob });
    const got = await packGet("abc");
    expect(got?.region).toBe("U");
    expect(new Uint8Array(got!.blob)).toEqual(new Uint8Array([1, 2, 3]));
  });
});
