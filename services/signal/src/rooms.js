import { createHash } from "node:crypto";
import { codeOk, randomRoomCode } from "./codes.js";

const UNUSED_MS = 30 * 60 * 1000;
const IDLE_MS = 2 * 60 * 60 * 1000;
const CREATE_LIMIT = 10;
const JOIN_LIMIT = 30;
const WINDOW_MS = 5 * 60 * 1000;

function nickOk(nick) {
  if (typeof nick !== "string" || nick.length < 1 || nick.length > 16)
    return false;
  if (nick.includes("@"))
    return false;
  return true;
}

function packHashOk(h) {
  return typeof h === "string" && /^[0-9a-f]{64}$/.test(h);
}

function now() {
  return Date.now();
}

export function createStore() {
  /** @type {Map<string, any>} */
  const rooms = new Map();
  /** @type {Map<string, number[]>} */
  const creates = new Map();
  /** @type {Map<string, number[]>} */
  const joins = new Map();
  const counters = { creates: 0, joins: 0, errors: 0 };

  function pruneHits(map, ip) {
    const t = now();
    const arr = (map.get(ip) || []).filter((x) => t - x < WINDOW_MS);
    map.set(ip, arr);
    return arr;
  }

  function rateOk(map, ip, limit) {
    const arr = pruneHits(map, ip);
    if (arr.length >= limit)
      return false;
    arr.push(now());
    map.set(ip, arr);
    return true;
  }

  function sweep() {
    const t = now();
    for (const [code, r] of rooms) {
      const idle = t - r.lastActive;
      const unused = !r.seats.some((s, i) => i > 0 && s);
      if (idle > IDLE_MS || (unused && idle > UNUSED_MS) || (r.endedAt && t - r.endedAt > UNUSED_MS))
        rooms.delete(code);
    }
  }

  function roster(r) {
    return r.seats
      .map((s, i) => s && { seat: i, nick: s.nick, packHash: s.packHash, region: s.region, ready: !!s.ready })
      .filter(Boolean);
  }

  function send(client, msg) {
    client.send(JSON.stringify(msg));
  }

  function broadcast(r, msg) {
    for (const s of r.seats) {
      if (s?.client)
        send(s.client, msg);
    }
  }

  function err(client, code, msg) {
    counters.errors += 1;
    send(client, { v: 1, t: "error", code, msg });
  }

  function validateSdp(desc) {
    return desc && (desc.type === "offer" || desc.type === "answer") && typeof desc.sdp === "string"
      && desc.sdp.length <= 8192 && desc.sdp.startsWith("v=");
  }

  function validateIce(cand) {
    return cand && typeof cand.candidate === "string" && cand.candidate.length <= 1024
      && cand.candidate.startsWith("candidate:");
  }

  function onMessage(client, raw) {
    sweep();
    let msg;
    try {
      if (typeof raw !== "string" || raw.length > 16384)
        return err(client, "BAD_CFG", "frame");
      msg = JSON.parse(raw);
    } catch {
      return err(client, "BAD_CFG", "json");
    }
    if (!msg || msg.v !== 1 || typeof msg.t !== "string")
      return err(client, "BAD_CFG", "v");

    if (msg.t === "hello")
      return send(client, { v: 1, t: "hello", proto: 1 });

    if (msg.t === "create") {
      if (!rateOk(creates, client.ip, CREATE_LIMIT))
        return err(client, "RATE_LIMIT", "create");
      if (!nickOk(msg.nick) || !packHashOk(msg.packHash))
        return err(client, "BAD_NICK", "nick/pack");
      if (msg.region !== "U")
        return err(client, "REGION_MISMATCH", "US only");
      if (typeof msg.buildId !== "string" || msg.buildId.length < 1)
        return err(client, "BUILD_MISMATCH", "buildId");
      let code = randomRoomCode();
      for (let i = 0; i < 8 && rooms.has(code); i++)
        code = randomRoomCode();
      const room = {
        code,
        createdAt: now(),
        lastActive: now(),
        hostId: client.id,
        inProgress: false,
        ice_fail: false,
        packHash: msg.packHash,
        region: msg.region,
        buildId: msg.buildId,
        cfgHex: null,
        cfgHash: null,
        seats: [null, null, null, null],
      };
      room.seats[0] = { nick: msg.nick, packHash: msg.packHash, region: msg.region, ready: false, client };
      client.room = code;
      client.seat = 0;
      rooms.set(code, room);
      counters.creates += 1;
      send(client, { v: 1, t: "created", code, seat: 0 });
      return send(client, { v: 1, t: "roster", seats: roster(room) });
    }

    if (msg.t === "join") {
      if (!rateOk(joins, client.ip, JOIN_LIMIT))
        return err(client, "RATE_LIMIT", "join");
      if (!codeOk(msg.code) || !nickOk(msg.nick) || !packHashOk(msg.packHash))
        return err(client, "BAD_NICK", "join fields");
      const room = rooms.get(msg.code.toUpperCase());
      if (!room)
        return err(client, "ROOM_NOT_FOUND", "no room");
      if (room.inProgress)
        return err(client, "ROOM_IN_PROGRESS", "started");
      if (msg.region !== "U" || msg.region !== room.region)
        return err(client, "REGION_MISMATCH", "region");
      if (msg.packHash !== room.packHash)
        return err(client, "PACK_MISMATCH", "pack");
      if (msg.buildId !== room.buildId)
        return err(client, "BUILD_MISMATCH", "build");
      const seat = room.seats.findIndex((s) => !s);
      if (seat < 0)
        return err(client, "ROOM_FULL", "full");
      room.seats[seat] = { nick: msg.nick, packHash: msg.packHash, region: msg.region, ready: false, client };
      room.lastActive = now();
      client.room = room.code;
      client.seat = seat;
      counters.joins += 1;
      send(client, { v: 1, t: "joined", code: room.code, seat, hostNick: room.seats[0].nick });
      broadcast(room, { v: 1, t: "roster", seats: roster(room) });
      if (room.cfgHex)
        send(client, { v: 1, t: "cfg", cfg: room.cfgHex, cfgHash: room.cfgHash });
      return;
    }

    const room = client.room && rooms.get(client.room);
    if (!room)
      return err(client, "ROOM_NOT_FOUND", "not in room");
    room.lastActive = now();

    if (msg.t === "cfg") {
      if (client.seat !== 0)
        return err(client, "BAD_CFG", "host only");
      if (typeof msg.cfg !== "string" || !/^[0-9a-f]{320}$/.test(msg.cfg))
        return err(client, "BAD_CFG", "cfg hex");
      if (typeof msg.cfgHash !== "string" || !/^[0-9a-f]{64}$/.test(msg.cfgHash))
        return err(client, "BAD_CFG", "cfgHash");
      const hash = createHash("sha256").update(Buffer.from(msg.cfg, "hex")).digest("hex");
      if (hash !== msg.cfgHash)
        return err(client, "BAD_CFG", "hash");
      const region = parseInt(msg.cfg.slice(4, 6), 16);
      if (region !== 0)
        return err(client, "REGION_MISMATCH", "cfg region");
      if (msg.cfg.slice(22, 24) !== "00")
        return err(client, "BAD_CFG", "pad0");
      room.cfgHex = msg.cfg;
      room.cfgHash = msg.cfgHash;
      broadcast(room, { v: 1, t: "cfg", cfg: msg.cfg, cfgHash: msg.cfgHash });
      return;
    }

    if (msg.t === "ready") {
      const seat = client.seat;
      if (room.seats[seat])
        room.seats[seat].ready = !!msg.ready;
      broadcast(room, { v: 1, t: "roster", seats: roster(room) });
      return;
    }

    if (msg.t === "kick") {
      if (client.seat !== 0)
        return err(client, "BAD_CFG", "host only");
      const k = msg.seat | 0;
      if (k < 1 || k > 3 || !room.seats[k])
        return;
      send(room.seats[k].client, { v: 1, t: "error", code: "EXPIRED", msg: "kicked" });
      room.seats[k].client.room = null;
      room.seats[k] = null;
      broadcast(room, { v: 1, t: "roster", seats: roster(room) });
      return;
    }

    if (msg.t === "start") {
      if (client.seat !== 0)
        return err(client, "BAD_CFG", "host only");
      if (!room.cfgHash || msg.cfgHash !== room.cfgHash)
        return err(client, "BAD_CFG", "cfgHash");
      room.inProgress = true;
      broadcast(room, { v: 1, t: "start", cfgHash: room.cfgHash });
      return;
    }

    if (msg.t === "close") {
      if (client.seat === 0) {
        broadcast(room, { v: 1, t: "error", code: "EXPIRED", msg: "host closed" });
        rooms.delete(room.code);
      }
      return;
    }

    if (msg.t === "sdp") {
      if (!validateSdp(msg.desc))
        return err(client, "BAD_SDP", "sdp");
      const dest = room.seats[msg.to | 0];
      if (dest?.client)
        send(dest.client, { v: 1, t: "sdp", from: client.seat, to: msg.to | 0, desc: msg.desc });
      return;
    }

    if (msg.t === "ice") {
      if (!validateIce(msg.cand))
        return err(client, "BAD_ICE", "ice");
      const dest = room.seats[msg.to | 0];
      if (dest?.client)
        send(dest.client, { v: 1, t: "ice", from: client.seat, to: msg.to | 0, cand: msg.cand });
      return;
    }
  }

  function onDisconnect(client) {
    const room = client.room && rooms.get(client.room);
    if (!room)
      return;
    if (client.seat === 0) {
      broadcast(room, { v: 1, t: "error", code: "EXPIRED", msg: "host left" });
      rooms.delete(room.code);
      return;
    }
    room.seats[client.seat] = null;
    broadcast(room, { v: 1, t: "roster", seats: roster(room) });
  }

  return {
    rooms,
    counters,
    onMessage,
    onDisconnect,
    health() {
      sweep();
      return { ok: true, rooms: rooms.size, proto: 1 };
    },
    metrics() {
      return { ...counters, rooms: rooms.size };
    },
  };
}
