import { describe, expect, it } from "vitest";
import { decodeMatchConfig, encodeMatchConfig, FIXTURE_HEX, fixtureConfig, hexBytes, MATCH_CONFIG_BYTES } from "./match_config.ts";

describe("encodeMatchConfig", () => {
  it("packs 160 bytes matching the C fixture", () => {
    const packed = encodeMatchConfig(fixtureConfig());
    expect(packed.byteLength).toBe(MATCH_CONFIG_BYTES);
    expect(packed[11]).toBe(0);
    expect(hexBytes(packed)).toBe(FIXTURE_HEX);
    expect(FIXTURE_HEX.length).toBe(320);
  });

  it("rejects pad0 != 0", () => {
    const c = fixtureConfig();
    c.pad0 = 1;
    expect(() => encodeMatchConfig(c)).toThrow(/pad0/);
  });

  it("round-trips the fixture", () => {
    const packed = encodeMatchConfig(fixtureConfig());
    const d = decodeMatchConfig(packed);
    expect(d?.nseats).toBe(2);
    expect(d?.delayTicks).toBe(2);
    expect(d?.rngSeed).toBe(0xa1b2c3d4);
    expect(d?.stage).toBe(34);
    expect(d?.pad0).toBe(0);
  });
});
