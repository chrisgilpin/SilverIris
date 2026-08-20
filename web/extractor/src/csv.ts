export type CsvRow = {
  offset: number;
  size: number;
  name: string;
  compressed: boolean;
  extract: boolean;
};

/** `offset,size,name,compressed,extract` — unix newlines, no comments. */
export function parseExtractCsv(text: string): CsvRow[] {
  const rows: CsvRow[] = [];
  const lines = text.split(/\r?\n/);
  for (const line of lines) {
    if (line.length === 0) continue;
    const parts = line.split(",");
    if (parts.length < 5) {
      throw new Error(`bad csv line: ${line.slice(0, 80)}`);
    }
    const offset = Number(parts[0]);
    const size = Number(parts[1]);
    const name = parts[2];
    const compressed = Number(parts[3]);
    const extract = Number(parts[4]);
    if (!Number.isFinite(offset) || !Number.isFinite(size) || !name) {
      throw new Error(`bad csv fields: ${line.slice(0, 80)}`);
    }
    rows.push({
      offset,
      size,
      name,
      compressed: compressed === 1,
      extract: extract === 1,
    });
  }
  return rows;
}
