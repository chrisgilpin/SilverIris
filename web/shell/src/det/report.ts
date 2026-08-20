/** ROM-free field report. Schema silveriris-report/1. Never attach a dump. */

export const REPORT_SCHEMA = "silveriris-report/1";
const INPUT_MAGIC = 0x49314e42; /* BIN1 */

export type ReportPad = { x: number; y: number; buttons: number };

export type SilverIrisReport = {
  schema: typeof REPORT_SCHEMA;
  buildId: string;
  region: "U";
  packHash: string;
  nseats: number;
  seat: number;
  tick: number;
  delayTicks: number;
  speedgraphframes: number;
  checksums: Array<{
    tick: number;
    rng_lo: number;
    chr_rng_lo: number;
    crc_players: number;
    crc_chrs: number;
    crc_objectives: number;
  }>;
  tapeExcerpt: { fromTick: number; toTick: number; pads: string };
  flags: { netplay: boolean; turnForce: boolean; wsRelay: boolean; widescreen: boolean };
};

function wrU32(b: Uint8Array, o: number, v: number): void {
  b[o] = v & 0xff;
  b[o + 1] = (v >>> 8) & 0xff;
  b[o + 2] = (v >>> 16) & 0xff;
  b[o + 3] = (v >>> 24) & 0xff;
}

/** Pack last N ticks as concatenated 20-byte InputBlocks (seat 0). */
export function encodeTapeExcerpt(
  nseats: number,
  frames: Array<{ tick: number; pads: ReportPad[] }>,
): { fromTick: number; toTick: number; pads: string } {
  const n = Math.max(1, Math.min(4, nseats | 0));
  const buf = new Uint8Array(frames.length * 20);
  frames.forEach((fr, i) => {
    const o = i * 20;
    const pad = fr.pads[0] ?? { x: 0, y: 0, buttons: 0 };
    wrU32(buf, o, INPUT_MAGIC);
    wrU32(buf, o + 4, fr.tick >>> 0);
    buf[o + 8] = 0;
    buf[o + 9] = n;
    buf[o + 10] = 0;
    buf[o + 11] = 0;
    buf[o + 12] = pad.x;
    buf[o + 13] = pad.y;
    buf[o + 14] = pad.buttons & 0xff;
    buf[o + 15] = (pad.buttons >>> 8) & 0xff;
    wrU32(buf, o + 16, 0);
  });
  let bin = "";
  buf.forEach((c) => {
    bin += String.fromCharCode(c);
  });
  const fromTick = frames.length ? frames[0].tick : 0;
  const toTick = frames.length ? frames[frames.length - 1].tick : 0;
  return { fromTick, toTick, pads: btoa(bin) };
}

export function buildReport(
  partial: Omit<SilverIrisReport, "schema" | "region" | "delayTicks" | "speedgraphframes"> & {
    delayTicks?: number;
    speedgraphframes?: number;
  },
): SilverIrisReport {
  return {
    schema: REPORT_SCHEMA,
    region: "U",
    delayTicks: partial.delayTicks ?? 0,
    speedgraphframes: partial.speedgraphframes ?? 3,
    ...partial,
    packHash: partial.packHash ?? "",
    tapeExcerpt: partial.tapeExcerpt ?? { fromTick: 0, toTick: 0, pads: "" },
  };
}
