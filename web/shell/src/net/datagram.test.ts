import { describe, expect, it } from "vitest";
import {
  DATAGRAM_BYTES,
  decodeInputDatagram,
  encodeInputDatagram,
  INPUT_BLOCK_BYTES,
  INPUT_REDUNDANCY,
} from "./datagram.ts";

describe("InputDatagram", () => {
  it("round-trips N=8 newest-first blocks", () => {
    const blocks = Array.from({ length: INPUT_REDUNDANCY }, (_, i) => ({
      tick: 100 - i,
      seat: 1,
      nseats: 2,
      delay: 2,
      pad: { x: -70, y: 10, buttons: 0x2000 },
      simCrc: 0xabcdef00 + i,
    }));
    const raw = encodeInputDatagram(1, blocks);
    expect(raw.byteLength).toBe(DATAGRAM_BYTES);
    expect(8 + INPUT_REDUNDANCY * INPUT_BLOCK_BYTES).toBe(DATAGRAM_BYTES);
    const got = decodeInputDatagram(raw);
    expect(got?.seat).toBe(1);
    expect(got?.blocks[0].tick).toBe(100);
    expect(got?.blocks[0].pad.x).toBe(-70);
    expect(got?.blocks[0].pad.buttons).toBe(0x2000);
    expect(got?.blocks[7].tick).toBe(93);
  });
});
