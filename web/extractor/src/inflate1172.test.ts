import { describe, expect, it } from "vitest";
import { inflate1172 } from "./inflate1172.ts";

/** zlib raw-deflate of "hello silveriris 1172 test vector", 2-byte 1172 header. */
const BLOB = Uint8Array.from(
  atob("EXLLSM3JyVcozswpSy3KLMosVjA0NDdSKEktLlEoS00uyS8CAA=="),
  (c) => c.charCodeAt(0),
);

describe("inflate1172", () => {
  it("inflates a synthetic 1172 blob", () => {
    const out = inflate1172(BLOB);
    expect(new TextDecoder().decode(out)).toBe("hello silveriris 1172 test vector");
  });

  it("rejects truncated input", () => {
    expect(() => inflate1172(new Uint8Array([0x11]))).toThrow(/too short/);
  });
});
