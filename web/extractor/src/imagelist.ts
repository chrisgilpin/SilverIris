import type { CsvRow } from "./csv.ts";
import { parseExtractCsv } from "./csv.ts";

/** IMAGE(NAME, SIZE, ...) in order — names only. */
export function parseImagesDef(text: string): string[] {
  const names: string[] = [];
  for (const line of text.split(/\r?\n/)) {
    const t = line.trim();
    if (!t.startsWith("IMAGE(") || !t.endsWith(")")) continue;
    const inner = t.slice(6, -1);
    const name = inner.split(",")[0]?.trim();
    if (name) names.push(name);
  }
  return names;
}

/**
 * Pair images.def names with imagelist.u.csv ROM offsets/sizes.
 * Extra def entries are not in the ROM and are skipped (no on-disk sizes).
 */
export function syncImagelist(imagesDef: string, imagelistCsv: string): CsvRow[] {
  const names = parseImagesDef(imagesDef);
  const offsets = parseExtractCsv(imagelistCsv);
  const n = Math.min(names.length, offsets.length);
  const rows: CsvRow[] = [];
  for (let i = 0; i < n; i++) {
    rows.push({
      offset: offsets[i].offset,
      size: offsets[i].size,
      name: `assets/images/split/${names[i]}.bin`,
      compressed: false,
      extract: true,
    });
  }
  return rows;
}
