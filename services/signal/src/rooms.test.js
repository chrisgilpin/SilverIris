import assert from "node:assert/strict";
import { test } from "node:test";
import { createHash } from "node:crypto";
import { createStore } from "./rooms.js";

function client() {
  const inbox = [];
  return {
    id: `t${Math.random()}`,
    ip: "127.0.0.1",
    room: null,
    seat: -1,
    inbox,
    send(s) {
      inbox.push(JSON.parse(s));
    },
  };
}

const HASH = "a".repeat(64);
const BUILD = "deadbeef";

test("create then join same pack", () => {
  const st = createStore();
  const host = client();
  const guest = client();
  st.onMessage(host, JSON.stringify({ v: 1, t: "create", nick: "Host", packHash: HASH, region: "U", buildId: BUILD }));
  const created = host.inbox.find((m) => m.t === "created");
  assert.ok(created.code);
  st.onMessage(guest, JSON.stringify({ v: 1, t: "join", code: created.code, nick: "Guest", packHash: HASH, region: "U", buildId: BUILD }));
  assert.equal(guest.inbox.find((m) => m.t === "joined")?.seat, 1);
  assert.equal(st.health().rooms, 1);
});

test("reject JP region and pack mismatch", () => {
  const st = createStore();
  const host = client();
  const guest = client();
  st.onMessage(host, JSON.stringify({ v: 1, t: "create", nick: "H", packHash: HASH, region: "J", buildId: BUILD }));
  assert.equal(host.inbox.at(-1).code, "REGION_MISMATCH");
  st.onMessage(host, JSON.stringify({ v: 1, t: "create", nick: "H", packHash: HASH, region: "U", buildId: BUILD }));
  const code = host.inbox.find((m) => m.t === "created").code;
  st.onMessage(guest, JSON.stringify({ v: 1, t: "join", code, nick: "G", packHash: "b".repeat(64), region: "U", buildId: BUILD }));
  assert.equal(guest.inbox.at(-1).code, "PACK_MISMATCH");
});

test("cfg must be 160-byte hex and pad0 zero", () => {
  const st = createStore();
  const host = client();
  st.onMessage(host, JSON.stringify({ v: 1, t: "create", nick: "H", packHash: HASH, region: "U", buildId: BUILD }));
  const cfg = "010000020203000100000101" + "00".repeat(148);
  const cfgHash = createHash("sha256").update(Buffer.from(cfg, "hex")).digest("hex");
  st.onMessage(host, JSON.stringify({ v: 1, t: "cfg", cfg, cfgHash }));
  assert.equal(host.inbox.at(-1).code, "BAD_CFG");
});

test("sdp must start with v=", () => {
  const st = createStore();
  const host = client();
  const guest = client();
  st.onMessage(host, JSON.stringify({ v: 1, t: "create", nick: "H", packHash: HASH, region: "U", buildId: BUILD }));
  const code = host.inbox.find((m) => m.t === "created").code;
  st.onMessage(guest, JSON.stringify({ v: 1, t: "join", code, nick: "G", packHash: HASH, region: "U", buildId: BUILD }));
  st.onMessage(host, JSON.stringify({ v: 1, t: "sdp", from: 0, to: 1, desc: { type: "offer", sdp: "garbage" } }));
  assert.equal(host.inbox.at(-1).code, "BAD_SDP");
  st.onMessage(host, JSON.stringify({ v: 1, t: "sdp", from: 0, to: 1, desc: { type: "offer", sdp: "v=0\r\n" } }));
  assert.equal(guest.inbox.at(-1).t, "sdp");
});
