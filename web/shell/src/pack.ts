import { sha256Hex, sha256Software } from "../../extractor/src/sha256.ts";

export const C0PACK_MAGIC = "C0PK";
export const C0PACK_VERSION = 1;
export const C0PACK_HEADER_SIZE = 44;

export type PackRegion = "U" | "J" | "E";

export type PackInputFile = {
  path: string;
  bytes: Uint8Array;
  sha256?: string;
};

export type PackParsedFile = {
  path: string;
  offset: number;
  size: number;
  sha256: string;
  codec: number;
  bytes: Uint8Array;
};

export type BuiltPack = {
  bytes: Uint8Array;
  packHash: string;
  region: PackRegion;
  flags: number;
  fileCount: number;
};

const PATH_RE = /^[A-Za-z0-9_./-]+$/;
const REGION_CODE: Record<PackRegion, number> = { U: 0, J: 1, E: 2 };
const REGION_FROM: PackRegion[] = ["U", "J", "E"];

export function validatePackPath(path: string): void {
  if (path.length === 0 || path.length > 65535) {
    throw new Error(`invalid pack path length: ${path}`);
  }
  if (path.startsWith("/") || path.includes("\\") || path.includes("..")) {
    throw new Error(`invalid pack path: ${path}`);
  }
  if (!path.startsWith("assets/") && !path.startsWith("bin/")) {
    throw new Error(`pack path must start with assets/ or bin/: ${path}`);
  }
  if (!PATH_RE.test(path)) {
    throw new Error(`pack path alphabet: ${path}`);
  }
}

function writeU16(out: Uint8Array, o: number, v: number): void {
  out[o] = v & 0xff;
  out[o + 1] = (v >>> 8) & 0xff;
}

function writeU32(out: Uint8Array, o: number, v: number): void {
  out[o] = v & 0xff;
  out[o + 1] = (v >>> 8) & 0xff;
  out[o + 2] = (v >>> 16) & 0xff;
  out[o + 3] = (v >>> 24) & 0xff;
}

function writeU64(out: Uint8Array, o: number, v: number): void {
  writeU32(out, o, v >>> 0);
  writeU32(out, o + 4, Math.floor(v / 0x100000000));
}

function readU16(buf: Uint8Array, o: number): number {
  return buf[o] | (buf[o + 1] << 8);
}

function readU32(buf: Uint8Array, o: number): number {
  return (
    (buf[o] | (buf[o + 1] << 8) | (buf[o + 2] << 16) | (buf[o + 3] << 24)) >>> 0
  );
}

function readU64(buf: Uint8Array, o: number): number {
  const lo = readU32(buf, o);
  const hi = readU32(buf, o + 4);
  return hi * 0x100000000 + lo;
}

export function hexToBytes(hex: string): Uint8Array {
  if (hex.length !== 64 || /[^0-9a-f]/i.test(hex)) {
    throw new Error("sha256 hex must be 64 lowercase hex chars");
  }
  const out = new Uint8Array(32);
  for (let i = 0; i < 32; i++) {
    out[i] = parseInt(hex.slice(i * 2, i * 2 + 2), 16);
  }
  return out;
}

export function bytesToHex(bytes: Uint8Array): string {
  let h = "";
  for (let i = 0; i < bytes.length; i++) h += bytes[i].toString(16).padStart(2, "0");
  return h;
}

type ReadyFile = {
  path: string;
  pathBytes: Uint8Array;
  bytes: Uint8Array;
  shaRaw: Uint8Array;
  shaHex: string;
  codec: number;
};

async function prepare(files: PackInputFile[]): Promise<ReadyFile[]> {
  const ready: ReadyFile[] = [];
  const enc = new TextEncoder();
  for (const f of files) {
    validatePackPath(f.path);
    const shaHex = (f.sha256 ?? (await sha256Hex(f.bytes))).toLowerCase();
    ready.push({
      path: f.path,
      pathBytes: enc.encode(f.path),
      bytes: f.bytes,
      shaRaw: hexToBytes(shaHex),
      shaHex,
      codec: 0,
    });
  }
  ready.sort((a, b) => (a.path < b.path ? -1 : a.path > b.path ? 1 : 0));
  return ready;
}

/** Canonical manifest bytes (sorted paths, no offsets). */
export function canonicalManifest(ready: ReadyFile[]): Uint8Array {
  let len = 0;
  for (const f of ready) len += 2 + f.pathBytes.byteLength + 4 + 32 + 1;
  const out = new Uint8Array(len);
  let o = 0;
  for (const f of ready) {
    writeU16(out, o, f.pathBytes.byteLength);
    o += 2;
    out.set(f.pathBytes, o);
    o += f.pathBytes.byteLength;
    writeU32(out, o, f.bytes.byteLength);
    o += 4;
    out.set(f.shaRaw, o);
    o += 32;
    out[o] = f.codec;
    o += 1;
  }
  return out;
}

export async function buildPack(
  files: PackInputFile[],
  region: PackRegion,
  flags = 0,
): Promise<BuiltPack> {
  if (flags & 1) {
    throw new Error("zstd packs are not implemented");
  }
  const ready = await prepare(files);
  const packHash = sha256Software(canonicalManifest(ready));
  const packHashRaw = hexToBytes(packHash);

  let manifestLen = 0;
  let blobLen = 0;
  for (const f of ready) {
    manifestLen += 2 + f.pathBytes.byteLength + 8 + 4 + 32 + 1;
    blobLen += f.bytes.byteLength;
  }
  const total = C0PACK_HEADER_SIZE + manifestLen + blobLen + 32;
  const out = new Uint8Array(total);
  const enc = new TextEncoder();
  out.set(enc.encode(C0PACK_MAGIC), 0);
  writeU16(out, 4, C0PACK_VERSION);
  out[6] = REGION_CODE[region];
  out[7] = flags;
  writeU32(out, 8, ready.length);
  out.set(packHashRaw, 12);

  let mo = C0PACK_HEADER_SIZE;
  let blobOff = 0;
  for (const f of ready) {
    writeU16(out, mo, f.pathBytes.byteLength);
    mo += 2;
    out.set(f.pathBytes, mo);
    mo += f.pathBytes.byteLength;
    writeU64(out, mo, blobOff);
    mo += 8;
    writeU32(out, mo, f.bytes.byteLength);
    mo += 4;
    out.set(f.shaRaw, mo);
    mo += 32;
    out[mo] = f.codec;
    mo += 1;
    blobOff += f.bytes.byteLength;
  }
  const blobStart = mo;
  blobOff = 0;
  for (const f of ready) {
    out.set(f.bytes, blobStart + blobOff);
    blobOff += f.bytes.byteLength;
  }
  out.set(packHashRaw, blobStart + blobLen);
  return {
    bytes: out,
    packHash,
    region,
    flags,
    fileCount: ready.length,
  };
}

export function parsePack(buf: Uint8Array): {
  region: PackRegion;
  flags: number;
  packHash: string;
  files: PackParsedFile[];
} {
  if (buf.byteLength < C0PACK_HEADER_SIZE + 32) {
    throw new Error("pack truncated (header)");
  }
  const magic = String.fromCharCode(buf[0], buf[1], buf[2], buf[3]);
  if (magic !== C0PACK_MAGIC) throw new Error("bad pack magic");
  const version = readU16(buf, 4);
  if (version !== C0PACK_VERSION) throw new Error(`unsupported pack version ${version}`);
  const regionCode = buf[6];
  if (regionCode > 2) throw new Error("bad pack region");
  const flags = buf[7];
  const fileCount = readU32(buf, 8);
  const headerHash = bytesToHex(buf.subarray(12, 44));

  let o = C0PACK_HEADER_SIZE;
  const meta: Omit<PackParsedFile, "bytes">[] = [];
  for (let i = 0; i < fileCount; i++) {
    if (o + 2 > buf.byteLength) throw new Error("pack truncated (pathLen)");
    const pathLen = readU16(buf, o);
    o += 2;
    if (o + pathLen + 8 + 4 + 32 + 1 > buf.byteLength) {
      throw new Error("pack truncated (entry)");
    }
    const path = new TextDecoder().decode(buf.subarray(o, o + pathLen));
    o += pathLen;
    validatePackPath(path);
    const offset = readU64(buf, o);
    o += 8;
    const size = readU32(buf, o);
    o += 4;
    const sha256 = bytesToHex(buf.subarray(o, o + 32));
    o += 32;
    const codec = buf[o];
    o += 1;
    if (codec !== 0) throw new Error(`unsupported codec ${codec}`);
    meta.push({ path, offset, size, sha256, codec });
  }

  const blobStart = o;
  const blobLen = meta.reduce((n, f) => Math.max(n, f.offset + f.size), 0);
  if (blobStart + blobLen + 32 > buf.byteLength) {
    throw new Error("pack truncated (blob/trailer)");
  }
  const trailerHash = bytesToHex(
    buf.subarray(blobStart + blobLen, blobStart + blobLen + 32),
  );
  if (trailerHash !== headerHash) throw new Error("packHash header/trailer mismatch");

  const files: PackParsedFile[] = [];
  const ready: ReadyFile[] = [];
  const enc = new TextEncoder();
  for (const m of meta) {
    const bytes = buf.subarray(blobStart + m.offset, blobStart + m.offset + m.size).slice();
    const got = sha256Software(bytes);
    if (got !== m.sha256) {
      throw new Error(`payload hash mismatch for ${m.path}`);
    }
    files.push({ ...m, bytes });
    ready.push({
      path: m.path,
      pathBytes: enc.encode(m.path),
      bytes,
      shaRaw: hexToBytes(m.sha256),
      shaHex: m.sha256,
      codec: m.codec,
    });
  }
  ready.sort((a, b) => (a.path < b.path ? -1 : a.path > b.path ? 1 : 0));
  const recomputed = sha256Software(canonicalManifest(ready));
  if (recomputed !== headerHash) throw new Error("packHash does not match canonical manifest");

  return {
    region: REGION_FROM[regionCode],
    flags,
    packHash: headerHash,
    files,
  };
}
