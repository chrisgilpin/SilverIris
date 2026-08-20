/** ICE helper. TURN is PR-18; STUN is this box. LAN prefers host via iceTransportPolicy all. */

export const STUN_HOST = "007.goodhouseinc.com";

export type IceOpts = { turnForce?: boolean };

export function rtcConfiguration(opts: IceOpts = {}): RTCConfiguration {
  return {
    iceServers: [{ urls: [`stun:${STUN_HOST}:3478`] }],
    iceTransportPolicy: opts.turnForce ? "relay" : "all",
  };
}

export function iceCspConnect(): string {
  return `stun:${STUN_HOST}:3478 turn:${STUN_HOST}:3478`;
}
