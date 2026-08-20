export type RomEndian = "z64" | "n64" | "v64";

/** GE US z64 starts 80 37 12 40. */
export function detectEndian(u8: Uint8Array): RomEndian {
  if (u8.length < 4) {
    throw new Error("unrecognised N64 header");
  }
  const b0 = u8[0],
    b1 = u8[1];
  if (b0 === 0x80 && b1 === 0x37) return "z64";
  if (b0 === 0x40 && b1 === 0x12) return "n64";
  if (b0 === 0x37 && b1 === 0x80) return "v64";
  throw new Error("unrecognised N64 header");
}

export function toZ64(u8: Uint8Array): Uint8Array {
  const kind = detectEndian(u8);
  if (kind === "z64") return u8;
  if (kind === "v64" && u8.length % 2 !== 0) {
    throw new Error("unrecognised N64 header");
  }
  if (kind === "n64" && u8.length % 4 !== 0) {
    throw new Error("unrecognised N64 header");
  }
  const out = new Uint8Array(u8.byteLength);
  if (kind === "v64") {
    for (let i = 0; i < u8.length; i += 2) {
      out[i] = u8[i + 1];
      out[i + 1] = u8[i];
    }
  } else {
    for (let i = 0; i < u8.length; i += 4) {
      out[i] = u8[i + 3];
      out[i + 1] = u8[i + 2];
      out[i + 2] = u8[i + 1];
      out[i + 3] = u8[i];
    }
  }
  return out;
}
