import http from "node:http";
import { WebSocketServer } from "ws";
import { createStore } from "./rooms.js";

const PORT = Number(process.env.PORT || 18787);
const HOST = process.env.HOST || "127.0.0.1";
const store = createStore();
let ids = 0;

const server = http.createServer((req, res) => {
  const url = req.url || "/";
  if (url.startsWith("/api/health")) {
    res.writeHead(200, { "content-type": "application/json" });
    res.end(JSON.stringify(store.health()));
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
