import { describe, expect, it } from "vitest";
import { packHashBytes } from "./bridge.ts";

describe("packHashBytes", () => {
  it("decodes a 32-byte sha256 hex", () => {
    const hex = "923e540c0e84a0ef240b9d440fc6d0e6ac719c5b9c0eb9871fe5b145aaa42fc3";
    const b = packHashBytes(hex);
    expect(b.byteLength).toBe(32);
    expect(b[0]).toBe(0x92);
    expect(b[31]).toBe(0xc3);
  });
});
