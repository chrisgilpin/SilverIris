import { describe, expect, it } from "vitest";
import { buildReport, encodeTapeExcerpt, REPORT_SCHEMA } from "./report.ts";

describe("silveriris-report/1", () => {
  it("tags schema and both RNG fields", () => {
    const r = buildReport({
      buildId: "test",
      packHash: "aa",
      nseats: 1,
      seat: 0,
      tick: 12,
      checksums: [
        {
          tick: 12,
          rng_lo: 1,
          chr_rng_lo: 2,
          crc_players: 3,
          crc_chrs: 4,
          crc_objectives: 5,
        },
      ],
      tapeExcerpt: { fromTick: 12, toTick: 12, pads: "AA==" },
      flags: { netplay: false, turnForce: false, wsRelay: false, widescreen: true },
    });
    expect(r.schema).toBe(REPORT_SCHEMA);
    expect(r.region).toBe("U");
    expect(r.speedgraphframes).toBe(3);
    expect(r.flags.netplay).toBe(false);
    expect(r.checksums[0].rng_lo).toBe(1);
    expect(r.checksums[0].chr_rng_lo).toBe(2);
    expect(r.packHash).toBe("aa");
    expect(r.tapeExcerpt.pads).toBe("AA==");
  });

  it("encodes 20-byte BIN1 blocks", () => {
    const ex = encodeTapeExcerpt(1, [{ tick: 7, pads: [{ x: -70, y: 0, buttons: 0x2000 }] }]);
    expect(ex.fromTick).toBe(7);
    expect(ex.toTick).toBe(7);
    const raw = Uint8Array.from(atob(ex.pads), (c) => c.charCodeAt(0));
    expect(raw.byteLength).toBe(20);
    expect(raw[12]).toBe(256 - 70);
    expect(raw[14]).toBe(0x00);
    expect(raw[15]).toBe(0x20);
  });
});
