import { describe, expect, it } from "vitest";
import { SHA1_NTSC_U, SHA1_NTSC_J, SHA1_PAL_E, classifySha1 } from "./hashes.ts";
import {
  MIN_ROM_SIZE,
  MSG_TOO_SMALL,
  MSG_UNKNOWN,
  MSG_US_REQUIRED,
  verifyRom,
} from "./verify.ts";

function fakeRom(endian: "z64" | "n64" | "v64"): Uint8Array {
  const u8 = new Uint8Array(MIN_ROM_SIZE);
  if (endian === "z64") {
    u8[0] = 0x80;
    u8[1] = 0x37;
    u8[2] = 0x12;
    u8[3] = 0x40;
  } else if (endian === "n64") {
    u8[0] = 0x40;
    u8[1] = 0x12;
    u8[2] = 0x37;
    u8[3] = 0x80;
  } else {
    u8[0] = 0x37;
    u8[1] = 0x80;
    u8[2] = 0x40;
    u8[3] = 0x12;
  }
  for (let i = 4; i < 256; i++) u8[i] = i & 0xff;
  return u8;
}

describe("classifySha1", () => {
  it("maps the three matching dumps and nothing else", () => {
    expect(classifySha1(SHA1_NTSC_U)).toBe("U");
    expect(classifySha1(SHA1_NTSC_J)).toBe("J");
    expect(classifySha1(SHA1_PAL_E)).toBe("E");
    expect(classifySha1("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")).toBe(
      "unknown",
    );
  });
});

describe("verifyRom", () => {
  it("rejects files under 12 MB", async () => {
    const small = new Uint8Array(4096);
    small[0] = 0x80;
    small[1] = 0x37;
    small[2] = 0x12;
    small[3] = 0x40;
    const r = await verifyRom(small);
    expect(r.ok).toBe(false);
    if (!r.ok) {
      expect(r.reason).toBe("too_small");
      expect(r.message).toBe(MSG_TOO_SMALL);
    }
  });

  it("rejects a synthetic 12 MB z64 that is not a matching dump", async () => {
    const r = await verifyRom(fakeRom("z64"));
    expect(r.ok).toBe(false);
    if (!r.ok) {
      expect(r.reason).toBe("unknown");
      expect(r.message).toBe(MSG_UNKNOWN);
      expect(r.romSha1).toMatch(/^[0-9a-f]{40}$/);
    }
  });

  it("accepts NTSC-U when the hash function returns the US SHA-1", async () => {
    const r = await verifyRom(fakeRom("n64"), async () => SHA1_NTSC_U);
    expect(r.ok).toBe(true);
    if (r.ok) {
      expect(r.region).toBe("U");
      expect(r.endian).toBe("n64");
      expect(r.z64[0]).toBe(0x80);
      expect(r.romSha1).toBe(SHA1_NTSC_U);
    }
  });

  it("says US dump required for JP and EU matching hashes", async () => {
    const jp = await verifyRom(fakeRom("z64"), async () => SHA1_NTSC_J);
    const eu = await verifyRom(fakeRom("v64"), async () => SHA1_PAL_E);
    expect(jp.ok).toBe(false);
    expect(eu.ok).toBe(false);
    if (!jp.ok && !eu.ok) {
      expect(jp.reason).toBe("jp");
      expect(eu.reason).toBe("eu");
      expect(jp.message).toBe(MSG_US_REQUIRED);
      expect(eu.message).toBe(MSG_US_REQUIRED);
    }
  });
});
