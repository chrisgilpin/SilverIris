import { describe, expect, it } from "vitest";
import { iceCspConnect, rtcConfiguration, STUN_GOOGLE, STUN_HOST } from "./ice.ts";

describe("ICE helper", () => {
  it("defaults to all and STUN on this host plus Google srflx", () => {
    const c = rtcConfiguration();
    expect(c.iceTransportPolicy).toBe("all");
    const urls = (c.iceServers ?? []).flatMap((s) => (Array.isArray(s.urls) ? s.urls : [s.urls]));
    expect(urls).toContain(`stun:${STUN_HOST}:3478`);
    expect(urls).toContain(STUN_GOOGLE);
  });

  it("turnForce stubs relay-only", () => {
    expect(rtcConfiguration({ turnForce: true }).iceTransportPolicy).toBe("relay");
  });

  it("CSP connect list includes both STUN hosts", () => {
    expect(iceCspConnect()).toContain(`stun:${STUN_HOST}:3478`);
    expect(iceCspConnect()).toContain(STUN_GOOGLE);
  });
});
