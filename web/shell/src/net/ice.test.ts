import { describe, expect, it } from "vitest";
import { rtcConfiguration, STUN_HOST } from "./ice.ts";

describe("ICE helper", () => {
  it("defaults to all and STUN on this host", () => {
    const c = rtcConfiguration();
    expect(c.iceTransportPolicy).toBe("all");
    expect(c.iceServers?.[0]?.urls).toContain(`stun:${STUN_HOST}:3478`);
  });

  it("turnForce stubs relay-only", () => {
    expect(rtcConfiguration({ turnForce: true }).iceTransportPolicy).toBe("relay");
  });
});
