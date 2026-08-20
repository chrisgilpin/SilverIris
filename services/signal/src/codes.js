import { randomBytes } from "node:crypto";

/** Crockford Base32, 5 chars, 25 bits. */
export const CROCKFORD = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

export function randomRoomCode() {
  const buf = randomBytes(4);
  const bits = buf.readUInt32BE(0) >>> 7; /* 25 bits */
  let code = "";
  for (let i = 0; i < 5; i++)
    code += CROCKFORD[(bits >>> (5 * (4 - i))) & 31];
  return code;
}

export function codeOk(code) {
  if (typeof code !== "string" || code.length !== 5)
    return false;
  const up = code.toUpperCase();
  for (const ch of up) {
    if (!CROCKFORD.includes(ch))
      return false;
  }
  return true;
}
