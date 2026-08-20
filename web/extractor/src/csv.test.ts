import { describe, expect, it } from "vitest";
import { parseExtractCsv } from "./csv.ts";

describe("parseExtractCsv", () => {
  it("parses a 3-line filelist", () => {
    const text = [
      "100,3,assets/test.bin,0,1",
      "200,37,assets/hello.bin,1,1",
      "0,0,assets/skip.bin,0,0",
      "",
    ].join("\n");
    const rows = parseExtractCsv(text);
    expect(rows).toHaveLength(3);
    expect(rows[0]).toEqual({
      offset: 100,
      size: 3,
      name: "assets/test.bin",
      compressed: false,
      extract: true,
    });
    expect(rows[1].compressed).toBe(true);
    expect(rows[2].extract).toBe(false);
  });
});
