/** ICE helper. TURN is PR-18; STUN is this box plus a public extra for srflx. */

export const STUN_HOST = "007.goodhouseinc.com";
export const STUN_GOOGLE = "stun:stun.l.google.com:19302";

export type IceOpts = { turnForce?: boolean };

export function rtcConfiguration(opts: IceOpts = {}): RTCConfiguration {
  return {
    iceServers: [
      { urls: [`stun:${STUN_HOST}:3478`] },
      { urls: [STUN_GOOGLE] },
    ],
    iceTransportPolicy: opts.turnForce ? "relay" : "all",
  };
}

export function iceCspConnect(): string {
  return `stun:${STUN_HOST}:3478 turn:${STUN_HOST}:3478 ${STUN_GOOGLE}`;
}
