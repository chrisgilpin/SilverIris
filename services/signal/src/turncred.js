import { createHmac, timingSafeEqual } from "node:crypto";

/** TURN REST long-term creds (draft-uberti-behave-turn-rest). Room-scoped, time-limited. */
export const TURN_TTL_SEC = 2 * 60 * 60;
export const DEFAULT_TURN_HOST = "007.goodhouseinc.com";

export function turnUrls(host = DEFAULT_TURN_HOST) {
  const h = host || DEFAULT_TURN_HOST;
  return [
    `turn:${h}:3478?transport=udp`,
    `turn:${h}:3478?transport=tcp`,
    `turns:${h}:5349?transport=tcp`,
  ];
}

/**
 * @param {string} secret
 * @param {string} room
 * @param {number} [nowMs]
 */
export function mintTurnCred(secret, room, nowMs = Date.now()) {
  if (typeof secret !== "string" || secret.length < 8)
    return null;
  if (typeof room !== "string" || room.length < 1)
    return null;
  const expiry = Math.floor(nowMs / 1000) + TURN_TTL_SEC;
  const username = `${expiry}:${room}`;
  const credential = createHmac("sha1", secret).update(username).digest("base64");
  return { username, credential, ttl: TURN_TTL_SEC, expiry };
}

/**
 * @param {string} secret
 * @param {string} room
 * @param {string} [host]
 * @param {number} [nowMs]
 */
export function turnIce(secret, room, host = DEFAULT_TURN_HOST, nowMs = Date.now()) {
  const cred = mintTurnCred(secret, room, nowMs);
  if (!cred)
    return null;
  return {
    urls: turnUrls(host),
    username: cred.username,
    credential: cred.credential,
    ttl: cred.ttl,
  };
}

/** Test helper: username must be expiry:room with expiry in the future. */
export function credUsernameOk(username, room, nowMs = Date.now()) {
  if (typeof username !== "string" || !username.includes(":"))
    return false;
  const i = username.indexOf(":");
  const expiry = Number(username.slice(0, i));
  const rest = username.slice(i + 1);
  if (!Number.isFinite(expiry) || expiry * 1000 <= nowMs)
    return false;
  return rest === room;
}

export function hmacMatches(secret, username, credential) {
  if (!secret || !username || !credential)
    return false;
  const expect = createHmac("sha1", secret).update(username).digest("base64");
  const a = Buffer.from(expect);
  const b = Buffer.from(String(credential));
  return a.length === b.length && timingSafeEqual(a, b);
}
