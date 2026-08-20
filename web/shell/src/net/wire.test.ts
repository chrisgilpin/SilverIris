import { describe, expect, it } from "vitest";
import { nickOk, packHashOk, parseSignalFrame, validateIce, validateSdp } from "./wire.ts";

describe("signal wire", () => {
  it("accepts offer SDP starting with v=", () => {
    expect(validateSdp({ type: "offer", sdp: "v=0\r\no=- 1 1 IN IP4 0.0.0.0\r\n" })).toBe(true);
    expect(validateSdp({ type: "pranswer", sdp: "v=0" })).toBe(false);
    expect(validateSdp({ type: "offer", sdp: "not-sdp" })).toBe(false);
  });

  it("accepts ICE candidate lines", () => {
    expect(validateIce({ candidate: "candidate:1 1 UDP 1 127.0.0.1 9 typ host" })).toBe(true);
    expect(validateIce({ candidate: "a=candidate:1" })).toBe(false);
  });

  it("rejects email nicks and bad pack hashes", () => {
    expect(nickOk("Bond")).toBe(true);
    expect(nickOk("a@b.c")).toBe(false);
    expect(packHashOk("ab")).toBe(false);
    expect(packHashOk("a".repeat(64))).toBe(true);
  });

  it("parses v:1 frames", () => {
    const m = parseSignalFrame(JSON.stringify({ v: 1, t: "hello", proto: 1 }));
    expect(m.t).toBe("hello");
  });
});
