import { describe, expect, it } from "vitest";
import { lastAudioError, startAudio, unlockAudio } from "./player.ts";

describe("startAudio", () => {
  it("returns null when Web Audio is missing", async () => {
    expect(typeof AudioContext).toBe("undefined");
    expect(unlockAudio()).toBeNull();
    const player = await startAudio(() => {
      throw new Error("fill must not run without a context");
    }, 22050);
    expect(player).toBeNull();
    expect(lastAudioError()).toMatch(/no Web Audio/i);
  });
});
