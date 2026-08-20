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

function pull(rom: Uint8Array, row: CsvRow): Uint8Array {
  const end = row.offset + row.size;
  if (row.offset < 0 || row.size < 0 || end > rom.byteLength) {
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
    out.push({
      path: row.name,
      sha256: await sha256Hex(bytes),
      bytes,
    });
  }
}

export const EXTRACT_RUNTIME = "dma-v2";

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
