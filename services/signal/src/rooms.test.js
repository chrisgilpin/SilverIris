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

test("3rd and 4th join; 5th is ROOM_FULL", () => {
  const st = createStore();
  const host = client();
  const p2 = client();
  const p3 = client();
  const p4 = client();
  const p5 = client();
  st.onMessage(host, JSON.stringify({ v: 1, t: "create", nick: "H", packHash: HASH, region: "U", buildId: BUILD }));
  const code = host.inbox.find((m) => m.t === "created").code;
  st.onMessage(p2, JSON.stringify({ v: 1, t: "join", code, nick: "G2", packHash: HASH, region: "U", buildId: BUILD }));
  st.onMessage(p3, JSON.stringify({ v: 1, t: "join", code, nick: "G3", packHash: HASH, region: "U", buildId: BUILD }));
  st.onMessage(p4, JSON.stringify({ v: 1, t: "join", code, nick: "G4", packHash: HASH, region: "U", buildId: BUILD }));
  assert.equal(p2.inbox.find((m) => m.t === "joined")?.seat, 1);
  assert.equal(p3.inbox.find((m) => m.t === "joined")?.seat, 2);
  assert.equal(p4.inbox.find((m) => m.t === "joined")?.seat, 3);
  const roster = host.inbox.filter((m) => m.t === "roster").at(-1);
  assert.deepEqual(roster.seats.map((s) => s.seat), [0, 1, 2, 3]);
  st.onMessage(p5, JSON.stringify({ v: 1, t: "join", code, nick: "G5", packHash: HASH, region: "U", buildId: BUILD }));
  assert.equal(p5.inbox.at(-1).code, "ROOM_FULL");
});

test("host disconnect during match ends the room", () => {
  const st = createStore();
  const host = client();
  const guest = client();
  st.onMessage(host, JSON.stringify({ v: 1, t: "create", nick: "H", packHash: HASH, region: "U", buildId: BUILD }));
  const code = host.inbox.find((m) => m.t === "created").code;
  st.onMessage(guest, JSON.stringify({ v: 1, t: "join", code, nick: "G", packHash: HASH, region: "U", buildId: BUILD }));
  const cfg = "010000020203000100000101" + "00".repeat(148);
  /* pad0 at offset 11 must be 00; region at offset 2 is 00. This fixture is still BAD_CFG
     in the hex-length/hash test above because it is all-zero after the header. Use a
     valid 160-byte buffer: protocol 1, region 0, nseats 2, delay 2, sgf 3, rest zero. */
  const good = Buffer.alloc(160);
  good.writeUInt16LE(1, 0);
  good[3] = 2;
  good[4] = 2;
  good[5] = 3;
  const cfgHex = good.toString("hex");
  const cfgHash = createHash("sha256").update(good).digest("hex");
  st.onMessage(host, JSON.stringify({ v: 1, t: "cfg", cfg: cfgHex, cfgHash }));
  assert.equal(host.inbox.at(-1).t, "cfg");
  st.onMessage(host, JSON.stringify({ v: 1, t: "start", cfgHash }));
  assert.equal(guest.inbox.at(-1).t, "start");
  guest.inbox.length = 0;
  st.onDisconnect(host);
  assert.equal(guest.inbox.at(-1).code, "EXPIRED");
  assert.match(guest.inbox.at(-1).msg, /host/i);
  assert.equal(st.health().rooms, 0);
});

test("relay before start is rejected; after start is forwarded", () => {
  const st = createStore();
  const host = client();
  const guest = client();
  st.onMessage(host, JSON.stringify({ v: 1, t: "create", nick: "H", packHash: HASH, region: "U", buildId: BUILD }));
  const code = host.inbox.find((m) => m.t === "created").code;
  st.onMessage(guest, JSON.stringify({ v: 1, t: "join", code, nick: "G", packHash: HASH, region: "U", buildId: BUILD }));
  st.onMessage(host, JSON.stringify({ v: 1, t: "relay", kind: "inp", data: "aa" }));
  assert.equal(host.inbox.at(-1).code, "BAD_CFG");
  const good = Buffer.alloc(160);
  good.writeUInt16LE(1, 0);
  good[3] = 2;
  good[4] = 2;
  good[5] = 3;
  const cfgHex = good.toString("hex");
  const cfgHash = createHash("sha256").update(good).digest("hex");
  st.onMessage(host, JSON.stringify({ v: 1, t: "cfg", cfg: cfgHex, cfgHash }));
  st.onMessage(host, JSON.stringify({ v: 1, t: "start", cfgHash }));
  guest.inbox.length = 0;
  st.onMessage(host, JSON.stringify({ v: 1, t: "relay", kind: "inp", data: "aabb" }));
  const rel = guest.inbox.find((m) => m.t === "relay");
  assert.equal(rel.kind, "inp");
  assert.equal(rel.data, "aabb");
  assert.equal(rel.from, 0);
  host.inbox.length = 0;
  st.onMessage(guest, JSON.stringify({ v: 1, t: "relay", kind: "ctl", data: '{"t":"bye"}' }));
  const bye = host.inbox.find((m) => m.t === "relay");
  assert.equal(bye.kind, "ctl");
  assert.equal(bye.from, 1);
});

test("relay rate-limit is 64KB/s per room", () => {
  const st = createStore();
  const host = client();
  const guest = client();
  st.onMessage(host, JSON.stringify({ v: 1, t: "create", nick: "H", packHash: HASH, region: "U", buildId: BUILD }));
  const code = host.inbox.find((m) => m.t === "created").code;
  st.onMessage(guest, JSON.stringify({ v: 1, t: "join", code, nick: "G", packHash: HASH, region: "U", buildId: BUILD }));
  const good = Buffer.alloc(160);
  good.writeUInt16LE(1, 0);
  good[3] = 2;
  good[4] = 2;
  good[5] = 3;
  const cfgHex = good.toString("hex");
  const cfgHash = createHash("sha256").update(good).digest("hex");
  st.onMessage(host, JSON.stringify({ v: 1, t: "cfg", cfg: cfgHex, cfgHash }));
  st.onMessage(host, JSON.stringify({ v: 1, t: "start", cfgHash }));
  const payload = "ab".repeat(400);
  let limited = false;
  for (let i = 0; i < 120; i++) {
    st.onMessage(host, JSON.stringify({ v: 1, t: "relay", kind: "inp", data: payload }));
    if (host.inbox.at(-1)?.code === "RATE_LIMIT") {
      limited = true;
      break;
    }
  }
  assert.equal(limited, true);
});
