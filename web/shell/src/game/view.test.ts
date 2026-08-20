import { describe, expect, it } from "vitest";
import { lookDir, PORT_WALL_Z, projectWorld, wallHitscan } from "./view.ts";

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
});
