# silveriris-signal

In-memory lobby rooms. No ROM, no pack bytes, no accounts, no password gate.

```
npm test
PORT=18787 npm start
```

`GET /api/health` → `{ ok, rooms, proto }`. WS `/ws` JSON `v:1`.

Limits: 10 creates / 5 min / IP, 30 joins / 5 min / IP. Region U only.
Unused rooms 30 min, idle 2 h. Host disconnect closes the room.

Compose: `docker compose -f deploy/docker-compose.yml up` then `?signal=ws://127.0.0.1:18787/ws`.

TURN: create/join mints room-scoped REST HMAC creds when the deploy-time shared hex is set. GET /api/turn lists URLs only. wsRelay requires inProgress && ice_fail.
