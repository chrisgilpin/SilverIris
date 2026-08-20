import { describe, expect, it } from "vitest";
import { extractRom } from "./extract.ts";
import { parseImagesDef, syncImagelist } from "./imagelist.ts";

const HELLO1172 = Uint8Array.from(
  atob("EXLLSM3JyVcozswpSy3KLMosVjA0NDdSKEktLlEoS00uyS8CAA=="),
  (c) => c.charCodeAt(0),
);

describe("syncImagelist", () => {
  it("pairs IMAGE() names with CSV offsets", () => {
    const def = "IMAGE(COPYICON, 0x754, HIT_DEFAULT)\nIMAGE(X, 0x16A, HIT_DEFAULT)\n";
    const csv = "10,4,assets/images/split/image0.bin,0,1\n20,2,assets/images/split/image1.bin,0,1\n";
    const rows = syncImagelist(def, csv);
    expect(parseImagesDef(def)).toEqual(["COPYICON", "X"]);
    expect(rows[0].name).toBe("assets/images/split/COPYICON.bin");
    expect(rows[0].offset).toBe(10);
    expect(rows[1].name).toBe("assets/images/split/X.bin");
  });
});

describe("extractRom", () => {
  it("extracts uncompressed and 1172-compressed rows from a synthetic ROM", async () => {
    const rom = new Uint8Array(12 * 1024 * 1024);
    rom.set([0x61, 0x62, 0x63], 100);
    rom.set(HELLO1172, 200);
    const filelist = [
      "100,3,assets/test.bin,0,1",
      `200,${HELLO1172.byteLength},assets/hello.bin,1,1`,
      "0,0,assets/skip.bin,0,0",
      "100,3,assets/obseg/bg/bg_ark_all_p.bin,0,0",
      "",
    ].join("\n");
    const files = await extractRom(rom, {
      filelistCsv: filelist,
      imagelistCsv: "",
      imagesDef: "",
    });
    expect(files).toHaveLength(3);
    expect(files[0].path).toBe("assets/test.bin");
    expect(new TextDecoder().decode(files[0].bytes)).toBe("abc");
    expect(files[1].path).toBe("assets/hello.bin");
    expect(new TextDecoder().decode(files[1].bytes)).toBe(
      "hello silveriris 1172 test vector",
    );
    expect(files[0].sha256).toMatch(/^[0-9a-f]{64}$/);
    expect(files[2].path).toBe("assets/obseg/bg/bg_ark_all_p.bin");
    expect(new TextDecoder().decode(files[2].bytes)).toBe("abc");
  });
});
