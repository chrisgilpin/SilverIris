import { describe, expect, it } from "vitest";
import { PcmRing, s16StereoToChannels } from "./pcm.ts";

describe("s16StereoToChannels", () => {
  it("splits interleaved s16 into float channels", () => {
    const l = new Float32Array(2);
    const r = new Float32Array(2);
    expect(s16StereoToChannels(new Int16Array([32767, -32768, 0, 16384]), l, r)).toBe(2);
    expect(l[0]).toBeCloseTo(32767 / 32768, 5);
    expect(r[0]).toBeCloseTo(-1, 5);
    expect(l[1]).toBe(0);
    expect(r[1]).toBeCloseTo(0.5, 5);
  });
});

describe("PcmRing", () => {
  it("converts s16 stereo and reads 1:1", () => {
    const ring = new PcmRing(8);
    const src = new Int16Array([32767, -32768, 0, 16384]);
    expect(ring.pushS16(src)).toBe(2);
    expect(ring.length).toBe(2);
    const l = new Float32Array(2);
    const r = new Float32Array(2);
    ring.readResampled(l, r, 22050, 22050);
    expect(l[0]).toBeCloseTo(32767 / 32768, 5);
    expect(r[0]).toBeCloseTo(-1, 5);
    expect(l[1]).toBe(0);
    expect(r[1]).toBeCloseTo(16384 / 32768, 5);
    expect(ring.length).toBe(0);
  });

  it("silence-fills on underrun", () => {
    const ring = new PcmRing(4);
    ring.pushS16(new Int16Array([1000, 2000]));
    const l = new Float32Array(3);
    const r = new Float32Array(3);
    ring.readResampled(l, r, 22050, 22050);
    expect(l[0]).not.toBe(0);
    expect(l[1]).toBe(0);
    expect(r[1]).toBe(0);
    expect(l[2]).toBe(0);
  });

  it("drops when full", () => {
    const ring = new PcmRing(2);
    expect(ring.pushS16(new Int16Array([1, 2, 3, 4, 5, 6]))).toBe(2);
    expect(ring.length).toBe(2);
  });

  it("nearest-neighbour downsamples when output rate is higher", () => {
    const ring = new PcmRing(8);
    ring.pushS16(new Int16Array([32767, 32767, -32768, -32768]));
    const l = new Float32Array(4);
    const r = new Float32Array(4);
    ring.readResampled(l, r, 22050, 44100);
    expect(l[0]).toBeCloseTo(32767 / 32768, 5);
    expect(l[1]).toBeCloseTo(32767 / 32768, 5);
    expect(l[2]).toBeCloseTo(-1, 5);
    expect(l[3]).toBeCloseTo(-1, 5);
  });
});
