import { describe, expect, it } from "vitest";
import { sha256Hex, sha256Software } from "./sha256.ts";

describe("sha256", () => {
  it("matches FIPS-180 vectors", async () => {
    const abc = new TextEncoder().encode("abc");
    const empty = new Uint8Array();
    expect(sha256Software(empty)).toBe(
      "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
    );
    expect(sha256Software(abc)).toBe(
      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
    );
    expect(await sha256Hex(abc)).toBe(
      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
    );
  });
});
