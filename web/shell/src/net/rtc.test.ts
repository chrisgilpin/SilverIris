import { describe, expect, it } from "vitest";
import { dataChannelsPerPeer, meshOfferTargets } from "./rtc.ts";

describe("4P full-mesh DataChannels", () => {
  it("lower seat offers to every higher seat", () => {
    const seats = [0, 1, 2, 3];
    expect(meshOfferTargets(0, seats)).toEqual([1, 2, 3]);
    expect(meshOfferTargets(1, seats)).toEqual([2, 3]);
    expect(meshOfferTargets(2, seats)).toEqual([3]);
    expect(meshOfferTargets(3, seats)).toEqual([]);
    const links = seats.flatMap((s) => meshOfferTargets(s, seats));
    expect(links).toHaveLength(6);
  });

  it("each peer opens 6 DataChannels in 4P (inp+ctl × 3 remotes)", () => {
    expect(dataChannelsPerPeer(4)).toBe(6);
    expect(dataChannelsPerPeer(3)).toBe(4);
    expect(dataChannelsPerPeer(2)).toBe(2);
    expect(dataChannelsPerPeer(1)).toBe(0);
  });
});
