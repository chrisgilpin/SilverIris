import { describe, expect, it } from "vitest";
import { HOLD_WORKLET_SOURCE } from "./hold_tab.ts";

describe("hold_tab", () => {
  it("ships a silent AudioWorklet processor", () => {
    expect(HOLD_WORKLET_SOURCE).toContain("AudioWorkletProcessor");
    expect(HOLD_WORKLET_SOURCE).toContain("registerProcessor");
    expect(HOLD_WORKLET_SOURCE).toContain("silveriris-hold");
  });
});
