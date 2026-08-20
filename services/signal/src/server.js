import http from "node:http";
import { WebSocketServer } from "ws";
import { createStore } from "./rooms.js";
import { TURN_TTL_SEC, turnUrls } from "./turncred.js";

const PORT = Number(process.env.PORT || 18787);
const HOST = process.env.HOST || "127.0.0.1";
const TURN_HOST = process.env.TURN_HOST || "007.goodhouseinc.com";
const store = createStore({
  turnSecret: process.env.TURN_AUTH_SECRET || "",
  turnHost: TURN_HOST,
});
let ids = 0;

const ICE_COUNTERS = new Set(["ice_ok_host", "ice_ok_srflx", "ice_ok_relay", "ice_fail", "ws_relay_used"]);

function readJson(req, limit = 4096) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    let n = 0;
    req.on("data", (c) => {
      n += c.length;
      if (n > limit) {
        reject(new Error("too large"));
        req.destroy();
        return;
      }
      chunks.push(c);
    });
    req.on("end", () => {
      if (!chunks.length)
        return resolve({});
      try {
        resolve(JSON.parse(Buffer.concat(chunks).toString()));
      } catch (e) {
        reject(e);
      }
    });
    req.on("error", reject);
  });
}

const server = http.createServer((req, res) => {
  const url = req.url || "/";
  if (url.startsWith("/api/health")) {
    res.writeHead(200, { "content-type": "application/json" });
    res.end(JSON.stringify(store.health()));
    return;
  }
  if (url.startsWith("/api/turn")) {
    res.writeHead(200, { "content-type": "application/json" });
    res.end(JSON.stringify({
      ok: true,
      urls: turnUrls(TURN_HOST),
      realm: TURN_HOST,
      ttl: TURN_TTL_SEC,
      auth: "long-term-rest",
    }));
    return;
  }
  if (url.startsWith("/api/m") && req.method === "POST") {
    readJson(req).then((body) => {
      const bump = body && typeof body === "object" ? body : {};
      for (const k of ICE_COUNTERS) {
        const n = Number(bump[k]);
        if (Number.isFinite(n) && n > 0 && n < 100)
          store.counters[k] = (store.counters[k] || 0) + (n | 0);
      }
      res.writeHead(200, { "content-type": "application/json" });
      res.end(JSON.stringify(store.metrics()));
    }).catch(() => {
      res.writeHead(400);
      res.end("bad json");
    });
    return;
  }
  if (url.startsWith("/api/m")) {
    res.writeHead(200, { "content-type": "application/json" });
    res.end(JSON.stringify(store.metrics()));
    return;
  }
  res.writeHead(404);
  res.end("not found");
});

const wss = new WebSocketServer({ server, path: "/ws" });

wss.on("connection", (ws, req) => {
  const ip = (req.headers["x-forwarded-for"] || req.socket.remoteAddress || "0").toString().split(",")[0].trim();
  const client = {
    id: `c${++ids}`,
    ip,
    room: null,
    seat: -1,
    send(s) {
      if (ws.readyState === ws.OPEN)
        ws.send(s);
    },
  };
  ws.on("message", (data) => store.onMessage(client, data.toString()));
  ws.on("close", () => store.onDisconnect(client));
});

server.listen(PORT, HOST, () => {
  console.log(`silveriris-signal ${HOST}:${PORT}`);
});
