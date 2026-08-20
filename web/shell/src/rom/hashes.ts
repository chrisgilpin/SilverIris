/** Matching retail SHA-1s (z64 canonical). v1 product accepts NTSC-U only. */

export const SHA1_NTSC_U = "abe01e4aeb033b6c0836819f549c791b26cfde83";
export const SHA1_NTSC_J = "2a5dade32f7fad6c73c659d2026994632c1b3174";
export const SHA1_PAL_E = "167c3c433dec1f1eb921736f7d53fac8cb45ee31";

export type RomRegion = "U" | "J" | "E";

export function classifySha1(hex: string): RomRegion | "unknown" {
  const h = hex.toLowerCase();
  if (h === SHA1_NTSC_U) return "U";
  if (h === SHA1_NTSC_J) return "J";
  if (h === SHA1_PAL_E) return "E";
  return "unknown";
}
