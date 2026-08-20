import { detectEndian, toZ64, type RomEndian } from "./byteswap.ts";
import { classifySha1 } from "./hashes.ts";
import { sha1Hex } from "./sha1.ts";

/** Decomp extractor MIN_ROM_SIZE: 12 MB. */
export const MIN_ROM_SIZE = 12 * 1024 * 1024;

export const MSG_UNKNOWN =
  "Not a matching dump. We do not accept hacks or reproductions.";
export const MSG_US_REQUIRED = "US dump required.";
export const MSG_TOO_SMALL = "File is too small to be a matching dump.";
export const MSG_BAD_HEADER = "Unrecognised N64 ROM header.";

export type VerifyOk = {
  ok: true;
  region: "U";
  romSha1: string;
  endian: RomEndian;
  z64: Uint8Array;
};

export type VerifyFail = {
  ok: false;
  reason: "too_small" | "bad_header" | "jp" | "eu" | "unknown";
  message: string;
  romSha1?: string;
};

export type VerifyResult = VerifyOk | VerifyFail;

export type Sha1Fn = (data: Uint8Array) => Promise<string>;

export async function verifyRom(
  u8: Uint8Array,
  sha1: Sha1Fn = sha1Hex,
): Promise<VerifyResult> {
  if (u8.byteLength < MIN_ROM_SIZE) {
    return { ok: false, reason: "too_small", message: MSG_TOO_SMALL };
  }

  let endian: RomEndian;
  let z64: Uint8Array;
  try {
    endian = detectEndian(u8);
    z64 = toZ64(u8);
  } catch {
    return { ok: false, reason: "bad_header", message: MSG_BAD_HEADER };
  }

  const romSha1 = await sha1(z64);
  const region = classifySha1(romSha1);

  if (region === "U") {
    return { ok: true, region: "U", romSha1, endian, z64 };
  }
  if (region === "J") {
    return { ok: false, reason: "jp", message: MSG_US_REQUIRED, romSha1 };
  }
  if (region === "E") {
    return { ok: false, reason: "eu", message: MSG_US_REQUIRED, romSha1 };
  }
  return { ok: false, reason: "unknown", message: MSG_UNKNOWN, romSha1 };
}
