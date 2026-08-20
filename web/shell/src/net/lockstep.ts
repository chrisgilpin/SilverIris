/** Delay lockstep scheduler. Ring lives here; sim inject is port_set_local_pad. */

import { INPUT_REDUNDANCY, type InputBlock, type PortPad } from "./datagram.ts";

export const LOCKSTEP_STALL_MS = 350;
export const LOCKSTEP_DROP_MS = 10000;
export const LOCKSTEP_RING = 64;

export type CkSnap = {
  tick: number;
  rng_lo: number;
  chr_rng_lo: number;
  crc_players: number;
  crc_chrs: number;
  crc_objectives: number;
};

export type CkMsg = CkSnap & { t: "ck" };

export type LockstepEngine = {
  beginMatch(nseats: number, seed: number): void;
  applyTick(tick: number, pads: PortPad[]): number;
  snapshot(tick: number): CkSnap;
};

export type LockstepEvent =
  | { t: "ran"; tick: number; ck: CkSnap }
  | { t: "stall"; seat: number; ms: number }
  | { t: "drop"; seat: number }
  | { t: "desync"; tick: number; local: CkSnap; remote: CkSnap }
  | { t: "hidden" };

type Slot = {
  tick: number;
  present: number;
  pads: PortPad[];
  simCrc: number[];
};

function emptyPads(n: number): PortPad[] {
  return Array.from({ length: n }, () => ({ x: 0, y: 0, buttons: 0 }));
}

export function ckEqual(a: CkSnap, b: CkSnap): boolean {
  return (
    a.tick === b.tick &&
    a.rng_lo === b.rng_lo &&
    a.chr_rng_lo === b.chr_rng_lo &&
    a.crc_players === b.crc_players &&
    a.crc_chrs === b.crc_chrs &&
    a.crc_objectives === b.crc_objectives
  );
}

export class LockstepSession {
  nextTick = 0;
  stalled = false;
  dropped = false;
  desynced = false;
  overlay = "";
  history: InputBlock[] = [];
  readonly localCk = new Map<number, CkSnap>();
  readonly remoteCk = new Map<number, CkSnap>();
  private readonly ring: Slot[] = [];
  private readonly committed = new Set<number>();
  private waitSince: number | null = null;
  lastCrc = 0;

  constructor(
    readonly mySeat: number,
    readonly nseats: number,
    readonly delay: number,
    private readonly engine: LockstepEngine,
  ) {}

  get halted(): boolean {
    return this.dropped || this.desynced;
  }

  endMatch(reason: string, seat = 0): LockstepEvent {
    this.dropped = true;
    this.overlay = reason;
    return { t: "drop", seat };
  }

  start(seed: number): void {
    this.engine.beginMatch(this.nseats, seed);
    for (let t = 0; t < this.delay; t++) {
      this.submit(t, this.mySeat, { x: 0, y: 0, buttons: 0 }, 0);
      this.committed.add(t);
      this.pushHistory({
        tick: t,
        seat: this.mySeat,
        nseats: this.nseats,
        delay: this.delay,
        pad: { x: 0, y: 0, buttons: 0 },
        simCrc: 0,
      });
    }
  }

  submit(tick: number, seat: number, pad: PortPad, simCrc: number): number {
    if (seat < 0 || seat >= this.nseats)
      return -1;
    if (tick < this.nextTick)
      return 0;
    if (tick >= this.nextTick + LOCKSTEP_RING)
      return -1;
    const s = this.slot(tick);
    const bit = 1 << seat;
    if (s.present & bit)
      return 0;
    s.pads[seat] = { x: pad.x, y: pad.y, buttons: pad.buttons };
    s.simCrc[seat] = simCrc >>> 0;
    s.present |= bit;
    return 1;
  }

  ingest(blocks: InputBlock[]): void {
    for (const b of blocks) {
      if (b.seat === this.mySeat)
        continue;
      if (b.nseats !== this.nseats)
        continue;
      this.submit(b.tick, b.seat, b.pad, b.simCrc);
    }
  }

  acceptRemoteCk(ck: CkSnap): LockstepEvent | null {
    this.remoteCk.set(ck.tick, ck);
    const local = this.localCk.get(ck.tick);
    if (!local)
      return null;
    if (ckEqual(local, ck))
      return null;
    this.desynced = true;
    this.overlay = `DESYNC at tick ${ck.tick}`;
    return { t: "desync", tick: ck.tick, local, remote: ck };
  }

  hasAll(tick: number): boolean {
    const s = this.slot(tick);
    const need = (1 << this.nseats) - 1;
    return s.tick === tick && (s.present & need) === need;
  }

  missingSeat(tick: number): number {
    const s = this.slot(tick);
    for (let i = 0; i < this.nseats; i++) {
      if ((s.present & (1 << i)) === 0)
        return i;
    }
    return -1;
  }

  /** One wall slice: commit local pad at next+delay, run at most one tick. */
  step(now: number, localPad: PortPad, hidden = false): LockstepEvent[] {
    const events: LockstepEvent[] = [];
    if (this.halted)
      return events;
    if (hidden) {
      this.stalled = true;
      this.overlay = "tab must stay visible.";
      events.push({ t: "hidden" });
      this.commitLocal(localPad);
      return events;
    }
    this.commitLocal(localPad);
    if (this.hasAll(this.nextTick)) {
      this.waitSince = null;
      this.stalled = false;
      if (this.overlay.startsWith("waiting") || this.overlay.startsWith("tab"))
        this.overlay = "";
      const pads = this.slot(this.nextTick).pads;
      this.engine.applyTick(this.nextTick, pads);
      const ck = this.engine.snapshot(this.nextTick);
      this.localCk.set(this.nextTick, ck);
      this.lastCrc = ck.crc_players >>> 0;
      const remote = this.remoteCk.get(this.nextTick);
      events.push({ t: "ran", tick: this.nextTick, ck });
      this.nextTick += 1;
      if (remote && !ckEqual(ck, remote)) {
        this.desynced = true;
        this.overlay = `DESYNC at tick ${ck.tick}`;
        events.push({ t: "desync", tick: ck.tick, local: ck, remote });
        return events;
      }
      return events;
    }
    const miss = this.missingSeat(this.nextTick);
    if (miss < 0)
      return events;
    if (this.waitSince == null)
      this.waitSince = now;
    const waited = now - this.waitSince;
    if (waited >= LOCKSTEP_DROP_MS) {
      this.dropped = true;
      this.overlay = `P${miss + 1} left. Match ended.`;
      events.push({ t: "drop", seat: miss });
    } else if (waited >= LOCKSTEP_STALL_MS) {
      this.stalled = true;
      this.overlay = `waiting for P${miss + 1}`;
      events.push({ t: "stall", seat: miss, ms: waited });
    }
    return events;
  }

  private commitLocal(pad: PortPad): InputBlock | null {
    const tick = this.nextTick + this.delay;
    if (this.committed.has(tick))
      return this.history[0] ?? null;
    this.submit(tick, this.mySeat, pad, this.lastCrc);
    this.committed.add(tick);
    const block: InputBlock = {
      tick,
      seat: this.mySeat,
      nseats: this.nseats,
      delay: this.delay,
      pad: { x: pad.x, y: pad.y, buttons: pad.buttons },
      simCrc: this.lastCrc >>> 0,
    };
    this.pushHistory(block);
    return block;
  }

  private pushHistory(block: InputBlock): void {
    this.history.unshift(block);
    if (this.history.length > INPUT_REDUNDANCY)
      this.history.length = INPUT_REDUNDANCY;
  }

  private slot(tick: number): Slot {
    const i = tick % LOCKSTEP_RING;
    let s = this.ring[i];
    if (!s || (s.present && s.tick !== tick)) {
      s = { tick, present: 0, pads: emptyPads(this.nseats), simCrc: Array(this.nseats).fill(0) };
      this.ring[i] = s;
    } else if (!s.present) {
      s.tick = tick;
    }
    return s;
  }
}
