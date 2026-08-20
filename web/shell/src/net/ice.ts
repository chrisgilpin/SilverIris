/** ICE helper. STUN + optional coturn TURN (ephemeral REST creds from signal). */

export const STUN_HOST = "007.goodhouseinc.com";
export const STUN_GOOGLE = "stun:stun.l.google.com:19302";

export type TurnCred = { username: string; credential: string };

export type IceOpts = { turnForce?: boolean; turn?: TurnCred | null };

export function turnUrls(host = STUN_HOST): string[] {
  return [
    `turn:${host}:3478?transport=udp`,
    `turn:${host}:3478?transport=tcp`,
    `turns:${host}:5349?transport=tcp`,
  ];
}

export function rtcConfiguration(opts: IceOpts = {}): RTCConfiguration {
  const iceServers: RTCIceServer[] = [
    { urls: [`stun:${STUN_HOST}:3478`] },
    { urls: [STUN_GOOGLE] },
  ];
  if (opts.turn?.username && opts.turn.credential) {
    iceServers.push({
      urls: turnUrls(STUN_HOST),
      username: opts.turn.username,
      credential: opts.turn.credential,
    });
  }
  return {
    iceServers,
    iceTransportPolicy: opts.turnForce ? "relay" : "all",
  };
}

export function iceCspConnect(): string {
  return `stun:${STUN_HOST}:3478 turn:${STUN_HOST}:3478 turns:${STUN_HOST}:5349 ${STUN_GOOGLE}`;
}

/** Classify the nominated local candidate from RTCStats. */
export function icePathFromStats(stats: Iterable<{ type?: string; nominated?: boolean; selected?: boolean; state?: string; localCandidateId?: string; id?: string; candidateType?: string }>): "host" | "srflx" | "relay" | null {
  const list = Array.from(stats);
  const pair = list.find((s) => s.type === "candidate-pair" && (s.nominated || s.selected || s.state === "succeeded"));
  if (!pair)
    return null;
  const local = list.find((s) => s.id === pair.localCandidateId);
  const t = local?.candidateType;
  if (t === "host" || t === "srflx" || t === "relay")
    return t;
  return null;
}
