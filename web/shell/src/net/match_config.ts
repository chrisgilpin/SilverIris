export const MATCH_CONFIG_BYTES = 160;
export const MATCH_CONFIG_PROTOCOL = 1;
export const MATCH_CONFIG_REGION_U = 0;

export type MatchConfig = {
  protocol: number;
  region: number;
  nseats: number;
  delayTicks: number;
  speedgraphframes: number;
  aimSight: number;
  autoAim: number;
  lookAhead: number;
  aimControl: number;
  radar: number;
  pad0: number;
  rngSeed: number;
  stage: number;
  scenario: number;
  gameLength: number;
  chars: [number, number, number, number];
  handicaps: [number, number, number, number];
  favWeapons: [[number, number], [number, number], [number, number], [number, number]];
  slider007: [number, number, number, number];
  packHash: Uint8Array;
  buildId: Uint8Array;
};

function wr16(p: Uint8Array, o: number, v: number): void {
  p[o] = v & 0xff;
  p[o + 1] = (v >>> 8) & 0xff;
}

function wr32(p: Uint8Array, o: number, v: number): void {
  p[o] = v & 0xff;
  p[o + 1] = (v >>> 8) & 0xff;
  p[o + 2] = (v >>> 16) & 0xff;
  p[o + 3] = (v >>> 24) & 0xff;
}

export function encodeMatchConfig(cfg: MatchConfig): Uint8Array {
  if (cfg.pad0 !== 0)
    throw new Error("pad0 must be 0");
  if (cfg.protocol !== MATCH_CONFIG_PROTOCOL)
    throw new Error("protocol");
  if (cfg.packHash.byteLength !== 32 || cfg.buildId.byteLength !== 20)
    throw new Error("hash/buildId size");
  const out = new Uint8Array(MATCH_CONFIG_BYTES);
  wr16(out, 0, cfg.protocol);
  out[2] = cfg.region;
  out[3] = cfg.nseats;
  out[4] = cfg.delayTicks;
  out[5] = cfg.speedgraphframes;
  out[6] = cfg.aimSight;
  out[7] = cfg.autoAim;
  out[8] = cfg.lookAhead;
  out[9] = cfg.aimControl;
  out[10] = cfg.radar;
  out[11] = 0;
  wr32(out, 12, cfg.rngSeed);
  wr32(out, 16, cfg.stage);
  wr32(out, 20, cfg.scenario);
  wr32(out, 24, cfg.gameLength);
  cfg.chars.forEach((v, i) => wr32(out, 28 + 4 * i, v));
  cfg.handicaps.forEach((v, i) => wr32(out, 44 + 4 * i, v));
  cfg.favWeapons.forEach((pair, i) => {
    wr32(out, 60 + 8 * i, pair[0]);
    wr32(out, 64 + 8 * i, pair[1]);
  });
  const dv = new DataView(out.buffer);
  cfg.slider007.forEach((v, i) => dv.setFloat32(92 + 4 * i, v, true));
  out.set(cfg.packHash, 108);
  out.set(cfg.buildId, 140);
  return out;
}

export function hexBytes(b: Uint8Array): string {
  let s = "";
  for (let i = 0; i < b.byteLength; i++)
    s += b[i].toString(16).padStart(2, "0");
  return s;
}

export function bytesFromHex(hex: string): Uint8Array {
  const n = hex.length / 2;
  const o = new Uint8Array(n);
  for (let i = 0; i < n; i++)
    o[i] = parseInt(hex.slice(i * 2, i * 2 + 2), 16);
  return o;
}

export const FIXTURE_HEX =
  "010000020203000100000100" +
  "d4c3b2a1" +
  "22000000" +
  "00000000" +
  "02000000" +
  "01000000020000000300000004000000" +
  "00000000000000000000000000000000" +
  "0000000000000000000000000000000000000000000000000000000000000000" +
  "00000000000000000000000000000000" +
  "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f" +
  "73696c766572697269732d6275696c6469642121";

export function fixtureConfig(): MatchConfig {
  const packHash = new Uint8Array(32);
  for (let i = 0; i < 32; i++)
    packHash[i] = i;
  return {
    protocol: 1,
    region: 0,
    nseats: 2,
    delayTicks: 2,
    speedgraphframes: 3,
    aimSight: 0,
    autoAim: 1,
    lookAhead: 0,
    aimControl: 0,
    radar: 1,
    pad0: 0,
    rngSeed: 0xa1b2c3d4,
    stage: 34,
    scenario: 0,
    gameLength: 2,
    chars: [1, 2, 3, 4],
    handicaps: [0, 0, 0, 0],
    favWeapons: [
      [0, 0],
      [0, 0],
      [0, 0],
      [0, 0],
    ],
    slider007: [0, 0, 0, 0],
    packHash,
    buildId: new TextEncoder().encode("silveriris-buildid!!"),
  };
}

function rd16(p: Uint8Array, o: number): number {
  return p[o] | (p[o + 1] << 8);
}

function rd32(p: Uint8Array, o: number): number {
  return (p[o] | (p[o + 1] << 8) | (p[o + 2] << 16) | (p[o + 3] << 24)) >>> 0;
}

export function decodeMatchConfig(buf: Uint8Array): MatchConfig | null {
  if (buf.byteLength !== MATCH_CONFIG_BYTES || buf[11] !== 0)
    return null;
  const protocol = rd16(buf, 0);
  if (protocol !== MATCH_CONFIG_PROTOCOL)
    return null;
  const dv = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);
  const chars: [number, number, number, number] = [rd32(buf, 28), rd32(buf, 32), rd32(buf, 36), rd32(buf, 40)];
  const handicaps: [number, number, number, number] = [rd32(buf, 44), rd32(buf, 48), rd32(buf, 52), rd32(buf, 56)];
  const favWeapons: MatchConfig["favWeapons"] = [
    [rd32(buf, 60), rd32(buf, 64)],
    [rd32(buf, 68), rd32(buf, 72)],
    [rd32(buf, 76), rd32(buf, 80)],
    [rd32(buf, 84), rd32(buf, 88)],
  ];
  const slider007: [number, number, number, number] = [
    dv.getFloat32(92, true),
    dv.getFloat32(96, true),
    dv.getFloat32(100, true),
    dv.getFloat32(104, true),
  ];
  return {
    protocol,
    region: buf[2],
    nseats: buf[3],
    delayTicks: buf[4],
    speedgraphframes: buf[5],
    aimSight: buf[6],
    autoAim: buf[7],
    lookAhead: buf[8],
    aimControl: buf[9],
    radar: buf[10],
    pad0: 0,
    rngSeed: rd32(buf, 12),
    stage: rd32(buf, 16),
    scenario: rd32(buf, 20),
    gameLength: rd32(buf, 24),
    chars,
    handicaps,
    favWeapons,
    slider007,
    packHash: buf.slice(108, 140),
    buildId: buf.slice(140, 160),
  };
}
