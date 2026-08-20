import assert from "node:assert/strict";
import { test } from "node:test";
import { CROCKFORD, codeOk, randomRoomCode } from "./codes.js";

test("codes are 5 Crockford chars", () => {
  const c = randomRoomCode();
  assert.equal(c.length, 5);
  assert.ok(codeOk(c));
  for (const ch of c)
    assert.ok(CROCKFORD.includes(ch));
  assert.equal(codeOk("abc"), false);
});
