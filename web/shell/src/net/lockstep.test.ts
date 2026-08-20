import { describe, expect, it } from "vitest";
import {
  ckEqual,
  ICE_CONNECT_MS,
  ICE_FAIL_OVERLAY,
  LOCKSTEP_DROP_MS,
  LOCKSTEP_STALL_MS,
  LockstepSession,
  type CkSnap,
  type LockstepEngine,
} from "./lockstep.ts";
import type { PortPad } from "./datagram.ts";

function fakeEngine(): LockstepEngine & { ticks: number[]; pads: PortPad[][]; nseats: number; seed: number } {
  const ticks: number[] = [];
  const pads: PortPad[][] = [];
  const eng: LockstepEngine & { ticks: number[]; pads: PortPad[][]; nseats: number; seed: number } = {
    ticks,
    pads,
    nseats: 0,
    seed: 0,
    beginMatch(n, seed) {
      eng.nseats = n;
      eng.seed = seed;
    },
    applyTick(tick, p) {
      ticks.push(tick);
      pads.push(p.map((x) => ({ ...x })));
      return 0;
    },
    snapshot(tick): CkSnap {
      const last = pads[pads.length - 1] ?? [];
      const mix = last.reduce((n, p) => n + ((p.y & 0xff) << (8 * (p.buttons ? 1 : 0))), 0);
      return {
        tick,
        rng_lo: 2,
        chr_rng_lo: 1,
        crc_players: mix >>> 0,
        crc_chrs: 0,
        crc_objectives: 0,
      };
    },
  };
  return eng;
}

const idle = { x: 0, y: 0, buttons: 0 };
const walk = { x: 0, y: -70, buttons: 0 };

describe("LockstepSession", () => {
  it("waits until every seat has tick 0, then runs delay-1", () => {
    const eng = fakeEngine();
    const a = new LockstepSession(0, 2, 1, eng);
    a.start(1);
    expect(eng.nseats).toBe(2);
    expect(eng.seed).toBe(1);
    expect(a.hasAll(0)).toBe(false);
    expect(a.missingSeat(0)).toBe(1);
    const ev0 = a.step(0, walk);
    expect(ev0.some((e) => e.t === "ran")).toBe(false);
    a.ingest([
      { tick: 0, seat: 1, nseats: 2, delay: 1, pad: idle, simCrc: 0 },
      { tick: 1, seat: 1, nseats: 2, delay: 1, pad: idle, simCrc: 0 },
    ]);
    expect(a.meshLinked).toBe(true);
    const ev1 = a.step(50, walk);
    expect(ev1).toEqual([expect.objectContaining({ t: "ran", tick: 0 })]);
    expect(eng.ticks).toEqual([0]);
    expect(eng.pads[0][0].y).toBe(0); /* prefill idle */
    expect(a.nextTick).toBe(1);
  });

  it("ICE fail / never-open channel is not P2 left", () => {
    const a = new LockstepSession(0, 2, 1, fakeEngine());
    a.start(1);
    a.step(0, idle);
    const stall = a.step(LOCKSTEP_STALL_MS, idle);
    expect(stall.some((e) => e.t === "stall" && e.seat === 1)).toBe(true);
    expect(a.stalled).toBe(true);
    expect(a.overlay).toBe("waiting for the other player");
    expect(a.overlay).not.toMatch(/P2 left/);
    const early = a.step(ICE_CONNECT_MS - 1, idle);
    expect(early.some((e) => e.t === "drop")).toBe(false);
    const drop = a.step(ICE_CONNECT_MS, idle);
    expect(drop.some((e) => e.t === "drop")).toBe(true);
    expect(a.dropped).toBe(true);
    expect(a.overlay).toBe(ICE_FAIL_OVERLAY);
    expect(a.overlay).not.toMatch(/P2 left/);
    expect(a.overlay).not.toMatch(/Host disconnected/);
  });

  it("10s drop says P2 left only after inp has opened", () => {
    const a = new LockstepSession(0, 2, 1, fakeEngine());
    a.start(1);
    a.markLinked();
    a.step(0, idle);
    const stall = a.step(LOCKSTEP_STALL_MS, idle);
    expect(stall.some((e) => e.t === "stall" && e.seat === 1)).toBe(true);
    expect(a.overlay).toMatch(/waiting for P2/);
    const mid = a.step(ICE_CONNECT_MS, idle);
    expect(mid.some((e) => e.t === "drop")).toBe(false);
    const drop = a.step(LOCKSTEP_DROP_MS, idle);
    expect(drop.some((e) => e.t === "drop")).toBe(true);
    expect(a.dropped).toBe(true);
    expect(a.overlay).toBe("P2 left. Match ended.");
    expect(a.overlay).not.toBe(ICE_FAIL_OVERLAY);
  });

  it("channel closed before link is a connect timeout, not P2 left", () => {
    const a = new LockstepSession(0, 2, 2, fakeEngine());
    a.start(1);
    a.step(0, idle);
    a.endMatch(ICE_FAIL_OVERLAY, 1);
    expect(a.overlay).toBe(ICE_FAIL_OVERLAY);
    expect(a.overlay).not.toMatch(/P2 left/);
    expect(a.halted).toBe(true);
  });

  it("DESYNC when remote checksum disagrees", () => {
    const a = new LockstepSession(0, 2, 1, fakeEngine());
    a.start(1);
    a.ingest([
      { tick: 0, seat: 1, nseats: 2, delay: 1, pad: idle, simCrc: 0 },
      { tick: 1, seat: 1, nseats: 2, delay: 1, pad: idle, simCrc: 0 },
    ]);
    const ran = a.step(0, idle);
    expect(ran[0]?.t).toBe("ran");
    const ck = a.localCk.get(0)!;
    const ev = a.acceptRemoteCk({ ...ck, crc_players: ck.crc_players ^ 1 });
    expect(ev?.t).toBe("desync");
    expect(a.desynced).toBe(true);
    expect(ckEqual(ck, ck)).toBe(true);
  });

  it("hidden tab stalls without running", () => {
    const eng = fakeEngine();
    const a = new LockstepSession(0, 2, 1, eng);
    a.start(1);
    a.ingest([{ tick: 0, seat: 1, nseats: 2, delay: 1, pad: idle, simCrc: 0 }]);
    const ev = a.step(0, walk, true);
    expect(ev.some((e) => e.t === "hidden")).toBe(true);
    expect(eng.ticks).toEqual([]);
    expect(a.overlay).toMatch(/visible/);
  });

  it("4P lockstep runs when every seat is present", () => {
    const eng = fakeEngine();
    const a = new LockstepSession(0, 4, 1, eng);
    a.start(7);
    expect(eng.nseats).toBe(4);
    for (const seat of [1, 2, 3]) {
      a.ingest([
        { tick: 0, seat, nseats: 4, delay: 1, pad: idle, simCrc: 0 },
        { tick: 1, seat, nseats: 4, delay: 1, pad: idle, simCrc: 0 },
      ]);
    }
    const ev = a.step(0, idle);
    expect(ev.some((e) => e.t === "ran" && e.tick === 0)).toBe(true);
    expect(eng.pads[0]).toHaveLength(4);
  });

  it("host disconnect ends the match immediately", () => {
    const a = new LockstepSession(1, 3, 2, fakeEngine());
    a.start(1);
    a.markLinked();
    const ev = a.endMatch("Host disconnected. Match ended.", 0);
    expect(ev).toEqual({ t: "drop", seat: 0 });
    expect(a.dropped).toBe(true);
    expect(a.halted).toBe(true);
    expect(a.overlay).toMatch(/Host disconnected/);
    expect(a.overlay).not.toBe(ICE_FAIL_OVERLAY);
    expect(a.step(50, idle)).toEqual([]);
  });
});
