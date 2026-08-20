import { parseExtractCsv, type CsvRow } from "./csv.ts";
import { inflate1172 } from "./inflate1172.ts";
import { syncImagelist } from "./imagelist.ts";
import { sha256Hex } from "./sha256.ts";

export type ExtractProgress = {
  done: number;
  total: number;
  current: string;
};

export type ExtractedFile = {
  path: string;
  sha256: string;
  bytes: Uint8Array;
};

export type ExtractOptions = {
  filelistCsv: string;
  imagelistCsv: string;
  imagesDef: string;
  /** Pass 3: asp/gsp/rsp from inflated cdata. Off in v1 (HLE, flags.bit1=0). */
  includeUcode?: boolean;
  onProgress?: (p: ExtractProgress) => void;
};

const US_CDATA_OFFSET = 137616;
const US_CDATA_SIZE = 71760;

/**
 * NTSC-U animation table DMA.
 * Verified against n64decomp/007 `scripts/filelist.u.csv` (not .j/.e) and
 * `ge007.ld` (animation_entries → animation_data → Globalimagetable).
 * Entries 1198784+1482432 == data 2681216; data+59360 == Globalimagetable 2740576.
 * JP entries start at 1202144 — do not use those. Matching filelist marks
 * extract=0 ("don't write to disk"); the port pack still needs the blobs for
 * ANIM_idle rest. Skip if the ROM range is missing.
 */
export const ANIM_TABLE_U: readonly {
  offset: number;
  size: number;
  name: string;
}[] = [
  { offset: 1198784, size: 1482432, name: "assets/animationtable_entries.bin" },
  { offset: 2681216, size: 59360, name: "assets/animationtable_data.bin" },
];

export function isAnimTablePath(name: string): boolean {
  return ANIM_TABLE_U.some((r) => r.name === name);
}

function pull(rom: Uint8Array, row: CsvRow): Uint8Array | null {
  const end = row.offset + row.size;
  if (row.offset < 0 || row.size < 0 || end > rom.byteLength) {
    /* Optional rest-pose blobs: skip-if-missing, do not abort the pack. */
    if (isAnimTablePath(row.name)) return null;
    throw new Error(`ROM range OOB for ${row.name}`);
  }
  const slice = rom.subarray(row.offset, end);
  if (row.compressed) return inflate1172(slice);
  return slice.slice();
}

async function extractRows(
  rom: Uint8Array,
  rows: CsvRow[],
  out: ExtractedFile[],
  progress: { done: number; total: number },
  onProgress?: (p: ExtractProgress) => void,
): Promise<void> {
  for (const row of rows) {
    /* extract=0 is matching-build "don't write to disk"; the port pack
     * still needs those DMA blobs (bg/stan). Skip only empty ranges. */
    if (row.size === 0) continue;
    progress.done++;
    onProgress?.({
      done: progress.done,
      total: progress.total,
      current: row.name,
    });
    const bytes = pull(rom, row);
    if (!bytes) continue;
    out.push({
      path: row.name,
      sha256: await sha256Hex(bytes),
      bytes,
    });
  }
}

async function extractAnimTables(
  rom: Uint8Array,
  out: ExtractedFile[],
): Promise<void> {
  const have = new Set(out.map((f) => f.path));
  for (const row of ANIM_TABLE_U) {
    if (have.has(row.name)) continue;
    const end = row.offset + row.size;
    if (row.offset < 0 || row.size <= 0 || end > rom.byteLength) continue;
    const bytes = rom.subarray(row.offset, end).slice();
    out.push({
      path: row.name,
      sha256: await sha256Hex(bytes),
      bytes,
    });
    have.add(row.name);
  }
}

export const EXTRACT_RUNTIME = "dma-v3";

export async function extractRom(
  z64: Uint8Array,
  maps: ExtractOptions,
): Promise<ExtractedFile[]> {
  const fileRows = parseExtractCsv(maps.filelistCsv);
  const imageRows = syncImagelist(maps.imagesDef, maps.imagelistCsv);
  const fileExtract = fileRows.filter((r) => r.size > 0);
  const total =
    fileExtract.length + imageRows.length + (maps.includeUcode ? 5 : 0);
  const progress = { done: 0, total };
  const out: ExtractedFile[] = [];

  maps.onProgress?.({
    done: 0,
    total,
    current: `${EXTRACT_RUNTIME} ${fileExtract.length} filelist rows`,
  });
  await extractRows(z64, fileRows, out, progress, maps.onProgress);
  await extractRows(z64, imageRows, out, progress, maps.onProgress);
  await extractAnimTables(z64, out);

  if (maps.includeUcode) {
    await extractUcode(z64, out, progress, maps.onProgress);
  }
  return out;
}

/** Port of scripts/extract_asp_gsp_rsp.sh (US). Optional; v1 boots without these. */
export async function extractUcode(
  z64: Uint8Array,
  out: ExtractedFile[],
  progress: { done: number; total: number },
  onProgress?: (p: ExtractProgress) => void,
): Promise<void> {
  const cdata = inflate1172(z64.subarray(US_CDATA_OFFSET, US_CDATA_OFFSET + US_CDATA_SIZE));
  const ramStart = 0x80020d90;
  const slices: [string, number, number][] = [
    ["bin/rspboot.text.bin", 0x80020d90, 0x80020e60],
    ["bin/gspboot.text.bin", 0x80020e60, 0x80022280],
    ["bin/aspboot.text.bin", 0x80022280, 0x80023040],
    ["bin/gspboot.data.bin", 0x8005c820, 0x8005d020],
    ["bin/aspboot.data.bin", 0x8005d020, 0x8005d2e0],
  ];
  for (const [path, start, end] of slices) {
    const off = start - ramStart;
    const size = end - start;
    const bytes = cdata.subarray(off, off + size).slice();
    progress.done++;
    onProgress?.({ done: progress.done, total: progress.total, current: path });
    out.push({ path, sha256: await sha256Hex(bytes), bytes });
  }
}
