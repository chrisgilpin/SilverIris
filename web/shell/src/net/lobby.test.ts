import { describe, expect, it } from "vitest";
import { packedLobbyCfg } from "./lobby.ts";
import { bytesFromHex, decodeMatchConfig } from "./match_config.ts";

const HASH = "ab".repeat(32);

describe("packedLobbyCfg seats", () => {
  it("encodes 3P and 4P nseats plus LAN delay 1", () => {
    const three = packedLobbyCfg(HASH, 3, 2);
    const d3 = decodeMatchConfig(bytesFromHex(three.cfg));
    expect(d3?.nseats).toBe(3);
    expect(d3?.delayTicks).toBe(2);
    const four = packedLobbyCfg(HASH, 4, 1);
    const d4 = decodeMatchConfig(bytesFromHex(four.cfg));
    expect(d4?.nseats).toBe(4);
    expect(d4?.delayTicks).toBe(1);
    expect(four.cfgHash).not.toBe(three.cfgHash);
  });

  it("clamps nseats to 2..4", () => {
    expect(decodeMatchConfig(bytesFromHex(packedLobbyCfg(HASH, 1).cfg))?.nseats).toBe(2);
    expect(decodeMatchConfig(bytesFromHex(packedLobbyCfg(HASH, 9).cfg))?.nseats).toBe(4);
  });

  it("packs Complex stage 31 and 5-pt length", () => {
    const packed = packedLobbyCfg(HASH, 2, 2, 31, 4);
    const d = decodeMatchConfig(bytesFromHex(packed.cfg));
    expect(d?.stage).toBe(31);
    expect(d?.gameLength).toBe(4);
    expect(packedLobbyCfg(HASH, 2, 2, 34, 2).cfgHash).not.toBe(packed.cfgHash);
  });
});
