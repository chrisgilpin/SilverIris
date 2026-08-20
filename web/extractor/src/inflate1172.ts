import { inflateSync } from "fflate";

/** Rare "1172": 2-byte header then raw deflate (same as decomp puff.c). */
export const GE_1172_HEADER_LENGTH = 2;
export const MAX_INFLATE_OUT = 2 * 1024 * 1024;

export function inflate1172(src: Uint8Array): Uint8Array {
  if (src.byteLength <= GE_1172_HEADER_LENGTH) {
    throw new Error("1172 stream too short");
  }
  const raw = src.subarray(GE_1172_HEADER_LENGTH);
  try {
    const out = inflateSync(raw);
    if (out.byteLength > MAX_INFLATE_OUT) {
      throw new Error("1172 output exceeds 2 MiB");
    }
    return out;
  } catch (err) {
    const msg = err instanceof Error ? err.message : String(err);
    throw new Error(`1172 inflate failed: ${msg}`);
  }
}
