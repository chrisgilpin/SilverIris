import { describe, expect, it } from "vitest";
import { readFlags } from "./flags.ts";

describe("readFlags", () => {
  it("defaults netplay on and widescreen on", () => {
    const f = readFlags("");
    expect(f.netplay).toBe(true);
    expect(f.turnForce).toBe(false);
    expect(f.wsRelay).toBe(false);
    expect(f.widescreen).toBe(true);
    expect(f.campaign).toBe(false);
    expect(f.lan).toBe(false);
  });

  it("honours ?ff_netplay=1", () => {
    expect(readFlags("?ff_netplay=1").netplay).toBe(true);
    expect(readFlags("?ff_netplay=0").netplay).toBe(false);
  });

  it("honours ?ff_lan=1 for delay-1 LAN", () => {
    expect(readFlags("?ff_lan=1").lan).toBe(true);
  });

  it("honours ?ff_turnForce=1 and ?ff_wsRelay=1", () => {
    expect(readFlags("?ff_turnForce=1").turnForce).toBe(true);
    expect(readFlags("?ff_wsRelay=1").wsRelay).toBe(true);
    expect(readFlags("").turnForce).toBe(false);
    expect(readFlags("").wsRelay).toBe(false);
  });
});
