import { describe, expect, it } from "vitest";
import { formatStageDebug, lastDrawLabel, packHashBytes, PORT_DRAW_FALLBACK, PORT_DRAW_STAGE, readHeapI32, shouldBlitStageFb } from "./bridge.ts";
import { presentLiveView } from "./view.ts";

describe("packHashBytes", () => {
  it("decodes a 32-byte sha256 hex", () => {
    const hex = "923e540c0e84a0ef240b9d440fc6d0e6ac719c5b9c0eb9871fe5b145aaa42fc3";
    const b = packHashBytes(hex);
    expect(b.byteLength).toBe(32);
    expect(b[0]).toBe(0x92);
    expect(b[31]).toBe(0xc3);
  });
});

describe("bridge stage blit", () => {
  it("shouldBlitStageFb matches port_api gdl_raw / gdl_c0", () => {
    expect(shouldBlitStageFb({ gdlRaw: false, gdlC0: false })).toBe(false);
    expect(shouldBlitStageFb({ gdlRaw: true, gdlC0: false })).toBe(true);
    expect(shouldBlitStageFb({ gdlRaw: false, gdlC0: true })).toBe(true);
  });

  it("present path uses stage FB bytes when drawable (not placeholder sky)", () => {
    const ops: string[] = [];
    let last: Uint8ClampedArray | null = null;
    const ctx = {
      canvas: { width: 4, height: 4 },
      fillStyle: "",
      strokeStyle: "",
      lineWidth: 1,
      save() {},
      restore() {},
      beginPath() {},
      closePath() {},
      clip() {},
      rect() {},
      translate() {},
      moveTo() {},
      lineTo() {},
      fill() {},
      stroke() {},
      strokeRect() {},
      fillRect() {
        ops.push(`fill ${this.fillStyle}`);
      },
      createImageData(w: number, h: number) {
        return { width: w, height: h, data: new Uint8ClampedArray(w * h * 4) };
      },
      putImageData(img: { data: Uint8ClampedArray }) {
        last = new Uint8ClampedArray(img.data);
        ops.push("putImageData");
      },
      drawImage() {
        ops.push("drawImage");
      },
    } as unknown as CanvasRenderingContext2D;
    const rgba = new Uint8ClampedArray(4 * 4 * 4);
    rgba[0] = 12;
    rgba[1] = 28;
    rgba[2] = 48;
    rgba[3] = 255;
    const src = shouldBlitStageFb({ gdlRaw: false, gdlC0: true }) ? PORT_DRAW_STAGE : PORT_DRAW_FALLBACK;
    expect(src).toBe(PORT_DRAW_STAGE);
    const used = presentLiveView(ctx, {
      gdlRaw: false,
      gdlC0: true,
      fb: { rgba, w: 4, h: 4 },
      cam: { x: 0, z: 0, theta: 0 },
      hits: [],
    });
    expect(used).toBe("stage");
    expect(ops).toContain("putImageData");
    expect(last![0]).toBe(12);
    expect(last![2]).toBe(48);
    expect(ops.some((o) => o.includes("#1a2430"))).toBe(false);
  });
});

describe("stage debug line", () => {
  it("formats last_draw, rooms, gdlC0, fbNonzero", () => {
    expect(lastDrawLabel(PORT_DRAW_STAGE)).toBe("STAGE");
    expect(lastDrawLabel(PORT_DRAW_FALLBACK)).toBe("FALLBACK");
    expect(lastDrawLabel(0)).toBe("NONE");
    expect(
      formatStageDebug({ lastDraw: PORT_DRAW_STAGE, rooms: 12, gdlC0: true, fbNonzero: 10712 }),
    ).toBe("last_draw STAGE  rooms 12  gdlC0 1  fbNonzero 10712");
    expect(
      formatStageDebug({ lastDraw: PORT_DRAW_STAGE, rooms: 78, gdlC0: true, fbNonzero: 4880, settex: 4, texOk: 0, texMiss: 4 }),
    ).toBe("last_draw STAGE  rooms 78  gdlC0 1  fbNonzero 4880  settex 4  texOk 0  texMiss 4");
  });
});

describe("HUD i32 heap view", () => {
  it("reads mag/reserve/hits/kills as little-endian i32, not HEAPF32 bits", () => {
    const buf = new Uint8Array(20);
    const dv = new DataView(buf.buffer);
    dv.setInt32(0, 7, true);
    dv.setInt32(4, 21, true);
    dv.setInt32(8, 0, true);
    dv.setInt32(12, 0, true);
    dv.setInt32(16, 8, true);
    expect(readHeapI32(buf, 0)).toBe(7);
    expect(readHeapI32(buf, 4)).toBe(21);
    expect(readHeapI32(buf, 8)).toBe(0);
    expect(readHeapI32(buf, 12)).toBe(0);
    expect(readHeapI32(buf, 16)).toBe(8);
    dv.setFloat32(0, 1.0, true);
    expect(readHeapI32(buf, 0)).toBe(1065353216);
  });
});
