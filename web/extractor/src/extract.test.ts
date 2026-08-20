import { describe, expect, it } from "vitest";
import { parseExtractCsv } from "./csv.ts";
import {
  ANIM_TABLE_U,
  EXTRACT_RUNTIME,
  extractRom,
} from "./extract.ts";
import { parseImagesDef, syncImagelist } from "./imagelist.ts";
import { extractMapsU } from "./maps.ts";
import { buildPack, parsePack } from "../../shell/src/pack.ts";

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
    expect(files).toHaveLength(5);
    expect(files[0].path).toBe("assets/test.bin");
    expect(new TextDecoder().decode(files[0].bytes)).toBe("abc");
    expect(files[1].path).toBe("assets/hello.bin");
    expect(new TextDecoder().decode(files[1].bytes)).toBe(
      "hello silveriris 1172 test vector",
    );
    expect(files[0].sha256).toMatch(/^[0-9a-f]{64}$/);
    expect(files[2].path).toBe("assets/obseg/bg/bg_ark_all_p.bin");
    expect(new TextDecoder().decode(files[2].bytes)).toBe("abc");
    expect(files[3].path).toBe("assets/animationtable_entries.bin");
    expect(files[3].bytes.byteLength).toBe(ANIM_TABLE_U[0].size);
    expect(files[4].path).toBe("assets/animationtable_data.bin");
    expect(files[4].bytes.byteLength).toBe(ANIM_TABLE_U[1].size);
  });

  it("packs NTSC-U animation table DMA from a fake ROM into the c0pack", async () => {
    const rom = new Uint8Array(12 * 1024 * 1024);
    rom.set([0x61, 0x62, 0x63], 100);
    rom[ANIM_TABLE_U[0].offset] = 0xe1;
    rom[ANIM_TABLE_U[0].offset + 1] = 0xde;
    rom[ANIM_TABLE_U[1].offset] = 0xda;
    rom[ANIM_TABLE_U[1].offset + 1] = 0x7a;
    const filelist = ["100,3,assets/test.bin,0,1", ""].join("\n");
    const files = await extractRom(rom, {
      filelistCsv: filelist,
      imagelistCsv: "",
      imagesDef: "",
    });
    const byPath = Object.fromEntries(files.map((f) => [f.path, f]));
    expect(byPath["assets/test.bin"].bytes[0]).toBe(0x61);
    expect(byPath["assets/animationtable_entries.bin"].bytes.byteLength).toBe(
      1482432,
    );
    expect(byPath["assets/animationtable_entries.bin"].bytes[0]).toBe(0xe1);
    expect(byPath["assets/animationtable_entries.bin"].bytes[1]).toBe(0xde);
    expect(byPath["assets/animationtable_data.bin"].bytes.byteLength).toBe(59360);
    expect(byPath["assets/animationtable_data.bin"].bytes[0]).toBe(0xda);
    expect(byPath["assets/animationtable_data.bin"].bytes[1]).toBe(0x7a);
    const packed = await buildPack(
      files.map((f) => ({ path: f.path, bytes: f.bytes, sha256: f.sha256 })),
      "U",
    );
    const parsed = parsePack(packed.bytes);
    expect(parsed.files.map((f) => f.path).sort()).toEqual([
      "assets/animationtable_data.bin",
      "assets/animationtable_entries.bin",
      "assets/test.bin",
    ]);
    const ent = parsed.files.find((f) => f.path === "assets/animationtable_entries.bin");
    const data = parsed.files.find((f) => f.path === "assets/animationtable_data.bin");
    expect(ent?.size).toBe(1482432);
    expect(data?.size).toBe(59360);
    expect(ent?.bytes[0]).toBe(0xe1);
    expect(data?.bytes[0]).toBe(0xda);
  });

  it("skips missing animation table DMA and still packs the rest", async () => {
    const rom = new Uint8Array(1024);
    rom.set([0x61, 0x62, 0x63], 100);
    const filelist = [
      "100,3,assets/test.bin,0,1",
      `${ANIM_TABLE_U[0].offset},${ANIM_TABLE_U[0].size},${ANIM_TABLE_U[0].name},0,0`,
      `${ANIM_TABLE_U[1].offset},${ANIM_TABLE_U[1].size},${ANIM_TABLE_U[1].name},0,0`,
      "",
    ].join("\n");
    const files = await extractRom(rom, {
      filelistCsv: filelist,
      imagelistCsv: "",
      imagesDef: "",
    });
    expect(files.map((f) => f.path)).toEqual(["assets/test.bin"]);
    expect(new TextDecoder().decode(files[0].bytes)).toBe("abc");
    const packed = await buildPack(
      files.map((f) => ({ path: f.path, bytes: f.bytes, sha256: f.sha256 })),
      "U",
    );
    expect(parsePack(packed.bytes).files.map((f) => f.path)).toEqual([
      "assets/test.bin",
    ]);
  });

  it("filelist.u.csv NTSC-U animation rows match ANIM_TABLE_U (not JP)", () => {
    const rows = parseExtractCsv(extractMapsU.filelistCsv);
    const entries = rows.find((r) => r.name === ANIM_TABLE_U[0].name);
    const data = rows.find((r) => r.name === ANIM_TABLE_U[1].name);
    expect(EXTRACT_RUNTIME).toBe("dma-v3");
    expect(entries).toMatchObject({
      offset: 1198784,
      size: 1482432,
      compressed: false,
    });
    expect(data).toMatchObject({
      offset: 2681216,
      size: 59360,
      compressed: false,
    });
    expect(entries!.offset + entries!.size).toBe(data!.offset);
  });
});
