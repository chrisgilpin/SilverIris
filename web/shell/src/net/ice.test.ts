import { describe, expect, it } from "vitest";
import { iceCspConnect, icePathFromStats, rtcConfiguration, STUN_GOOGLE, STUN_HOST, turnUrls } from "./ice.ts";

describe("ICE helper", () => {
  it("defaults to all and STUN on this host plus Google srflx", () => {
    const c = rtcConfiguration();
    expect(c.iceTransportPolicy).toBe("all");
    const urls = (c.iceServers ?? []).flatMap((s) => (Array.isArray(s.urls) ? s.urls : [s.urls]));
    expect(urls).toContain(`stun:${STUN_HOST}:3478`);
    expect(urls).toContain(STUN_GOOGLE);
    expect(urls.some((u) => String(u).startsWith("turn:"))).toBe(false);
  });

  it("turnForce is relay-only and still needs minted creds for TURN urls", () => {
    expect(rtcConfiguration({ turnForce: true }).iceTransportPolicy).toBe("relay");
    const withCred = rtcConfiguration({
      turnForce: true,
      turn: { username: "1:ROOM", credential: "x" },
    });
    expect(withCred.iceTransportPolicy).toBe("relay");
    const urls = (withCred.iceServers ?? []).flatMap((s) => (Array.isArray(s.urls) ? s.urls : [s.urls]));
    for (const u of turnUrls())
      expect(urls).toContain(u);
    const turn = (withCred.iceServers ?? []).find((s) => s.username === "1:ROOM");
    expect(turn?.credential).toBe("x");
  });

  it("CSP connect list includes STUN, TURN, TURNS, and Google", () => {
    expect(iceCspConnect()).toContain(`stun:${STUN_HOST}:3478`);
    expect(iceCspConnect()).toContain(`turn:${STUN_HOST}:3478`);
    expect(iceCspConnect()).toContain(`turns:${STUN_HOST}:5349`);
    expect(iceCspConnect()).toContain(STUN_GOOGLE);
  });

  it("classifies nominated candidate-pair local type", () => {
    const stats = [
      { type: "candidate-pair", nominated: true, localCandidateId: "L1", state: "succeeded" },
      { type: "local-candidate", id: "L1", candidateType: "relay" },
    ];
    expect(icePathFromStats(stats)).toBe("relay");
    expect(icePathFromStats([{ type: "transport" }])).toBeNull();
  });
});
