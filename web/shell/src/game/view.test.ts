import { describe, expect, it } from "vitest";
import {
  blitRgbaToCanvas,
  horPlusHfovDeg,
  lookDir,
  overlayGunGeom,
  PORT_NATIVE_FOVY,
  PORT_WALL_Z,
  presentLiveView,
  projectWorld,
  stageHasDrawableRooms,
  wallHitscan,
} from "./view.ts";

describe("port view", () => {
  it("theta 0 look dir is -Z", () => {
    const d = lookDir(0);
    expect(d.dx).toBeCloseTo(0, 5);
    expect(d.dz).toBeCloseTo(-1, 5);
  });

  it("hitscan from spawn hits the PORT wall at x=0", () => {
    const h = wallHitscan({ x: 0, z: 0, theta: 0 });
    expect(h).not.toBeNull();
    expect(h!.z).toBeCloseTo(PORT_WALL_Z, 4);
    expect(h!.x).toBeCloseTo(0, 4);
  });

  it("projects that hit near screen center", () => {
    const p = projectWorld(0, 0, PORT_WALL_Z, { x: 0, z: 0, theta: 0 }, 320, 240);
    expect(p).not.toBeNull();
    expect(p!.sx).toBeCloseTo(160, 0);
    expect(p!.sy).toBeCloseTo(120, 0);
  });

  it("looking away from the wall has no hitscan", () => {
    expect(wallHitscan({ x: 0, z: 0, theta: 180 })).toBeNull();
  });

  it("Hor+ widens hfov at 16:9 vs 4:3 with fixed vfov", () => {
    const a = horPlusHfovDeg(PORT_NATIVE_FOVY, 4 / 3);
    const b = horPlusHfovDeg(PORT_NATIVE_FOVY, 16 / 9);
    expect(a).toBeGreaterThan(74);
    expect(a).toBeLessThan(76);
    expect(b).toBeGreaterThan(90);
    expect(b).toBeLessThan(93);
    expect(b).toBeGreaterThan(a);
  });
});


describe("overlay PP7 pitch", () => {
  it("phi=0 matches the historical yaw-only mesh", () => {
    const g = overlayGunGeom(320, 240, 0);
    expect(g.tipX).toBeCloseTo(160, 5);
    expect(g.tipY).toBeCloseTo(240 * 0.74, 5);
    expect(g.baseLX).toBeCloseTo(320 * 0.42, 5);
    expect(g.baseLY).toBeCloseTo(240, 5);
    expect(g.baseRX).toBeCloseTo(320 * 0.58, 5);
    expect(g.slideX).toBeCloseTo(320 * 0.48, 5);
    expect(g.slideY).toBeCloseTo(240 * 0.74, 5);
    expect(g.slideW).toBeCloseTo(320 * 0.04, 5);
    expect(g.slideH).toBeCloseTo(240 * 0.08, 5);
  });

  it("look down (-45) raises the tip and shows more slide", () => {
    const z = overlayGunGeom(320, 240, 0);
    const d = overlayGunGeom(320, 240, -45);
    expect(d.tipY).toBeLessThan(z.tipY);
    expect(d.slideH).toBeGreaterThan(z.slideH);
    expect(d.slideY).toBe(d.tipY);
  });

  it("look up (+45) tucks the tip and shrinks the slide", () => {
    const z = overlayGunGeom(320, 240, 0);
    const u = overlayGunGeom(320, 240, 45);
    expect(u.tipY).toBeGreaterThan(z.tipY);
    expect(u.slideH).toBeLessThan(z.slideH);
    expect(u.slideY).toBe(u.tipY);
  });

  it("does not composite the PORT trapezoid on the G1 overlay path", () => {
    const rec = mockCtx(320, 240);
    const fb = new Uint8ClampedArray(320 * 240 * 4);
    fb[3] = 255;
    presentLiveView(rec.ctx, {
      gdlRaw: true,
      gdlC0: false,
      fb: { rgba: fb, w: 320, h: 240 },
      cam: { x: 0, z: 0, theta: 0, phi: -45 },
      hits: [],
    });
    expect(rec.ops.some((o) => o.includes("#2a2a28") || o.includes("#3a3a38"))).toBe(false);
  });

  it("does not stamp yellow debug prims on the live G1 canvas by default", () => {
    const rec = mockCtx(320, 240);
    const fb = new Uint8ClampedArray(320 * 240 * 4);
    fb[3] = 255;
    presentLiveView(rec.ctx, {
      gdlRaw: true,
      gdlC0: false,
      fb: { rgba: fb, w: 320, h: 240 },
      cam: { x: 0, z: 0, theta: 0 },
      hits: [{ x: 0, y: 0, z: PORT_WALL_Z }],
      chrs: [{ x: 0, z: PORT_WALL_Z, theta: 0, setup: true }],
    });
    const yellow = rec.ops.some(
      (o) =>
        o.includes("#e8c14a") ||
        o.includes("232,193,74") ||
        o.includes("rgba(232,193,74"),
    );
    expect(yellow).toBe(false);
  });

  it("draws yellow hit-cylinder overlay only with debug=true", () => {
    const rec = mockCtx(320, 240);
    const fb = new Uint8ClampedArray(320 * 240 * 4);
    fb[3] = 255;
    presentLiveView(rec.ctx, {
      gdlRaw: true,
      gdlC0: false,
      fb: { rgba: fb, w: 320, h: 240 },
      cam: { x: 0, z: 0, theta: 0 },
      hits: [{ x: 0, y: 0, z: PORT_WALL_Z }],
      chrs: [{ x: 0, z: PORT_WALL_Z, theta: 0, setup: true }],
      debug: true,
    });
    expect(rec.ops.some((o) => o.includes("232,193,74") || o.includes("#e8c14a"))).toBe(true);
  });
});
describe("stage G1 present", () => {
  it("only treats raw Fast3D or inflated C0 as drawable", () => {
    expect(stageHasDrawableRooms({ gdlRaw: false, gdlC0: false })).toBe(false);
    expect(stageHasDrawableRooms({ gdlRaw: true, gdlC0: false })).toBe(true);
    expect(stageHasDrawableRooms({ gdlRaw: false, gdlC0: true })).toBe(true);
  });

  it("blits the stage FB when a drawable GDL is present", () => {
    const rec = mockCtx(320, 240);
    const fb = new Uint8ClampedArray(320 * 240 * 4);
    fb[0] = 12;
    fb[1] = 28;
    fb[2] = 48;
    fb[3] = 255;
    const used = presentLiveView(rec.ctx, {
      gdlRaw: false,
      gdlC0: true,
      fb: { rgba: fb, w: 320, h: 240 },
      cam: { x: 0, z: 0, theta: 0 },
      hits: [],
    });
    expect(used).toBe("stage");
    expect(rec.ops.some((o) => o.startsWith("putImageData"))).toBe(true);
    expect(rec.lastImage![0]).toBe(12);
    expect(rec.lastImage![1]).toBe(28);
    expect(rec.lastImage![2]).toBe(48);
    expect(rec.ops.some((o) => o.includes("#1a2430"))).toBe(false);
  });

  it("keeps the placeholder mesh when the pack has no drawable rooms", () => {
    const rec = mockCtx(320, 240);
    const fb = new Uint8ClampedArray(320 * 240 * 4);
    fb.fill(9);
    const used = presentLiveView(rec.ctx, {
      gdlRaw: false,
      gdlC0: false,
      fb: { rgba: fb, w: 320, h: 240 },
      cam: { x: 0, z: 0, theta: 0 },
      hits: [],
    });
    expect(used).toBe("placeholder");
    expect(rec.ops.some((o) => o.startsWith("putImageData"))).toBe(false);
    expect(rec.ops.some((o) => o.includes("#1a2430"))).toBe(true);
  });

  it("copies FB bytes through blitRgbaToCanvas", () => {
    const rec = mockCtx(8, 8);
    const rgba = new Uint8ClampedArray(8 * 8 * 4);
    rgba[4] = 200;
    expect(blitRgbaToCanvas(rec.ctx, rgba, 8, 8)).toBe(true);
    expect(rec.lastImage![4]).toBe(200);
  });
});

function mockCtx(w: number, h: number) {
  const ops: string[] = [];
  let lastImage: Uint8ClampedArray | null = null;
  const ctx = {
    canvas: { width: w, height: h },
    fillStyle: "",
    strokeStyle: "",
    lineWidth: 1,
    font: "",
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
    fillText() {},
    drawImage() {
      ops.push("drawImage");
    },
    fillRect(_x: number, _y: number, fw: number, fh: number) {
      ops.push(`fillRect ${String(this.fillStyle)} y=${_y} ${fw}x${fh}`);
    },
    createImageData(iw: number, ih: number) {
      return { width: iw, height: ih, data: new Uint8ClampedArray(iw * ih * 4) };
    },
    putImageData(img: { data: Uint8ClampedArray }, _x: number, _y: number) {
      lastImage = new Uint8ClampedArray(img.data);
      ops.push("putImageData");
    },
  };
  return { ops, get lastImage() { return lastImage; }, ctx: ctx as unknown as CanvasRenderingContext2D };
}
