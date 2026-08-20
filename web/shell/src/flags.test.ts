import { describe, expect, it } from "vitest";
import { readFlags } from "./flags.ts";

describe("readFlags", () => {
  it("defaults netplay off and widescreen on", () => {
    const f = readFlags("");
    expect(f.netplay).toBe(false);
    expect(f.turnForce).toBe(false);
    expect(f.wsRelay).toBe(false);
    expect(f.widescreen).toBe(true);
    expect(f.campaign).toBe(false);
  });

  it("honours ?ff_netplay=1", () => {
    expect(readFlags("?ff_netplay=1").netplay).toBe(true);
    expect(readFlags("?ff_netplay=0").netplay).toBe(false);
  });
});
