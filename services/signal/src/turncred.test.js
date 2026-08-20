import assert from "node:assert/strict";
import { test } from "node:test";
import { credUsernameOk, hmacMatches, mintTurnCred, turnIce, turnUrls, TURN_TTL_SEC } from "./turncred.js";

const SECRET = "unit-test-turn-secret-not-for-prod";
const ROOM = "ABCDE";

test("mint is room-scoped long-term REST (expiry:room + HMAC-SHA1)", () => {
  const now = 1_700_000_000_000;
  const c = mintTurnCred(SECRET, ROOM, now);
  assert.ok(c);
  assert.equal(c.ttl, TURN_TTL_SEC);
  assert.ok(credUsernameOk(c.username, ROOM, now));
  assert.equal(c.username, `${Math.floor(now / 1000) + TURN_TTL_SEC}:${ROOM}`);
  assert.ok(hmacMatches(SECRET, c.username, c.credential));
  assert.equal(hmacMatches("wrong-secret-value", c.username, c.credential), false);
});

test("refuses missing secret or room (no open-TURN mint)", () => {
  assert.equal(mintTurnCred("", ROOM), null);
  assert.equal(mintTurnCred("short", ROOM), null);
  assert.equal(mintTurnCred(SECRET, ""), null);
  assert.equal(turnIce("", ROOM), null);
});

test("different rooms get different usernames and HMACs", () => {
  const a = mintTurnCred(SECRET, "AAAAA", 1_700_000_000_000);
  const b = mintTurnCred(SECRET, "BBBBB", 1_700_000_000_000);
  assert.notEqual(a.username, b.username);
  assert.notEqual(a.credential, b.credential);
  assert.ok(a.username.endsWith(":AAAAA"));
  assert.ok(b.username.endsWith(":BBBBB"));
});

test("turnIce lists udp/tcp 3478 and turns 5349", () => {
  const ice = turnIce(SECRET, ROOM, "007.goodhouseinc.com", 1_700_000_000_000);
  assert.deepEqual(ice.urls, turnUrls("007.goodhouseinc.com"));
  assert.ok(ice.urls.some((u) => u.startsWith("turn:") && u.includes("3478") && u.includes("udp")));
  assert.ok(ice.urls.some((u) => u.startsWith("turn:") && u.includes("tcp")));
  assert.ok(ice.urls.some((u) => u.startsWith("turns:") && u.includes("5349")));
  assert.equal(typeof ice.username, "string");
  assert.equal(typeof ice.credential, "string");
});
