import { encodeMatchConfig, hexBytes, MATCH_CONFIG_REGION_U } from "./match_config.ts";
import { sha256Software } from "../../../extractor/src/sha256.ts";
import type { RosterSeat, SignalMsg } from "./wire.ts";
import { parseSignalFrame } from "./wire.ts";

export type LobbyHandlers = {
  onStatus(text: string): void;
  onRoster(seats: RosterSeat[]): void;
  onCode(code: string, seat: number): void;
  onError(code: string, msg: string): void;
  onCfg(cfg: string, cfgHash: string): void;
  onStart(cfgHash: string): void;
  onSdp(from: number, to: number, desc: { type: "offer" | "answer"; sdp: string }): void;
  onIce(from: number, to: number, cand: { candidate: string; sdpMid?: string; sdpMLineIndex?: number }): void;
  onRelay(from: number, kind: "inp" | "ctl", data: string): void;
};

export function defaultSignalUrl(): string {
  const q = new URLSearchParams(typeof location !== "undefined" ? location.search : "");
  const override = q.get("signal");
  if (override)
    return override;
  if (typeof location === "undefined")
    return "ws://127.0.0.1:18787/ws";
  const proto = location.protocol === "https:" ? "wss:" : "ws:";
  return `${proto}//${location.host}/ws`;
}

function buildIdBytes(): Uint8Array {
  const b = new Uint8Array(20);
  const s = "silveriris-buildid!!";
  for (let i = 0; i < 20; i++)
    b[i] = s.charCodeAt(i);
  return b;
}

export function packedLobbyCfg(
  packHashHex: string,
  nseats = 2,
  delayTicks = 2,
): { cfg: string; cfgHash: string } {
  const n = Math.max(2, Math.min(4, nseats | 0));
  const delay = Math.max(1, Math.min(3, delayTicks | 0));
  const packHash = new Uint8Array(32);
  for (let i = 0; i < 32; i++)
    packHash[i] = parseInt(packHashHex.slice(i * 2, i * 2 + 2), 16) || 0;
  const packed = encodeMatchConfig({
    protocol: 1,
    region: MATCH_CONFIG_REGION_U,
    nseats: n,
    delayTicks: delay,
    speedgraphframes: 3,
    aimSight: 0,
    autoAim: 1,
    lookAhead: 0,
    aimControl: 0,
    radar: 1,
    pad0: 0,
    rngSeed: 1,
    stage: 34,
    scenario: 0,
    gameLength: 2,
    chars: [1, 2, 3, 4],
    handicaps: [0, 0, 0, 0],
    favWeapons: [
      [0, 0],
      [0, 0],
      [0, 0],
      [0, 0],
    ],
    slider007: [0, 0, 0, 0],
    packHash,
    buildId: buildIdBytes(),
  });
  const cfg = hexBytes(packed);
  return { cfg, cfgHash: sha256Software(packed) };
}

export class SignalClient {
  private ws: WebSocket | null = null;
  constructor(private readonly handlers: LobbyHandlers) {}

  connect(url: string): void {
    this.close();
    const ws = new WebSocket(url);
    this.ws = ws;
    ws.addEventListener("open", () => this.send({ v: 1, t: "hello", proto: 1 }));
    ws.addEventListener("message", (ev) => {
      const msg = parseSignalFrame(String(ev.data));
      this.dispatch(msg);
    });
    ws.addEventListener("close", () => this.handlers.onStatus("signal closed"));
  }

  send(msg: SignalMsg): void {
    if (this.ws?.readyState === WebSocket.OPEN)
      this.ws.send(JSON.stringify(msg));
  }

  close(): void {
    this.ws?.close();
    this.ws = null;
  }

  private dispatch(msg: SignalMsg): void {
    if (msg.t === "error")
      this.handlers.onError(msg.code, msg.msg);
    else if (msg.t === "created")
      this.handlers.onCode(msg.code, 0);
    else if (msg.t === "joined")
      this.handlers.onCode(msg.code, msg.seat);
    else if (msg.t === "roster")
      this.handlers.onRoster(msg.seats);
    else if (msg.t === "cfg")
      this.handlers.onCfg(msg.cfg, msg.cfgHash);
    else if (msg.t === "start")
      this.handlers.onStart(msg.cfgHash);
    else if (msg.t === "hello")
      this.handlers.onStatus("signal ok");
    else if (msg.t === "sdp")
      this.handlers.onSdp(msg.from, msg.to, msg.desc);
    else if (msg.t === "ice")
      this.handlers.onIce(msg.from, msg.to, msg.cand);
    else if (msg.t === "relay")
      this.handlers.onRelay(msg.from, msg.kind, msg.data);
  }
}
