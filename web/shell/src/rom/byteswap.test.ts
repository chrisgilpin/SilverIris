import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { detectEndian, toZ64 } from "./byteswap.ts";

const here = dirname(fileURLToPath(import.meta.url));
const headerPath = join(here, "..", "..", "testdata", "fake-header.bin");

function header(): Uint8Array {
  return new Uint8Array(readFileSync(headerPath));
}

function swap16(src: Uint8Array): Uint8Array {
  const out = new Uint8Array(src.length);
  for (let i = 0; i < src.length; i += 2) {
    out[i] = src[i + 1];
    out[i + 1] = src[i];
  }
  return out;
}

function swap32(src: Uint8Array): Uint8Array {
  const out = new Uint8Array(src.length);
  for (let i = 0; i < src.length; i += 4) {
    out[i] = src[i + 3];
    out[i + 1] = src[i + 2];
    out[i + 2] = src[i + 1];
    out[i + 3] = src[i];
  }
  return out;
}

describe("endian", () => {
  it("loads a 4 KiB fake z64 header, not a dump", () => {
    const z = header();
    expect(z.byteLength).toBe(4096);
    expect(z[0]).toBe(0x80);
    expect(z[1]).toBe(0x37);
    expect(z[2]).toBe(0x12);
    expect(z[3]).toBe(0x40);
  });

  it("detects z64 / n64 / v64 from the standard magic", () => {
    const z = header();
    expect(detectEndian(z)).toBe("z64");
    expect(detectEndian(swap32(z))).toBe("n64");
    expect(detectEndian(swap16(z))).toBe("v64");
  });

  it("toZ64 is identity on z64 and inverts n64 and v64", () => {
    const z = header();
    expect(toZ64(z)).toBe(z);
    expect([...toZ64(swap32(z)).subarray(0, 4)]).toEqual([0x80, 0x37, 0x12, 0x40]);
    expect([...toZ64(swap16(z)).subarray(0, 4)]).toEqual([0x80, 0x37, 0x12, 0x40]);
  });

  it("rejects unrecognised headers", () => {
    expect(() => detectEndian(new Uint8Array([0, 0, 0, 0]))).toThrow(
      /unrecognised/,
    );
  });
});
