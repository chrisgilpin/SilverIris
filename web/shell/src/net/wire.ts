export type TurnIce = {
  urls: string[];
  username: string;
  credential: string;
  ttl?: number;
};

export type SignalMsg =
  | { v: 1; t: "hello"; proto: 1 }
  | { v: 1; t: "create"; nick: string; packHash: string; region: "U" | "J" | "E"; buildId: string }
  | { v: 1; t: "created"; code: string; seat: 0; turn?: TurnIce }
  | { v: 1; t: "join"; code: string; nick: string; packHash: string; region: "U" | "J" | "E"; buildId: string }
  | { v: 1; t: "joined"; code: string; seat: 1 | 2 | 3; hostNick: string; turn?: TurnIce }
  | { v: 1; t: "ice_fail" }
  | { v: 1; t: "ice_ok"; path: "host" | "srflx" | "relay" }
  | { v: 1; t: "roster"; seats: RosterSeat[] }
  | { v: 1; t: "cfg"; cfg: string; cfgHash: string }
  | { v: 1; t: "ready"; seat: number; ready: boolean }
  | { v: 1; t: "kick"; seat: number }
  | { v: 1; t: "close" }
  | { v: 1; t: "start"; cfgHash: string }
  | { v: 1; t: "sdp"; from: number; to: number; desc: { type: "offer" | "answer"; sdp: string } }
  | { v: 1; t: "ice"; from: number; to: number; cand: { candidate: string; sdpMid?: string; sdpMLineIndex?: number } }
  | { v: 1; t: "relay"; from: number; to?: number; kind: "inp" | "ctl"; data: string }
  | { v: 1; t: "error"; code: ErrorCode; msg: string };

export interface RosterSeat {
  seat: number;
  nick: string;
  packHash: string;
  region: "U" | "J" | "E";
  ready: boolean;
}

export type ErrorCode =
  | "ROOM_NOT_FOUND"
  | "ROOM_FULL"
  | "ROOM_IN_PROGRESS"
  | "PACK_MISMATCH"
  | "REGION_MISMATCH"
  | "BUILD_MISMATCH"
  | "RATE_LIMIT"
  | "BAD_NICK"
  | "EXPIRED"
  | "BAD_SDP"
  | "BAD_ICE"
  | "BAD_CFG";

export const SIGNAL_MAX_BYTES = 16 * 1024;
export const SDP_MAX = 8 * 1024;
export const ICE_MAX = 1024;

export function parseSignalFrame(raw: string): SignalMsg | { v: 1; t: "error"; code: ErrorCode; msg: string } {
  if (raw.length > SIGNAL_MAX_BYTES)
    return { v: 1, t: "error", code: "BAD_CFG", msg: "frame too large" };
  let j: unknown;
  try {
    j = JSON.parse(raw);
  } catch {
    return { v: 1, t: "error", code: "BAD_CFG", msg: "not json" };
  }
  if (!j || typeof j !== "object" || (j as { v?: unknown }).v !== 1 || typeof (j as { t?: unknown }).t !== "string")
    return { v: 1, t: "error", code: "BAD_CFG", msg: "v/t" };
  return j as SignalMsg;
}

export function validateSdp(desc: { type?: string; sdp?: string }): boolean {
  if (desc.type !== "offer" && desc.type !== "answer")
    return false;
  if (typeof desc.sdp !== "string" || desc.sdp.length > SDP_MAX)
    return false;
  return desc.sdp.startsWith("v=");
}

export function validateIce(cand: { candidate?: string }): boolean {
  if (typeof cand.candidate !== "string" || cand.candidate.length > ICE_MAX)
    return false;
  return cand.candidate.startsWith("candidate:");
}


export function validateRelay(kind: string, data: string): boolean {
  if (kind !== "inp" && kind !== "ctl")
    return false;
  if (typeof data !== "string")
    return false;
  if (kind === "inp")
    return data.length <= 1024 && data.length % 2 === 0 && /^[0-9a-f]*$/i.test(data);
  return data.length >= 2 && data.length <= 2048;
}

export function nickOk(nick: string): boolean {
  if (nick.length < 1 || nick.length > 16)
    return false;
  if (nick.includes("@"))
    return false;
  if (/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(nick))
    return false;
  return true;
}

export function packHashOk(h: string): boolean {
  return /^[0-9a-f]{64}$/.test(h);
}
